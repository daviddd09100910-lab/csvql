#pragma once

#include "table.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace csvql {

/// Exception thrown when a SQL-like query has invalid syntax or references missing columns.
class QueryError : public std::runtime_error {
public:
    explicit QueryError(const std::string& msg) : std::runtime_error(msg) {}
};

namespace detail {

enum class TokenType {
    Ident,
    Number,
    String,
    Star,
    Comma,
    Dot,
    LParen,
    RParen,
    Semicolon,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    Plus,
    Minus,
    End
};

struct Token {
    TokenType type = TokenType::End;
    std::string text;
    double number_val = 0.0;
    std::size_t pos = 0;

    bool is_kw(const char* kw) const {
        return type == TokenType::Ident && to_upper_ascii(text) == kw;
    }
};

inline std::vector<Token> tokenize_sql(const std::string& sql) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    const std::size_t n = sql.size();

    // Strip UTF-8 BOM if passed from piped input or script files
    if (n >= 3 &&
        static_cast<unsigned char>(sql[0]) == 0xEF &&
        static_cast<unsigned char>(sql[1]) == 0xBB &&
        static_cast<unsigned char>(sql[2]) == 0xBF) {
        i = 3;
    }

    while (i < n) {
        const unsigned char uc = static_cast<unsigned char>(sql[i]);
        if (std::isspace(uc)) {
            ++i;
            continue;
        }

        const std::size_t start = i;
        const char ch = sql[i];

        if (ch == '*') {
            tokens.push_back({TokenType::Star, "*", 0.0, start});
            ++i;
        } else if (ch == ',') {
            tokens.push_back({TokenType::Comma, ",", 0.0, start});
            ++i;
        } else if (ch == '.' && !(i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
            tokens.push_back({TokenType::Dot, ".", 0.0, start});
            ++i;
        } else if (ch == '(') {
            tokens.push_back({TokenType::LParen, "(", 0.0, start});
            ++i;
        } else if (ch == ')') {
            tokens.push_back({TokenType::RParen, ")", 0.0, start});
            ++i;
        } else if (ch == ';') {
            tokens.push_back({TokenType::Semicolon, ";", 0.0, start});
            ++i;
        } else if (ch == '+') {
            tokens.push_back({TokenType::Plus, "+", 0.0, start});
            ++i;
        } else if (ch == '-') {
            tokens.push_back({TokenType::Minus, "-", 0.0, start});
            ++i;
        } else if (ch == '=') {
            tokens.push_back({TokenType::Eq, "=", 0.0, start});
            ++i;
        } else if (ch == '!' && i + 1 < n && sql[i + 1] == '=') {
            tokens.push_back({TokenType::Ne, "!=", 0.0, start});
            i += 2;
        } else if (ch == '<') {
            if (i + 1 < n && sql[i + 1] == '=') {
                tokens.push_back({TokenType::Le, "<=", 0.0, start});
                i += 2;
            } else if (i + 1 < n && sql[i + 1] == '>') {
                tokens.push_back({TokenType::Ne, "<>", 0.0, start});
                i += 2;
            } else {
                tokens.push_back({TokenType::Lt, "<", 0.0, start});
                ++i;
            }
        } else if (ch == '>') {
            if (i + 1 < n && sql[i + 1] == '=') {
                tokens.push_back({TokenType::Ge, ">=", 0.0, start});
                i += 2;
            } else {
                tokens.push_back({TokenType::Gt, ">", 0.0, start});
                ++i;
            }
        } else if (ch == '\'' || ch == '"') {
            const char quote = ch;
            ++i;
            std::string val;
            bool closed = false;
            while (i < n) {
                if (sql[i] == '\\' && i + 1 < n) {
                    val.push_back(sql[i + 1]);
                    i += 2;
                    continue;
                }
                if (sql[i] == quote) {
                    if (i + 1 < n && sql[i + 1] == quote) {
                        val.push_back(quote);
                        i += 2;
                        continue;
                    }
                    closed = true;
                    ++i;
                    break;
                }
                val.push_back(sql[i]);
                ++i;
            }
            if (!closed) {
                throw QueryError("Unterminated string literal at position " + std::to_string(start));
            }
            tokens.push_back({TokenType::String, std::move(val), 0.0, start});
        } else if (ch == '`') {
            ++i;
            std::string ident;
            bool closed = false;
            while (i < n) {
                if (sql[i] == '`') {
                    closed = true;
                    ++i;
                    break;
                }
                ident.push_back(sql[i]);
                ++i;
            }
            if (!closed || ident.empty()) {
                throw QueryError("Invalid backtick identifier at position " + std::to_string(start));
            }
            tokens.push_back({TokenType::Ident, std::move(ident), 0.0, start});
        } else if (std::isdigit(uc) || (ch == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
            std::size_t j = i;
            bool saw_dot = false;
            while (j < n) {
                const char c = sql[j];
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    ++j;
                } else if (c == '.' && !saw_dot) {
                    saw_dot = true;
                    ++j;
                } else {
                    break;
                }
            }
            if (j < n && (sql[j] == 'e' || sql[j] == 'E')) {
                std::size_t exp_pos = j + 1;
                if (exp_pos < n && (sql[exp_pos] == '+' || sql[exp_pos] == '-')) {
                    ++exp_pos;
                }
                if (exp_pos < n && std::isdigit(static_cast<unsigned char>(sql[exp_pos]))) {
                    j = exp_pos;
                    while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) {
                        ++j;
                    }
                }
            }
            const std::string raw = sql.substr(i, j - i);
            char* end = nullptr;
            const double num = std::strtod(raw.c_str(), &end);
            if (end != raw.c_str() + raw.size() || !std::isfinite(num)) {
                throw QueryError("Invalid numeric literal '" + raw + "' at position " + std::to_string(start));
            }
            tokens.push_back({TokenType::Number, raw, num, start});
            i = j;
        } else if (std::isalpha(uc) || ch == '_') {
            std::size_t j = i + 1;
            while (j < n) {
                const unsigned char cj = static_cast<unsigned char>(sql[j]);
                if (std::isalnum(cj) || sql[j] == '_') {
                    ++j;
                } else {
                    break;
                }
            }
            tokens.push_back({TokenType::Ident, sql.substr(i, j - i), 0.0, start});
            i = j;
        } else {
            throw QueryError(
                std::string("Unexpected character '") + ch + "' at position " + std::to_string(start));
        }
    }

    tokens.push_back({TokenType::End, "", 0.0, n});
    return tokens;
}

enum class AggFunc { CountStar, CountCol, Sum, Avg, Min, Max };

inline std::optional<AggFunc> parse_agg_name(const std::string& ident) {
    const std::string u = to_upper_ascii(ident);
    if (u == "COUNT") return AggFunc::CountCol;
    if (u == "SUM") return AggFunc::Sum;
    if (u == "AVG") return AggFunc::Avg;
    if (u == "MIN") return AggFunc::Min;
    if (u == "MAX") return AggFunc::Max;
    return std::nullopt;
}

inline std::string agg_func_name(AggFunc f) {
    switch (f) {
        case AggFunc::CountStar: return "COUNT";
        case AggFunc::CountCol: return "COUNT";
        case AggFunc::Sum: return "SUM";
        case AggFunc::Avg: return "AVG";
        case AggFunc::Min: return "MIN";
        case AggFunc::Max: return "MAX";
    }
    return "AGG";
}

struct AggSpec {
    AggFunc func = AggFunc::CountStar;
    std::string col_name;         // "*" for CountStar
    std::size_t col_idx = 0;      // resolved against source Table
    std::string canonical_name;   // e.g. "AVG(daily_return_pct)"

    std::string key() const {
        return std::to_string(static_cast<int>(func)) + ":" +
               (func == AggFunc::CountStar ? "*" : std::to_string(col_idx));
    }
};

struct SelectItem {
    enum class Kind { Star, Column, Aggregate };
    Kind kind = Kind::Column;
    std::string col_name;
    std::size_t col_idx = 0;
    AggSpec agg;
    std::string alias;
    std::string output_name;
};

struct Operand {
    enum class Kind { Literal, Identifier, Aggregate };
    Kind kind = Kind::Literal;
    Value literal;
    std::string ident;
    std::size_t resolved_col = 0;
    bool col_resolved = false;
    AggSpec agg;
};

enum class CmpOp { Eq, Ne, Lt, Le, Gt, Ge, Contains };

struct ExprNode {
    enum class Kind { And, Or, Not, IsNull, Compare };
    Kind kind = Kind::Compare;
    std::shared_ptr<ExprNode> left;
    std::shared_ptr<ExprNode> right;
    Operand op_lhs;
    Operand op_rhs;
    CmpOp cmp = CmpOp::Eq;
    bool is_not_null = false;
};

struct OrderByItem {
    enum class Kind { ColumnOrAlias, Aggregate };
    Kind kind = Kind::ColumnOrAlias;
    std::string name;
    AggSpec agg;
    bool ascending = true;
};

struct QueryPlan {
    std::vector<SelectItem> select_items;
    std::shared_ptr<ExprNode> where_expr;
    std::vector<std::pair<std::string, std::size_t>> group_by;
    std::shared_ptr<ExprNode> having_expr;
    std::vector<OrderByItem> order_by;
    std::optional<std::size_t> limit;
    bool has_aggregates = false;
};

class SqlParser {
public:
    SqlParser(const Table& table, std::vector<Token> tokens)
        : table_(table), tokens_(std::move(tokens)) {}

    QueryPlan parse() {
        QueryPlan plan;
        expect_kw("SELECT", "Query must start with SELECT");

        // Parse SELECT item list
        do {
            plan.select_items.push_back(parse_select_item(plan));
        } while (consume(TokenType::Comma));

        // Optional FROM <table> (accepts qualified names such as stocks.csv)
        if (peek().is_kw("FROM")) {
            advance();
            if (peek().type != TokenType::Ident && peek().type != TokenType::String) {
                throw QueryError("Expected table name after FROM at position " + std::to_string(peek().pos));
            }
            const bool was_ident = (peek().type == TokenType::Ident);
            advance();
            if (was_ident) {
                while (consume(TokenType::Dot)) {
                    if (peek().type != TokenType::Ident) {
                        throw QueryError("Expected identifier after '.' in FROM clause at position " +
                                         std::to_string(peek().pos));
                    }
                    advance();
                }
            }
        }

        // Optional WHERE
        if (peek().is_kw("WHERE")) {
            advance();
            plan.where_expr = parse_or(false);
        }

        // Optional GROUP BY
        if (peek().is_kw("GROUP")) {
            advance();
            expect_kw("BY", "Expected BY after GROUP");
            do {
                if (peek().type != TokenType::Ident) {
                    throw QueryError("Expected column name in GROUP BY at position " + std::to_string(peek().pos));
                }
                const Token tok = advance();
                const std::size_t idx = resolve_column(tok.text, tok.pos);
                plan.group_by.emplace_back(table_.headers()[idx], idx);
            } while (consume(TokenType::Comma));
        }

        // Optional HAVING
        if (peek().is_kw("HAVING")) {
            advance();
            plan.having_expr = parse_or(true);
        }

        // Optional ORDER BY
        if (peek().is_kw("ORDER")) {
            advance();
            expect_kw("BY", "Expected BY after ORDER");
            do {
                plan.order_by.push_back(parse_order_item());
            } while (consume(TokenType::Comma));
        }

        // Optional LIMIT
        if (peek().is_kw("LIMIT")) {
            advance();
            if (peek().type != TokenType::Number) {
                throw QueryError("Expected non-negative integer after LIMIT at position " + std::to_string(peek().pos));
            }
            const Token num_tok = advance();
            if (num_tok.number_val < 0.0 || !is_whole_number(num_tok.number_val)) {
                throw QueryError("LIMIT expects a non-negative integer at position " + std::to_string(num_tok.pos));
            }
            plan.limit = static_cast<std::size_t>(std::llround(num_tok.number_val));
        }

        while (consume(TokenType::Semicolon)) {}

        if (peek().type != TokenType::End) {
            throw QueryError("Unexpected token '" + peek().text + "' at position " + std::to_string(peek().pos));
        }

        validate_plan(plan);
        return plan;
    }

private:
    const Table& table_;
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;

    const Token& peek(std::size_t offset = 0) const {
        const std::size_t idx = pos_ + offset;
        if (idx >= tokens_.size()) {
            return tokens_.back();
        }
        return tokens_[idx];
    }

    const Token& advance() {
        const Token& cur = peek();
        if (pos_ + 1 < tokens_.size()) {
            ++pos_;
        }
        return cur;
    }

    bool consume(TokenType t) {
        if (peek().type == t) {
            advance();
            return true;
        }
        return false;
    }

    void expect_kw(const char* kw, const std::string& msg) {
        if (!peek().is_kw(kw)) {
            throw QueryError(msg + " (got '" + peek().text + "' at position " + std::to_string(peek().pos) + ")");
        }
        advance();
    }

    std::size_t resolve_column(const std::string& name, std::size_t pos) const {
        auto idx = table_.column_index(name);
        if (!idx.has_value()) {
            throw QueryError("Unknown column '" + name + "' at position " + std::to_string(pos));
        }
        return *idx;
    }

    AggSpec parse_agg_call(const Token& fn_tok) {
        auto base_func = parse_agg_name(fn_tok.text);
        if (!base_func.has_value()) {
            throw QueryError("Unknown aggregate function '" + fn_tok.text + "'");
        }
        if (!consume(TokenType::LParen)) {
            throw QueryError("Expected '(' after aggregate function '" + fn_tok.text + "'");
        }

        AggSpec spec;
        if (peek().type == TokenType::Star) {
            advance();
            if (*base_func != AggFunc::CountCol) {
                throw QueryError(
                    agg_func_name(*base_func) + "(*) is not supported; only COUNT(*) accepts '*'");
            }
            spec.func = AggFunc::CountStar;
            spec.col_name = "*";
            spec.canonical_name = "COUNT(*)";
        } else if (peek().type == TokenType::Ident) {
            const Token col_tok = advance();
            spec.func = *base_func;
            spec.col_idx = resolve_column(col_tok.text, col_tok.pos);
            spec.col_name = table_.headers()[spec.col_idx];
            spec.canonical_name = agg_func_name(spec.func) + "(" + spec.col_name + ")";
        } else {
            throw QueryError(
                "Expected column name or '*' inside " + fn_tok.text + "() at position " +
                std::to_string(peek().pos));
        }

        if (!consume(TokenType::RParen)) {
            throw QueryError("Expected ')' after aggregate argument at position " + std::to_string(peek().pos));
        }
        return spec;
    }

    SelectItem parse_select_item(QueryPlan& plan) {
        SelectItem item;
        if (consume(TokenType::Star)) {
            item.kind = SelectItem::Kind::Star;
            item.output_name = "*";
            return item;
        }
        if (peek().type != TokenType::Ident) {
            throw QueryError("Expected column, '*', or aggregate in SELECT at position " + std::to_string(peek().pos));
        }

        const Token tok = advance();
        if (parse_agg_name(tok.text).has_value() && peek().type == TokenType::LParen) {
            item.kind = SelectItem::Kind::Aggregate;
            item.agg = parse_agg_call(tok);
            item.output_name = item.agg.canonical_name;
            plan.has_aggregates = true;
        } else {
            item.kind = SelectItem::Kind::Column;
            item.col_idx = resolve_column(tok.text, tok.pos);
            item.col_name = table_.headers()[item.col_idx];
            item.output_name = item.col_name;
        }

        if (peek().is_kw("AS")) {
            advance();
            if (peek().type != TokenType::Ident && peek().type != TokenType::String) {
                throw QueryError("Expected alias name after AS at position " + std::to_string(peek().pos));
            }
            const Token alias_tok = advance();
            item.alias = alias_tok.text;
            item.output_name = alias_tok.text;
        }
        return item;
    }

    std::shared_ptr<ExprNode> parse_or(bool allow_agg) {
        auto lhs = parse_and(allow_agg);
        while (peek().is_kw("OR")) {
            advance();
            auto rhs = parse_and(allow_agg);
            auto node = std::make_shared<ExprNode>();
            node->kind = ExprNode::Kind::Or;
            node->left = std::move(lhs);
            node->right = std::move(rhs);
            lhs = std::move(node);
        }
        return lhs;
    }

    std::shared_ptr<ExprNode> parse_and(bool allow_agg) {
        auto lhs = parse_not(allow_agg);
        while (peek().is_kw("AND")) {
            advance();
            auto rhs = parse_not(allow_agg);
            auto node = std::make_shared<ExprNode>();
            node->kind = ExprNode::Kind::And;
            node->left = std::move(lhs);
            node->right = std::move(rhs);
            lhs = std::move(node);
        }
        return lhs;
    }

    std::shared_ptr<ExprNode> parse_not(bool allow_agg) {
        if (peek().is_kw("NOT")) {
            advance();
            auto node = std::make_shared<ExprNode>();
            node->kind = ExprNode::Kind::Not;
            node->left = parse_not(allow_agg);
            return node;
        }
        return parse_primary(allow_agg);
    }

    std::shared_ptr<ExprNode> parse_primary(bool allow_agg) {
        if (consume(TokenType::LParen)) {
            auto inner = parse_or(allow_agg);
            if (!consume(TokenType::RParen)) {
                throw QueryError("Expected closing ')' at position " + std::to_string(peek().pos));
            }
            return inner;
        }

        Operand lhs = parse_operand(allow_agg);

        if (peek().is_kw("IS")) {
            advance();
            bool negated = false;
            if (peek().is_kw("NOT")) {
                negated = true;
                advance();
            }
            expect_kw("NULL", "Expected NULL after IS / IS NOT");
            auto node = std::make_shared<ExprNode>();
            node->kind = ExprNode::Kind::IsNull;
            node->op_lhs = std::move(lhs);
            node->is_not_null = negated;
            return node;
        }

        CmpOp op;
        if (consume(TokenType::Eq)) {
            op = CmpOp::Eq;
        } else if (consume(TokenType::Ne)) {
            op = CmpOp::Ne;
        } else if (consume(TokenType::Lt)) {
            op = CmpOp::Lt;
        } else if (consume(TokenType::Le)) {
            op = CmpOp::Le;
        } else if (consume(TokenType::Gt)) {
            op = CmpOp::Gt;
        } else if (consume(TokenType::Ge)) {
            op = CmpOp::Ge;
        } else if (peek().is_kw("CONTAINS")) {
            advance();
            op = CmpOp::Contains;
        } else {
            throw QueryError(
                "Expected comparison operator (=, !=, <, <=, >, >=, CONTAINS, IS NULL) at position " +
                std::to_string(peek().pos));
        }

        Operand rhs = parse_operand(allow_agg);
        auto node = std::make_shared<ExprNode>();
        node->kind = ExprNode::Kind::Compare;
        node->op_lhs = std::move(lhs);
        node->cmp = op;
        node->op_rhs = std::move(rhs);
        return node;
    }

    Operand parse_operand(bool allow_agg) {
        Operand op;
        if (peek().type == TokenType::Plus || peek().type == TokenType::Minus) {
            const bool neg = (peek().type == TokenType::Minus);
            advance();
            if (peek().type != TokenType::Number) {
                throw QueryError("Expected number after unary sign at position " + std::to_string(peek().pos));
            }
            const Token num = advance();
            op.kind = Operand::Kind::Literal;
            op.literal = Value::number(neg ? -num.number_val : num.number_val);
            return op;
        }
        if (peek().type == TokenType::Number) {
            const Token num = advance();
            op.kind = Operand::Kind::Literal;
            op.literal = Value::number(num.number_val);
            return op;
        }
        if (peek().type == TokenType::String) {
            const Token str = advance();
            op.kind = Operand::Kind::Literal;
            op.literal = Value::text(str.text);
            return op;
        }
        if (peek().is_kw("NULL")) {
            advance();
            op.kind = Operand::Kind::Literal;
            op.literal = Value::null();
            return op;
        }
        if (peek().type == TokenType::Ident) {
            const Token tok = advance();
            if (parse_agg_name(tok.text).has_value() && peek().type == TokenType::LParen) {
                if (!allow_agg) {
                    throw QueryError(
                        "Aggregate function '" + tok.text + "()' is not allowed in WHERE; use HAVING instead");
                }
                op.kind = Operand::Kind::Aggregate;
                op.agg = parse_agg_call(tok);
                return op;
            }
            op.kind = Operand::Kind::Identifier;
            op.ident = tok.text;
            if (!allow_agg) {
                op.resolved_col = resolve_column(tok.text, tok.pos);
                op.col_resolved = true;
            } else {
                // In HAVING, identifier may be a group column or a SELECT alias.
                if (auto idx = table_.column_index(tok.text)) {
                    op.resolved_col = *idx;
                    op.col_resolved = true;
                }
            }
            return op;
        }
        throw QueryError("Expected operand (column, number, or string) at position " + std::to_string(peek().pos));
    }

    OrderByItem parse_order_item() {
        OrderByItem item;
        if (peek().type != TokenType::Ident) {
            throw QueryError("Expected column, alias, or aggregate in ORDER BY at position " + std::to_string(peek().pos));
        }
        const Token tok = advance();
        if (parse_agg_name(tok.text).has_value() && peek().type == TokenType::LParen) {
            item.kind = OrderByItem::Kind::Aggregate;
            item.agg = parse_agg_call(tok);
            item.name = item.agg.canonical_name;
        } else {
            item.kind = OrderByItem::Kind::ColumnOrAlias;
            item.name = tok.text;
        }
        if (peek().is_kw("ASC")) {
            advance();
            item.ascending = true;
        } else if (peek().is_kw("DESC")) {
            advance();
            item.ascending = false;
        }
        return item;
    }

    void validate_plan(const QueryPlan& plan) const {
        const bool is_grouped = !plan.group_by.empty() || plan.has_aggregates;

        if (plan.having_expr && !is_grouped) {
            throw QueryError("HAVING requires GROUP BY or an aggregate function in SELECT");
        }

        if (!is_grouped) {
            return;
        }

        auto is_grouped_col = [&](std::size_t col_idx) {
            for (const auto& gb : plan.group_by) {
                if (gb.second == col_idx) return true;
            }
            return false;
        };

        for (const auto& item : plan.select_items) {
            if (item.kind == SelectItem::Kind::Star) {
                throw QueryError("SELECT * cannot be combined with GROUP BY or aggregate functions");
            }
            if (item.kind == SelectItem::Kind::Column && !is_grouped_col(item.col_idx)) {
                throw QueryError(
                    "Column '" + item.col_name +
                    "' in SELECT must appear in GROUP BY or be wrapped in an aggregate function");
            }
        }

        // Pre-validate HAVING identifiers so errors surface even when 0 groups match
        if (plan.having_expr) {
            validate_grouped_expr_idents(plan.having_expr, plan);
        }
    }

    bool is_valid_grouped_ident(const std::string& name, const QueryPlan& plan) const {
        const std::string low = to_lower_ascii(name);
        for (const auto& item : plan.select_items) {
            if (!item.alias.empty() && to_lower_ascii(item.alias) == low) {
                return true;
            }
        }
        for (const auto& gb : plan.group_by) {
            if (to_lower_ascii(gb.first) == low) {
                return true;
            }
        }
        return false;
    }

    void validate_grouped_expr_idents(
        const std::shared_ptr<ExprNode>& node,
        const QueryPlan& plan) const {
        if (!node) return;
        if (node->kind == ExprNode::Kind::And ||
            node->kind == ExprNode::Kind::Or ||
            node->kind == ExprNode::Kind::Not) {
            validate_grouped_expr_idents(node->left, plan);
            validate_grouped_expr_idents(node->right, plan);
            return;
        }
        auto check_op = [&](const Operand& op) {
            if (op.kind == Operand::Kind::Identifier && !is_valid_grouped_ident(op.ident, plan)) {
                throw QueryError(
                    "Identifier '" + op.ident +
                    "' in HAVING is neither a GROUP BY column nor a SELECT alias");
            }
        };
        check_op(node->op_lhs);
        if (node->kind == ExprNode::Kind::Compare) {
            check_op(node->op_rhs);
        }
    }
};

inline Value compute_aggregate(
    const Table& table,
    const std::vector<std::size_t>& row_indices,
    const AggSpec& spec) {

    if (spec.func == AggFunc::CountStar) {
        return Value::number(static_cast<double>(row_indices.size()));
    }

    if (spec.func == AggFunc::CountCol) {
        std::size_t cnt = 0;
        for (std::size_t r : row_indices) {
            if (!table.cell(r, spec.col_idx).is_null()) {
                ++cnt;
            }
        }
        return Value::number(static_cast<double>(cnt));
    }

    if (spec.func == AggFunc::Sum || spec.func == AggFunc::Avg) {
        double sum = 0.0;
        std::size_t cnt = 0;
        for (std::size_t r : row_indices) {
            const Value& v = table.cell(r, spec.col_idx);
            if (v.is_null()) continue;
            if (!v.is_number()) {
                throw QueryError(
                    agg_func_name(spec.func) + "(" + spec.col_name +
                    ") encountered non-numeric value '" + v.to_string() + "'");
            }
            sum += v.as_number();
            ++cnt;
        }
        if (cnt == 0) {
            return Value::null();
        }
        return Value::number(spec.func == AggFunc::Sum ? sum : (sum / static_cast<double>(cnt)));
    }

    // Min / Max (works across numbers or strings)
    const Value* best = nullptr;
    for (std::size_t r : row_indices) {
        const Value& v = table.cell(r, spec.col_idx);
        if (v.is_null()) continue;
        if (!best) {
            best = &v;
        } else {
            const int cmp = compare_values(v, *best);
            if ((spec.func == AggFunc::Min && cmp < 0) ||
                (spec.func == AggFunc::Max && cmp > 0)) {
                best = &v;
            }
        }
    }
    return best ? *best : Value::null();
}

inline void collect_expr_aggs(
    const std::shared_ptr<ExprNode>& node,
    std::vector<AggSpec>& out) {
    if (!node) return;
    if (node->kind == ExprNode::Kind::And || node->kind == ExprNode::Kind::Or ||
        node->kind == ExprNode::Kind::Not) {
        collect_expr_aggs(node->left, out);
        collect_expr_aggs(node->right, out);
        return;
    }
    if (node->op_lhs.kind == Operand::Kind::Aggregate) {
        out.push_back(node->op_lhs.agg);
    }
    if (node->op_rhs.kind == Operand::Kind::Aggregate) {
        out.push_back(node->op_rhs.agg);
    }
}

template <typename ResolverFn>
inline Value eval_operand(const Operand& op, ResolverFn&& resolve_ident,
                          const std::unordered_map<std::string, Value>* agg_values) {
    switch (op.kind) {
        case Operand::Kind::Literal:
            return op.literal;
        case Operand::Kind::Identifier:
            return resolve_ident(op);
        case Operand::Kind::Aggregate: {
            if (!agg_values) {
                throw QueryError("Aggregate referenced outside grouped context");
            }
            auto it = agg_values->find(op.agg.key());
            if (it == agg_values->end()) {
                return Value::null();
            }
            return it->second;
        }
    }
    return Value::null();
}

template <typename ResolverFn>
inline bool eval_expr(const std::shared_ptr<ExprNode>& node, ResolverFn&& resolve_ident,
                      const std::unordered_map<std::string, Value>* agg_values = nullptr) {
    if (!node) return true;
    switch (node->kind) {
        case ExprNode::Kind::And:
            return eval_expr(node->left, resolve_ident, agg_values) &&
                   eval_expr(node->right, resolve_ident, agg_values);
        case ExprNode::Kind::Or:
            return eval_expr(node->left, resolve_ident, agg_values) ||
                   eval_expr(node->right, resolve_ident, agg_values);
        case ExprNode::Kind::Not:
            return !eval_expr(node->left, resolve_ident, agg_values);
        case ExprNode::Kind::IsNull: {
            const Value v = eval_operand(node->op_lhs, resolve_ident, agg_values);
            return node->is_not_null ? !v.is_null() : v.is_null();
        }
        case ExprNode::Kind::Compare: {
            const Value lhs = eval_operand(node->op_lhs, resolve_ident, agg_values);
            const Value rhs = eval_operand(node->op_rhs, resolve_ident, agg_values);
            if (node->cmp == CmpOp::Contains) {
                if (lhs.is_null() || rhs.is_null()) return false;
                const std::string hay = to_lower_ascii(lhs.is_string() ? lhs.as_string() : lhs.to_string());
                const std::string needle = to_lower_ascii(rhs.is_string() ? rhs.as_string() : rhs.to_string());
                return hay.find(needle) != std::string::npos;
            }
            auto cmp = compare_comparable(lhs, rhs);
            if (!cmp.has_value()) {
                return false;
            }
            switch (node->cmp) {
                case CmpOp::Eq: return *cmp == 0;
                case CmpOp::Ne: return *cmp != 0;
                case CmpOp::Lt: return *cmp < 0;
                case CmpOp::Le: return *cmp <= 0;
                case CmpOp::Gt: return *cmp > 0;
                case CmpOp::Ge: return *cmp >= 0;
                case CmpOp::Contains: return false;
            }
        }
    }
    return false;
}

inline int compare_for_sort(const Value& a, const Value& b, bool ascending) {
    if (a.is_null() && b.is_null()) return 0;
    if (a.is_null()) return 1;   // NULLs always placed last
    if (b.is_null()) return -1;
    const int cmp = compare_values(a, b);
    return ascending ? cmp : -cmp;
}

inline std::string encode_group_key(const Table& table, std::size_t row,
                                    const std::vector<std::pair<std::string, std::size_t>>& group_by) {
    std::string key;
    for (const auto& gb : group_by) {
        const Value& v = table.cell(row, gb.second);
        if (v.is_null()) {
            key += "N;";
        } else if (v.is_number()) {
            key += "D:" + format_number(v.as_number(), 9, false) + ";";
        } else {
            const std::string& s = v.as_string();
            key += "S" + std::to_string(s.size()) + ":" + s + ";";
        }
    }
    return key;
}

}  // namespace detail

/// Executes a SQL-like query string against a Table and returns a new result Table.
inline Table query(const Table& table, const std::string& sql) {
    if (table.col_count() == 0) {
        throw QueryError("Cannot query an empty table with no columns");
    }
    std::vector<detail::Token> tokens = detail::tokenize_sql(sql);
    detail::SqlParser parser(table, std::move(tokens));
    detail::QueryPlan plan = parser.parse();

    // Step 1: WHERE filter against source rows.
    std::vector<std::size_t> filtered_rows;
    filtered_rows.reserve(table.row_count());
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        auto resolve_row = [&](const detail::Operand& op) -> Value {
            return table.cell(r, op.resolved_col);
        };
        if (detail::eval_expr(plan.where_expr, resolve_row, nullptr)) {
            filtered_rows.push_back(r);
        }
    }

    const bool is_grouped = !plan.group_by.empty() || plan.has_aggregates;

    // Non-aggregated path: sort indices -> limit -> project.
    if (!is_grouped) {
        // Resolve ORDER BY targets against aliases or source columns.
        std::vector<std::size_t> order_cols;
        order_cols.reserve(plan.order_by.size());
        for (const auto& ob : plan.order_by) {
            if (ob.kind == detail::OrderByItem::Kind::Aggregate) {
                throw QueryError("Aggregate '" + ob.name + "' in ORDER BY requires GROUP BY or aggregate SELECT");
            }
            std::optional<std::size_t> target_col;
            for (const auto& item : plan.select_items) {
                if (item.kind == detail::SelectItem::Kind::Column &&
                    !item.alias.empty() &&
                    detail::to_lower_ascii(item.alias) == detail::to_lower_ascii(ob.name)) {
                    target_col = item.col_idx;
                    break;
                }
            }
            if (!target_col.has_value()) {
                target_col = table.column_index(ob.name);
            }
            if (!target_col.has_value()) {
                throw QueryError("Unknown column or alias '" + ob.name + "' in ORDER BY");
            }
            order_cols.push_back(*target_col);
        }

        if (!order_cols.empty()) {
            std::stable_sort(
                filtered_rows.begin(), filtered_rows.end(),
                [&](std::size_t ra, std::size_t rb) {
                    for (std::size_t k = 0; k < order_cols.size(); ++k) {
                        const int cmp = detail::compare_for_sort(
                            table.cell(ra, order_cols[k]),
                            table.cell(rb, order_cols[k]),
                            plan.order_by[k].ascending);
                        if (cmp != 0) return cmp < 0;
                    }
                    return false;
                });
        }

        if (plan.limit.has_value() && filtered_rows.size() > *plan.limit) {
            filtered_rows.resize(*plan.limit);
        }

        // Expand SELECT * and build output headers.
        std::vector<std::string> out_headers;
        std::vector<std::size_t> proj_cols;
        for (const auto& item : plan.select_items) {
            if (item.kind == detail::SelectItem::Kind::Star) {
                for (std::size_t c = 0; c < table.col_count(); ++c) {
                    out_headers.push_back(table.headers()[c]);
                    proj_cols.push_back(c);
                }
            } else {
                out_headers.push_back(item.output_name);
                proj_cols.push_back(item.col_idx);
            }
        }

        Table result(std::move(out_headers));
        for (std::size_t r : filtered_rows) {
            std::vector<Value> row;
            row.reserve(proj_cols.size());
            for (std::size_t c : proj_cols) {
                row.push_back(table.cell(r, c));
            }
            result.add_row(std::move(row));
        }
        return result;
    }

    // Grouped / Aggregated path:
    struct GroupBucket {
        std::unordered_map<std::size_t, Value> group_values;
        std::vector<std::size_t> row_indices;
        std::unordered_map<std::string, Value> agg_values;
    };

    std::vector<GroupBucket> buckets;
    if (plan.group_by.empty()) {
        // Global aggregation always produces exactly 1 bucket (even if 0 filtered rows).
        GroupBucket single;
        single.row_indices = std::move(filtered_rows);
        buckets.push_back(std::move(single));
    } else {
        std::unordered_map<std::string, std::size_t> key_to_bucket;
        for (std::size_t r : filtered_rows) {
            const std::string gkey = detail::encode_group_key(table, r, plan.group_by);
            auto it = key_to_bucket.find(gkey);
            if (it == key_to_bucket.end()) {
                std::size_t idx = buckets.size();
                key_to_bucket[gkey] = idx;
                GroupBucket bucket;
                for (const auto& gb : plan.group_by) {
                    bucket.group_values[gb.second] = table.cell(r, gb.second);
                }
                bucket.row_indices.push_back(r);
                buckets.push_back(std::move(bucket));
            } else {
                buckets[it->second].row_indices.push_back(r);
            }
        }
    }

    // Collect every unique aggregate referenced in SELECT, HAVING, and ORDER BY.
    std::vector<detail::AggSpec> needed_aggs;
    for (const auto& item : plan.select_items) {
        if (item.kind == detail::SelectItem::Kind::Aggregate) {
            needed_aggs.push_back(item.agg);
        }
    }
    detail::collect_expr_aggs(plan.having_expr, needed_aggs);
    for (const auto& ob : plan.order_by) {
        if (ob.kind == detail::OrderByItem::Kind::Aggregate) {
            needed_aggs.push_back(ob.agg);
        }
    }

    std::unordered_map<std::string, detail::AggSpec> unique_aggs;
    for (const auto& spec : needed_aggs) {
        unique_aggs.emplace(spec.key(), spec);
    }

    for (auto& bucket : buckets) {
        for (const auto& kv : unique_aggs) {
            bucket.agg_values[kv.first] =
                detail::compute_aggregate(table, bucket.row_indices, kv.second);
        }
    }

    // Map SELECT aliases (and canonical headers) for HAVING / ORDER BY resolution.
    auto resolve_bucket_ident = [&](const GroupBucket& bucket, const std::string& name) -> Value {
        const std::string low = detail::to_lower_ascii(name);
        // 1. Check SELECT aliases first
        for (const auto& item : plan.select_items) {
            if (!item.alias.empty() && detail::to_lower_ascii(item.alias) == low) {
                if (item.kind == detail::SelectItem::Kind::Aggregate) {
                    return bucket.agg_values.at(item.agg.key());
                }
                return bucket.group_values.at(item.col_idx);
            }
        }
        // 2. Check GROUP BY columns
        for (const auto& gb : plan.group_by) {
            if (detail::to_lower_ascii(gb.first) == low) {
                return bucket.group_values.at(gb.second);
            }
        }
        throw QueryError(
            "Identifier '" + name + "' is neither a GROUP BY column nor a SELECT alias");
    };

    // Validate & apply HAVING
    std::vector<std::size_t> passing_buckets;
    for (std::size_t b = 0; b < buckets.size(); ++b) {
        auto resolve_having = [&](const detail::Operand& op) -> Value {
            return resolve_bucket_ident(buckets[b], op.ident);
        };
        if (detail::eval_expr(plan.having_expr, resolve_having, &buckets[b].agg_values)) {
            passing_buckets.push_back(b);
        }
    }

    // Validate and sort ORDER BY (pre-validate keys even if 0 or 1 groups match)
    if (!plan.order_by.empty()) {
        auto is_known_group_ident = [&](const std::string& name) {
            const std::string low = detail::to_lower_ascii(name);
            for (const auto& item : plan.select_items) {
                if (!item.alias.empty() && detail::to_lower_ascii(item.alias) == low) {
                    return true;
                }
            }
            for (const auto& gb : plan.group_by) {
                if (detail::to_lower_ascii(gb.first) == low) {
                    return true;
                }
            }
            return false;
        };
        for (const auto& ob : plan.order_by) {
            if (ob.kind == detail::OrderByItem::Kind::ColumnOrAlias &&
                !is_known_group_ident(ob.name)) {
                throw QueryError(
                    "Identifier '" + ob.name +
                    "' in ORDER BY is neither a GROUP BY column nor a SELECT alias");
            }
        }

        auto get_order_val = [&](const GroupBucket& bucket, const detail::OrderByItem& ob) -> Value {
            if (ob.kind == detail::OrderByItem::Kind::Aggregate) {
                return bucket.agg_values.at(ob.agg.key());
            }
            return resolve_bucket_ident(bucket, ob.name);
        };

        std::stable_sort(
            passing_buckets.begin(), passing_buckets.end(),
            [&](std::size_t ba, std::size_t bb) {
                for (const auto& ob : plan.order_by) {
                    const Value va = get_order_val(buckets[ba], ob);
                    const Value vb = get_order_val(buckets[bb], ob);
                    const int cmp = detail::compare_for_sort(va, vb, ob.ascending);
                    if (cmp != 0) return cmp < 0;
                }
                return false;
            });
    }

    if (plan.limit.has_value() && passing_buckets.size() > *plan.limit) {
        passing_buckets.resize(*plan.limit);
    }

    std::vector<std::string> out_headers;
    out_headers.reserve(plan.select_items.size());
    for (const auto& item : plan.select_items) {
        out_headers.push_back(item.output_name);
    }

    Table result(std::move(out_headers));
    for (std::size_t b : passing_buckets) {
        const auto& bucket = buckets[b];
        std::vector<Value> row;
        row.reserve(plan.select_items.size());
        for (const auto& item : plan.select_items) {
            if (item.kind == detail::SelectItem::Kind::Column) {
                row.push_back(bucket.group_values.at(item.col_idx));
            } else if (item.kind == detail::SelectItem::Kind::Aggregate) {
                row.push_back(bucket.agg_values.at(item.agg.key()));
            }
        }
        result.add_row(std::move(row));
    }
    return result;
}

}  // namespace csvql

$ErrorActionPreference = "Stop"

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Error "g++ not found in PATH. Please install MSYS2/MinGW-w64 or GCC."
    exit 1
}

$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force $BuildDir | Out-Null

$Flags = @("-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I$PSScriptRoot\include")

$Targets = @(
    @{ Src = "tests\test_parser_and_query.cpp"; Out = "build\test_parser_and_query.exe" },
    @{ Src = "tests\test_visualizer.cpp";       Out = "build\test_visualizer.exe" },
    @{ Src = "examples\basic_usage.cpp";        Out = "build\basic_usage.exe" },
    @{ Src = "examples\demo_cli.cpp";           Out = "build\csvql_demo.exe" }
)

foreach ($t in $Targets) {
    $srcPath = Join-Path $PSScriptRoot $t.Src
    $outPath = Join-Path $PSScriptRoot $t.Out
    Write-Host "[BUILD] $($t.Src) -> $($t.Out)" -ForegroundColor Cyan
    & g++ @Flags $srcPath -o $outPath
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed for $($t.Src)"
    }
}

Write-Host "`n[TEST] Running test_parser_and_query.exe" -ForegroundColor Cyan
& (Join-Path $BuildDir "test_parser_and_query.exe")
if ($LASTEXITCODE -ne 0) { throw "test_parser_and_query failed" }

Write-Host "`n[TEST] Running test_visualizer.exe" -ForegroundColor Cyan
& (Join-Path $BuildDir "test_visualizer.exe")
if ($LASTEXITCODE -ne 0) { throw "test_visualizer failed" }

Write-Host "`n[SMOKE] Running basic_usage.exe" -ForegroundColor Cyan
& (Join-Path $BuildDir "basic_usage.exe")
if ($LASTEXITCODE -ne 0) { throw "basic_usage failed" }

Write-Host "`n[SMOKE] Running csvql_demo.exe --demo --ascii --no-color" -ForegroundColor Cyan
& (Join-Path $BuildDir "csvql_demo.exe") --demo --ascii --no-color
if ($LASTEXITCODE -ne 0) { throw "csvql_demo smoke test failed" }

Write-Host "`n[OK] All builds, unit tests, and smoke checks passed." -ForegroundColor Green

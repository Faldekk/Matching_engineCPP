@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /DNDEBUG /Zi /I Matching_engineCPP benchmarks\Benchmark.cpp Matching_engineCPP\OrderBook.cpp /Fo:build\ /Fe:build\Benchmark.exe /Fd:build\Benchmark.pdb /link /DEBUG
if errorlevel 1 (
    popd
    exit /b 1
)
if "%~1"=="" (
    build\Benchmark.exe
) else (
    build\Benchmark.exe > "%~1"
)
set result=%errorlevel%
popd
exit /b %result%

@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this script from the Visual Studio Developer Command Prompt.
    exit /b 1
)
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /WX /Zi /Od /MDd /I Matching_engineCPP tests\OrderBookTests.cpp Matching_engineCPP\OrderBook.cpp /Fo:build\ /Fe:build\OrderBookTests.exe /Fd:build\OrderBookTests.pdb
if errorlevel 1 (
    popd
    exit /b 1
)
build\OrderBookTests.exe
if errorlevel 1 (
    popd
    exit /b 1
)
cl /nologo /std:c++20 /EHsc /W4 /WX /Zi /Od /MDd /I Matching_engineCPP tests\EngineFeaturesTests.cpp Matching_engineCPP\OrderBook.cpp Matching_engineCPP\EngineCommand.cpp Matching_engineCPP\EngineWorker.cpp /Fo:build\ /Fe:build\EngineFeaturesTests.exe /Fd:build\EngineFeaturesTests.pdb
if errorlevel 1 (
    popd
    exit /b 1
)
build\EngineFeaturesTests.exe
set result=%errorlevel%
popd
exit /b %result%

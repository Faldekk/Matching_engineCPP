@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /WX /Zi /Od /MDd Matching_engineCPP\main.cpp Matching_engineCPP\OrderBook.cpp Matching_engineCPP\EngineCommand.cpp Matching_engineCPP\EngineWorker.cpp /Fo:build\ /Fe:build\MatchingEngineDemo.exe /Fd:build\Demo.pdb
set result=%errorlevel%
popd
exit /b %result%

@echo off
setlocal EnableDelayedExpansion
pushd "%~dp0"
rem Pierwszy start pobiera sprawdzone archiwa Crow i Asio. Kolejne budowy dzialaja offline.
powershell -NoProfile -ExecutionPolicy Bypass -File setup-server.ps1
if errorlevel 1 goto failed
where cl >nul 2>nul
if errorlevel 1 (
    set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!vswhere!" goto failed
    for /f "usebackq delims=" %%i in (`"!vswhere!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vsdir=%%i"
    if not defined vsdir goto failed
    call "!vsdir!\VC\Auxiliary\Build\vcvars64.bat"
    if errorlevel 1 goto failed
)
if not exist build mkdir build
if not exist build\server-obj mkdir build\server-obj
rem Crow korzysta z Asio do HTTP. WinHTTP dalej odbiera dane Binance przez TLS.
set "includes=/I Matching_engineCPP /external:I .deps\Crow-1.2.1\include /external:I .deps\asio-asio-1-30-2\asio\include /external:W0"
set "sources=Matching_engineCPP\ServerApi.cpp Matching_engineCPP\LivePaper.cpp Matching_engineCPP\PaperTrading.cpp Matching_engineCPP\BinanceFeed.cpp Matching_engineCPP\MarketData.cpp"
if not defined MATCHING_SERVER_OUTPUT set "MATCHING_SERVER_OUTPUT=build\MatchingEngineServer.exe"
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /D_WIN32_WINNT=0x0A00 /DASIO_STANDALONE %includes% Matching_engineCPP\ServerMain.cpp %sources% /Fo:build\server-obj\ /Fe:%MATCHING_SERVER_OUTPUT% /link ws2_32.lib mswsock.lib
if errorlevel 1 goto failed
if /I "%~1"=="test" (
    cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /D_WIN32_WINNT=0x0A00 /DASIO_STANDALONE %includes% tests\ServerApiTests.cpp %sources% /Fo:build\server-obj\ /Fe:build\ServerApiTests.exe /link ws2_32.lib mswsock.lib
    if errorlevel 1 goto failed
    build\ServerApiTests.exe
    if errorlevel 1 goto failed
)
popd
exit /b 0
:failed
echo Blad budowania serwera. Wymagane Visual Studio z Desktop development with C++ i dostep do sieci przy pierwszym pobraniu.
popd
exit /b 1

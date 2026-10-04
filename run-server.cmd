@echo off
setlocal
pushd "%~dp0"
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $destination='build\MatchingEngineServer.exe'; foreach ($candidate in @('build\MatchingEngineServer-next.exe','build\MatchingEngineServer-rules.exe')) { if ((Test-Path -LiteralPath $candidate) -and (!(Test-Path -LiteralPath $destination) -or (Get-Item -LiteralPath $candidate).LastWriteTimeUtc -gt (Get-Item -LiteralPath $destination).LastWriteTimeUtc)) { Copy-Item -LiteralPath $candidate -Destination $destination -Force } }"
if errorlevel 1 (
    echo Najpierw zakoncz poprzednia sesje serwera.
    popd
    exit /b 1
)
if not exist build\MatchingEngineServer.exe (
    call build-server.cmd
    if errorlevel 1 (
        popd
        exit /b 1
    )
)
build\MatchingEngineServer.exe %*
set result=%errorlevel%
popd
exit /b %result%

@echo off
setlocal
pushd "%~dp0frontend"
where npm >nul 2>nul
if errorlevel 1 (
    echo Wymagany Node.js 22.12+ lub 24. Uruchom tez run-server.cmd w drugim terminalu.
    popd
    exit /b 1
)
if not exist node_modules\lightweight-charts (
    call npm ci
    if errorlevel 1 (
        popd
        exit /b 1
    )
)
echo Interfejs: http://127.0.0.1:5173. API wymaga osobno uruchomionego run-server.cmd.
call npm run dev
set result=%errorlevel%
popd
exit /b %result%

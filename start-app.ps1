param([switch]$NoBrowser, [switch]$CheckOnly)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$serverProcess = $null
$uiProcess = $null

function Test-Server {
    try {
        $state = Invoke-RestMethod 'http://127.0.0.1:18080/api/state' -TimeoutSec 2
        return $null -ne $state.wallet -and $null -ne $state.market
    } catch { return $false }
}

function Test-Ui {
    try {
        $page = Invoke-WebRequest 'http://127.0.0.1:5173/' -UseBasicParsing -TimeoutSec 2
        return $page.Content.Contains('Exchange Lab') -and $page.Content.Contains('/@vite/client')
    } catch { return $false }
}

function Test-Port($port) {
    return $null -ne (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue)
}

try {
    Set-Location -LiteralPath $projectRoot
    Write-Host 'Exchange Lab - uruchamianie aplikacji...'
    if (!(Test-Server)) {
        if (Test-Port 18080) { throw 'Port 18080 jest zajety przez inna aplikacje.' }
        foreach ($pendingName in @('MatchingEngineServer-next.exe', 'MatchingEngineServer-rules.exe')) {
            $pendingPath = Join-Path "$projectRoot\build" $pendingName
            $serverPath = "$projectRoot\build\MatchingEngineServer.exe"
            if ((Test-Path -LiteralPath $pendingPath) -and (!(Test-Path -LiteralPath $serverPath) -or (Get-Item -LiteralPath $pendingPath).LastWriteTimeUtc -gt (Get-Item -LiteralPath $serverPath).LastWriteTimeUtc)) {
                Copy-Item -LiteralPath $pendingPath -Destination $serverPath -Force
            }
        }
        if (!(Test-Path -LiteralPath "$projectRoot\build\MatchingEngineServer.exe")) {
            & "$projectRoot\build-server.cmd"
            if ($LASTEXITCODE -ne 0) { throw 'Nie udalo sie zbudowac serwera C++.' }
        }
        $serverProcess = Start-Process -FilePath "$projectRoot\build\MatchingEngineServer.exe" -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    }

    if (!$serverProcess) {
        $version = Invoke-RestMethod 'http://127.0.0.1:18080/api/state' -TimeoutSec 2
        if ($null -eq $version.wallet.positions -or $null -eq $version.conditionalOrders) {
            throw 'Dziala starszy serwer. Zakoncz poprzednia sesje i ponownie uruchom Uruchom.cmd. Restart zeruje niezapisany portfel.'
        }
    }

    if (!(Test-Ui)) {
        if (Test-Port 5173) { throw 'Port 5173 jest zajety przez inna aplikacje.' }
        $node = Get-Command node.exe -ErrorAction SilentlyContinue
        $npm = Get-Command npm.cmd -ErrorAction SilentlyContinue
        if (!$node -or !$npm) { throw 'Zainstaluj Node.js 22.12+ lub 24, potem uruchom ten plik ponownie.' }
        Set-Location -LiteralPath "$projectRoot\frontend"
        if (!(Test-Path 'node_modules\vite') -or !(Test-Path 'node_modules\lightweight-charts')) {
            & $npm.Source ci
            if ($LASTEXITCODE -ne 0) { throw 'Nie udalo sie pobrac zaleznosci interfejsu. Sprawdz internet.' }
        }
        $uiProcess = Start-Process -FilePath $node.Source -ArgumentList 'node_modules/vite/bin/vite.js' -WorkingDirectory "$projectRoot\frontend" -WindowStyle Hidden -PassThru
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Server) -or !(Test-Ui)) {
        if ($serverProcess -and $serverProcess.HasExited) { throw 'Serwer zakonczyl dzialanie. Uruchom run-server.cmd, aby zobaczyc blad.' }
        if ($uiProcess -and $uiProcess.HasExited) { throw 'Interfejs zakonczyl dzialanie. Uruchom run-ui.cmd, aby zobaczyc blad.' }
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Aplikacja nie uruchomila sie w ciagu 30 sekund.' }
        Start-Sleep -Milliseconds 300
    }
    Write-Host 'Gotowe: http://127.0.0.1:5173' -ForegroundColor Green
    if (!$NoBrowser) { Start-Process 'http://127.0.0.1:5173/' -WindowStyle Hidden }
    if (!$CheckOnly) {
        Write-Host 'Zostaw to okno otwarte podczas gry.'
        Write-Host 'ENTER zatrzyma procesy uruchomione przez to okno. Portfel serwera zostanie wtedy utracony.'
        Read-Host 'Nacisnij ENTER, aby zakonczyc' | Out-Null
    }
} catch {
    Write-Host "Blad: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    # Nie zatrzymujemy serwera juz uruchomionego przez uzytkownika: zachowuje portfel.
    if ($uiProcess -and !$uiProcess.HasExited) { $uiProcess.Kill(); $uiProcess.WaitForExit() }
    if ($serverProcess -and !$serverProcess.HasExited) { $serverProcess.Kill(); $serverProcess.WaitForExit() }
}

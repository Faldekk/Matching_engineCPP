# Test rzeczywistego HTTP i wirtualnego handlu na danych Binance.
# Wymaga gotowego build/MatchingEngineServer.exe i dostepu do sieci.
$ErrorActionPreference = 'Stop'
$serverPath = Join-Path $PSScriptRoot 'build/MatchingEngineServer.exe'
$server = Start-Process -FilePath $serverPath -WindowStyle Hidden -PassThru -WorkingDirectory $PSScriptRoot -RedirectStandardOutput (Join-Path $PSScriptRoot 'build/server-test.log') -RedirectStandardError (Join-Path $PSScriptRoot 'build/server-test-error.log')
try {
    $api = 'http://127.0.0.1:18080/api'
    $ready = $false
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        if ($server.HasExited) { throw 'Serwer zakonczyl sie przed testem' }
        try {
            $state = Invoke-RestMethod -Uri ($api + '/state') -TimeoutSec 2
            if ($state.market.ready) { $ready = $true; break }
        }
        catch { }
        Start-Sleep -Milliseconds 1000
    }
    if (!$ready) { throw 'Brak swiezych danych Binance podczas testu HTTP' }
    $buy = Invoke-RestMethod -Uri ($api + '/orders') -Method Post -ContentType 'application/json' -Body '{"side":"BUY","quantity":0.001}'
    $sell = Invoke-RestMethod -Uri ($api + '/orders') -Method Post -ContentType 'application/json' -Body '{"side":"SELL","quantity":0.0005}'
    $final = Invoke-RestMethod -Uri ($api + '/state')
    if (!$buy.ok -or !$sell.ok -or [Math]::Abs($buy.filled - 0.001) -gt 1e-10 -or [Math]::Abs($sell.filled - 0.0005) -gt 1e-10) {
        throw 'Niepoprawne wykonania API'
    }
    if ([Math]::Abs($final.wallet.btc - 0.0005) -gt 1e-10 -or $final.fills.Count -lt 2) {
        throw 'Niepoprawny stan portfela API'
    }
    Write-Host ('HTTP live OK: BUY @ ' + $buy.averagePrice + ', SELL @ ' + $sell.averagePrice + ', BTC: ' + $final.wallet.btc)
}
finally {
    if (!$server.HasExited) { Stop-Process -Id $server.Id -Force }
    $server.Dispose()
}

# Crow i Asio sa bibliotekami naglowkowymi. Nie instalujemy ich globalnie.
# Wersje: Crow 1.2.1 (BSD-3-Clause), standalone Asio 1.30.2 (Boost Software License).
param()
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$dependencyRoot = Join-Path $PSScriptRoot '.deps'
New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null
$packages = @(
    @{ Name='crow'; Url='https://github.com/CrowCpp/Crow/archive/refs/tags/v1.2.1.zip'; Hash='B3A26C3E402E472002439B9A0CD8AC6DD62D2DC3B1978B5A9A7FE48967053209'; Header='Crow-1.2.1/include/crow.h' },
    @{ Name='asio'; Url='https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.zip'; Hash='948373ED2E838EE1181937380C754E8FE6DA8B18022EAE922BCDC0CD38D6DBDE'; Header='asio-asio-1-30-2/asio/include/asio.hpp' }
)
foreach ($package in $packages) {
    $archivePath = Join-Path $dependencyRoot ($package.Name + '.zip')
    if (!(Test-Path -LiteralPath $archivePath)) {
        Write-Host ('Pobieranie: ' + $package.Name)
        Invoke-WebRequest -UseBasicParsing -Uri $package.Url -OutFile $archivePath
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    $archiveStream = [IO.File]::OpenRead($archivePath)
    try { $actualHash = [BitConverter]::ToString($sha256.ComputeHash($archiveStream)).Replace('-', '') }
    finally { $archiveStream.Dispose(); $sha256.Dispose() }
    if ($actualHash -ne $package.Hash) {
        throw ('Niepoprawna suma SHA256: ' + $archivePath)
    }
    if (!(Test-Path -LiteralPath (Join-Path $dependencyRoot $package.Header))) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $dependencyRoot)
    }
}
Write-Host 'Crow i Asio gotowe w .deps.'

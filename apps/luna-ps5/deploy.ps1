param (
    [string]$ConsoleIp = "192.168.18.21",
    [int]$ElfPort = 9021
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host "   Despliegue de Luna-PS5 a la consola              " -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan

$elfPath = "$here\luna-ps5.elf"

if (-not (Test-Path $elfPath)) {
    Write-Error "El archivo $elfPath no existe. Ejecuta primero build_ps5.ps1"
}

Write-Host "Enviando $elfPath a la consola ($ConsoleIp`:$ElfPort)..."

try {
    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect($ConsoleIp, $ElfPort)
    $stream = $client.GetStream()
    $bytes = [System.IO.File]::ReadAllBytes($elfPath)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
    $client.Close()
    Write-Host "Payload $elfPath enviado correctamente con exito!" -ForegroundColor Green
}
catch {
    Write-Warning "No se pudo conectar a $ConsoleIp`:$ElfPort ($($_.Exception.Message))"
    Write-Host "Nota: Tambien puedes copiar la carpeta 'dist\LUNA00001' via FTP al directorio '/data/homebrew/' de tu PS5." -ForegroundColor Yellow
}

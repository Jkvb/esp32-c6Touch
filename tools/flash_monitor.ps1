param(
    [string]$Port = "COM6"
)

$ErrorActionPreference = "Stop"

$idfExport = "C:\esp\esp-idf\export.ps1"
if (-not (Test-Path $idfExport)) {
    throw "No se encontró $idfExport. Instala/configura ESP-IDF primero."
}

Write-Host "Activando entorno ESP-IDF..." -ForegroundColor Cyan
& $idfExport

Write-Host "Verificando idf.py..." -ForegroundColor Cyan
&idf.py --version

Write-Host "Flasheando + monitor en $Port..." -ForegroundColor Cyan
&idf.py -p $Port flash monitor

param(
    [string]$Port = "COM6",
    [string]$Branch = "main",
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"

Write-Host "[1/7] Activando entorno ESP-IDF..." -ForegroundColor Cyan
& "C:\esp\esp-idf\export.ps1"

Write-Host "[2/7] Entrando al repo..." -ForegroundColor Cyan
Set-Location "C:\Users\jcarl\esp32\esp32-c6Touch"

Write-Host "[3/7] Sincronizando repo..." -ForegroundColor Cyan
git fetch --all --prune
git checkout $Branch
git pull --rebase origin $Branch

if (-not $NoClean) {
    Write-Host "[4/7] Limpiando build..." -ForegroundColor Cyan
    idf.py fullclean
} else {
    Write-Host "[4/7] Omitiendo fullclean (NoClean)." -ForegroundColor Yellow
}

Write-Host "[5/7] Reconfigurando..." -ForegroundColor Cyan
idf.py reconfigure

Write-Host "[6/7] Flasheando a $Port ..." -ForegroundColor Cyan
idf.py -p $Port flash

Write-Host "[7/7] Abriendo monitor..." -ForegroundColor Cyan
idf.py -p $Port monitor

param(
    [string]$Port = "COM6",
    [string]$Branch = "main",
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "tools\update_and_flash.ps1"
if (-not (Test-Path $scriptPath)) {
    throw "No se encontró $scriptPath. Actualiza repo con: git fetch --all --prune; git checkout main; git pull --rebase origin main"
}

& $scriptPath -Port $Port -Branch $Branch -NoClean:$NoClean

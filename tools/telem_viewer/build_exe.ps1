# Build Windows distribution for 折叠翼遥测查看器
# Usage (from tools/telem_viewer):
#   powershell -ExecutionPolicy Bypass -File .\build_exe.ps1

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "==> install build deps"
python -m pip install -q -r requirements.txt pyinstaller

Write-Host "==> clean old build"
Remove-Item -Recurse -Force .\build, .\dist -ErrorAction SilentlyContinue

Write-Host "==> pyinstaller"
python -m PyInstaller --noconfirm --clean .\telem_viewer.spec

$distDir = Join-Path $PSScriptRoot "dist\折叠翼遥测查看器"
if (-not (Test-Path $distDir)) {
    throw "Build output missing: $distDir"
}

$zipPath = Join-Path $PSScriptRoot "dist\折叠翼遥测查看器.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path $distDir -DestinationPath $zipPath -Force

Write-Host ""
Write-Host "OK"
Write-Host "Folder: $distDir"
Write-Host "Zip:    $zipPath"
Write-Host "Send the zip; on the other PC unzip and run 折叠翼遥测查看器.exe"

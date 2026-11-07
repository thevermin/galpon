# Compila Galpon en Windows (Visual Studio 2022 o sus Build Tools, con C++, y CMake).
#   .\scripts\build_windows.ps1            compila, corre las pruebas
#   .\scripts\build_windows.ps1 -NoTests   solo compila
param([switch]$NoTests)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "Falta CMake. Instálalo con:  winget install Kitware.CMake  (y abre otra ventana de PowerShell)"
    exit 1
}

New-Item -ItemType Directory -Force build | Out-Null
$log = "build\ultimo-build.log"
Write-Host "Configurando (la primera vez descarga JUCE y las demás dependencias)..."
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { Write-Host "Falló la configuración. Mira $log"; exit 1 }

Write-Host "Compilando..."
cmake --build build --config Release --parallel 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { Write-Host "Falló la compilación. Mira $log"; exit 1 }

if (-not $NoTests) {
    Write-Host "Corriendo las pruebas del motor..."
    & "build\GalponTests_artefacts\Release\GalponTests.exe"
    if ($LASTEXITCODE -ne 0) { Write-Host "Alguna prueba falló."; exit 1 }
}
Write-Host ""
Write-Host "Listo: build\Galpon_artefacts\Release\Galpon.exe"

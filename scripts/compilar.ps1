# Compila OCRX en Windows (MSVC 2022 + CMake + vcpkg) y deja el paquete en dist\.
# Requisitos en la maquina que compila: Visual Studio 2022 Build Tools (C++), CMake 3.20+, vcpkg con VCPKG_ROOT definido.
#   cd C:\Proyectos\RGX
#   powershell -ExecutionPolicy Bypass -File scripts\compilar.ps1
$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
if (-not $env:VCPKG_ROOT) { throw "Defina VCPKG_ROOT (carpeta de vcpkg)" }

& powershell -ExecutionPolicy Bypass -File scripts\descargar-deps.ps1

cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --preset windows-x64 --output-on-failure

$dist = Join-Path $raiz "dist\ocrx"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
cmake --install build --config Release --prefix $dist
Copy-Item (Join-Path $raiz "README.md") $dist
Compress-Archive -Path "$dist\*" -DestinationPath (Join-Path $raiz "dist\ocrx-win-x64.zip") -Force
Write-Host "Paquete listo: dist\ocrx-win-x64.zip"

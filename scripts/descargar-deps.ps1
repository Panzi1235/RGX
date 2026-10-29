# Descarga PDFium (binario de bblanchon/pdfium-binaries) a third_party\pdfium.
# Se ejecuta UNA vez en la maquina que compila (Jenkins), desde la raiz del repo:
#   cd C:\Proyectos\RGX
#   powershell -ExecutionPolicy Bypass -File scripts\descargar-deps.ps1
param([string]$Version = "chromium/6996")

# Modelo de espanol de Tesseract (tessdata_fast, GitHub) a third_party\tessdata

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
$destino = Join-Path $raiz "third_party\pdfium"
if (Test-Path (Join-Path $destino "include\fpdfview.h")) { Write-Host "PDFium ya esta en $destino" }
else {
New-Item -ItemType Directory -Force -Path $destino | Out-Null
$url = "https://github.com/bblanchon/pdfium-binaries/releases/download/$($Version.Replace('/','%2F'))/pdfium-win-x64.tgz"
$tgz = Join-Path $env:TEMP "pdfium-win-x64.tgz"
Write-Host "Descargando $url"
Invoke-WebRequest -Uri $url -OutFile $tgz
tar -xzf $tgz -C $destino
Write-Host "PDFium listo en $destino (include\, lib\pdfium.dll.lib, bin\pdfium.dll)"
}

$tess = Join-Path $raiz "third_party\tessdata"
New-Item -ItemType Directory -Force -Path $tess | Out-Null
foreach ($lang in @("spa", "eng", "osd")) {
    $f = Join-Path $tess "$lang.traineddata"
    if (-not (Test-Path $f)) {
        $u = "https://github.com/tesseract-ocr/tessdata_fast/raw/main/$lang.traineddata"
        Write-Host "Descargando $u"
        Invoke-WebRequest -Uri $u -OutFile $f
    }
}
Write-Host "tessdata listo en $tess"

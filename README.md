# OCRX — lectura de comprobantes SUNAT en C

Servicio Windows que recibe un comprobante (PDF, XML, ZIP, foto) y devuelve un JSON con los datos
validados con las reglas de SUNAT. Pensado para rendiciones: cada campo dice de dónde salió y cuánta
confianza tiene, y el documento se marca `requiereRevision` cuando algo no cuadra.

## Estado

| Entrada | Qué hace |
|---|---|
| XML UBL 2.1 (factura, boleta, NC, ND, guía, retención, percepción) | Lee todo: cabecera, partes, ítems, totales, detracción |
| ZIP con el XML firmado | Extrae el XML y lo lee |
| PDF con capa de texto | Extrae el texto, busca el QR y aplica las reglas |
| PDF escaneado / foto (JPG, PNG, TIFF, BMP) | QR (RUC, serie, número, IGV, total, fecha) + OCR del texto con Tesseract (español): razón social, cliente, totales, letras |
| Texto plano (salida de un OCR externo) | Aplica las mismas reglas: útil para pruebas |

Reglas implementadas: RUC con módulo 11 y corrección de confusiones (O→0, S→5…), series según tipo (F/B/E/T/EB01),
montos en todos los formatos (`S/ 1,234.56`, `1.234,56`, `118.0O`), fechas en todos los formatos, monto en letras
("SON: CIENTO DIECIOCHO CON 00/100 SOLES") convertido a número, cuentas cruzadas (gravada + exonerada + inafecta +
IGV + ICBPER − descuentos = total) con reparación de dígitos mal leídos, tasas 18 % / 10.5 % / 8 %, detracción,
retención, percepción, referencia de notas de crédito/débito.

## Uso

```
ocrx factura.pdf                 JSON compacto por stdout
ocrx factura.pdf --bonito        JSON con sangría
ocrx --lote C:\comprobantes      CSV con una fila por archivo (para el banco de pruebas)
ocrx --servir --puerto 8085      servicio HTTP en primer plano
ocrx --instalar --puerto 8085    registra el servicio de Windows, abre el firewall y lo inicia (como administrador)
ocrx --desinstalar               lo detiene y lo elimina
ocrx --estado
```

### API HTTP

```
GET  /v1/salud                   -> {"ok":true,"version":"0.1.0"}
POST /v1/comprobante             cuerpo = el archivo tal cual (Content-Type cualquiera) o multipart con campo "archivo"
POST /v1/comprobante?bonito=1    JSON con sangría
POST /v1/texto                   cuerpo = texto plano ya extraído
```

Respuesta: `{"ok":true,"error":null,"comprobante":{...}}`. Códigos: 200 ok, 415 formato no reconocido,
422 no se pudo leer (el JSON igual trae lo que se alcanzó a leer y el motivo).

Desde Java 11, sin librerías:

```java
HttpClient http = HttpClient.newHttpClient();
HttpRequest req = HttpRequest.newBuilder(URI.create("http://SERVIDOR:8085/v1/comprobante"))
    .POST(HttpRequest.BodyPublishers.ofFile(Path.of("factura.pdf"))).build();
String json = http.send(req, HttpResponse.BodyHandlers.ofString()).body();
```

### JSON de salida (resumen)

```json
{
  "tipo": "01", "serie": "F001", "numero": "123", "serieNumero": "F001-00000123",
  "fechaEmision": "2026-10-15", "moneda": "PEN", "formaPago": "Credito",
  "emisor":  { "ruc": "20100070970", "razonSocial": "...", "direccion": "..." },
  "cliente": { "ruc": "20131312955", "razonSocial": "..." },
  "totales": { "gravada": 100, "exonerada": 0, "inafecta": 0, "igv": 18, "total": 118, "tasaIgv": 0.18, "cuadra": true,
               "montoEnLetras": "...", "detraccion": { "porcentaje": 12, "monto": 14.16, "codigo": "037", "cuenta": "..." } },
  "items": [ { "cantidad": 2, "unidad": "NIU", "descripcion": "...", "precioUnitario": 50, "importe": 118 } ],
  "campos": { "totales.total": { "origen": "qr|xml|pdf_texto|ocr|letras|calculo", "confianza": 0.99, "corregido": false, "dudoso": false } },
  "diagnostico": { "electronico": true, "teniaQr": true, "teniaXml": false, "paginas": 1, "ms": 12.3 },
  "requiereRevision": false, "motivoRevision": ""
}
```

## Compilar (Windows)

Requisitos en la máquina que compila: Visual Studio 2022 Build Tools (C++), CMake 3.20+, vcpkg con `VCPKG_ROOT`.

```
cd C:\Proyectos\RGX
powershell -ExecutionPolicy Bypass -File scripts\compilar.ps1
```

Deja `dist\ocrx-win-x64.zip` con `ocrx.exe`, `pdfium.dll`, `ocrx.dll` (API en C) y `ocrx.ini`.
El `Jenkinsfile` hace lo mismo en Jenkins. PDFium se baja de `bblanchon/pdfium-binaries` (script `descargar-deps.ps1`);
el resto viene de vcpkg (`expat`, `zlib`, `quirc`, `libjpeg-turbo`, `libpng`, `tiff`).

## Manual de instalación en un servidor Windows

Todos los pasos se ejecutan **en el servidor**, en PowerShell **como administrador**. En la máquina que compila
solo se genera `dist\ocrx-win-x64.zip` (ver "Compilar"); el servidor no necesita compilador, vcpkg ni Git.

**Requisitos del servidor**: Windows Server 2016 o superior (o Windows 10/11) de 64 bits. No requiere Python, Java,
.NET ni VC++ Redistributable: el runtime va dentro del ejecutable.

### 1. Copiar el paquete
Llevar `ocrx-win-x64.zip` al servidor (carpeta compartida, escritorio remoto o USB) y descomprimirlo:
```powershell
New-Item -ItemType Directory -Force C:\Servicios\ocrx | Out-Null
Expand-Archive C:\Descargas\ocrx-win-x64.zip C:\Servicios\ocrx -Force
dir C:\Servicios\ocrx
```
Debe contener `ocrx.exe`, `pdfium.dll`, `ocrx.dll`, `ocrx.ini` y la carpeta `models\tessdata` (con `spa.traineddata`).

### 2. Instalar e iniciar el servicio
```powershell
cd C:\Servicios\ocrx
.\ocrx.exe --instalar --puerto 8085
```
Ese comando registra el servicio de Windows **OCRX** con arranque automático, reinicio automático si se cae
(a los 5 s, 10 s y 30 s), abre el puerto en el firewall de Windows y lo inicia. Para otro puerto, cambiar el número.

### 3. Verificar
```powershell
cd C:\Servicios\ocrx
.\ocrx.exe --estado
curl.exe http://localhost:8085/v1/salud
```
Respuesta esperada: `{"ok":true,"version":"0.1.0",...}`. Desde otra máquina de la red, `http://IP_DEL_SERVIDOR:8085/`
muestra un formulario para subir un comprobante y ver el JSON.

### Operación
Todos desde `C:\Servicios\ocrx`, como administrador:

| Tarea | Comando |
|---|---|
| Estado del servicio | `.\ocrx.exe --estado` |
| Detener / iniciar | `Stop-Service OCRX` / `Start-Service OCRX` |
| Actualizar a una versión nueva | `Stop-Service OCRX`, descomprimir el zip nuevo encima con `-Force`, `Start-Service OCRX` |
| Cambiar de puerto | `.\ocrx.exe --instalar --puerto 9090` (reconfigura el mismo servicio) |
| Desinstalar | `.\ocrx.exe --desinstalar` |
| Probar un archivo a mano | `.\ocrx.exe C:\ruta\factura.pdf --bonito` |
| Procesar una carpeta (CSV) | `.\ocrx.exe --lote C:\ruta\carpeta > resultado.csv` |
| Ver qué leyó el OCR de una foto | `.\ocrx.exe C:\ruta\foto.jpg --texto` |
| Correr en primer plano (diagnóstico) | `Stop-Service OCRX` y luego `.\ocrx.exe --servir --puerto 8085` |

### Problemas frecuentes
- **"no se pudo abrir el puerto"**: otro programa usa el puerto; instalar con otro (`--instalar --puerto 8086`).
- **"Failed loading language 'spa'"**: falta `models\tessdata\spa.traineddata` junto a `ocrx.exe`; volver a descomprimir el zip completo.
- **No responde desde otra máquina pero sí en local**: revisar que la regla de firewall `OCRX 8085` exista (`netsh advfirewall firewall show rule name="OCRX 8085"`) o que un firewall externo la bloquee.
- **El servicio no arranca**: ver el Visor de eventos → Windows → Sistema, origen "Service Control Manager", y probar `.\ocrx.exe --servir` en primer plano para ver el error.

## Estructura

```
include/ocrx.h        API pública en C
src/core/             arena de memoria, texto UTF-8, JSON
src/sunat/            reglas: ruc.c serie.c montos.c fechas.c letras.c totales.c qr.c ubl.c
src/ingest/           formato.c (detección por contenido) zipxml.c pdf.c (PDFium)
src/extraer/texto.c   extracción etiqueta→valor desde líneas de texto (PDF u OCR)
src/qr/qr_imagen.c    QR en imágenes (quirc)
src/ingest/imagen.c   JPG/PNG/TIFF/BMP a gris, con orientación EXIF
src/ocr/tess.c        OCR de texto con Tesseract (API en C), preparación de imagen y elección del mejor modo
src/svc/              http.c (servidor propio, sin dependencias) servicio_win.c (SCM, firewall)
src/motor.c           orquestación, consolidación, decisión de revisión, JSON
cli/ocrx.c            ejecutable
tests/                test_sunat.c (138 pruebas) y muestras
```

## Pendiente

Padrón RUC local (razón social oficial y estado del RUC), preparación de fotos de celular (perspectiva, orientación,
sombras, tickets térmicos), `POST /v1/calidad` para rechazar fotos malas al capturarlas, detalle de ítems desde texto,
consulta de validez en SUNAT y lectura de `ocrx.ini`.

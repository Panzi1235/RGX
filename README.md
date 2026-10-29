# OCRX — lectura de comprobantes SUNAT en C

Servicio Windows que recibe un comprobante (PDF, XML, ZIP, foto) y devuelve un JSON con los datos
validados con las reglas de SUNAT. Pensado para rendiciones: cada campo dice de dónde salió y cuánta
confianza tiene, y el documento se marca `requiereRevision` cuando algo no cuadra.

## Estado (fase 1)

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

## Instalar en el servidor

```
Expand-Archive ocrx-win-x64.zip C:\Servicios\ocrx
cd C:\Servicios\ocrx
.\ocrx.exe --instalar --puerto 8085
curl.exe http://localhost:8085/v1/salud
```

Arranque automático y reinicio solo si se cae (5 s, 10 s, 30 s). Sin Python, .NET ni VC++ Redistributable.

## Estructura

```
include/ocrx.h        API pública en C
src/core/             arena de memoria, texto UTF-8, JSON
src/sunat/            reglas: ruc.c serie.c montos.c fechas.c letras.c totales.c qr.c ubl.c
src/ingest/           formato.c (detección por contenido) zipxml.c pdf.c (PDFium)
src/extraer/texto.c   extracción etiqueta→valor desde líneas de texto (PDF u OCR)
src/qr/qr_imagen.c    QR en imágenes (quirc)
src/ingest/imagen.c   JPG/PNG/TIFF/BMP a gris, con orientación EXIF
src/svc/              http.c (servidor propio, sin dependencias) servicio_win.c (SCM, firewall)
src/motor.c           orquestación, consolidación, decisión de revisión, JSON
cli/ocrx.c            ejecutable
tests/                test_sunat.c (138 pruebas) y muestras
```

## Fase 2 (pendiente)

OCR del texto en fotos y PDF escaneados con ONNX Runtime + DirectML (PP-OCRv6 afinado en español), preparación
de fotos (perspectiva, orientación, sombras, tickets térmicos), `POST /v1/calidad` para rechazar fotos malas al
capturarlas, padrón RUC local, detalle de ítems desde texto, y consulta de validez en SUNAT.

#include "formato.h"
#include "../core/str.h"
#include <string.h>
#include <ctype.h>

static int extension_es(const char *nombre, const char *ext) {
    if (!nombre) return 0;
    const char *p = strrchr(nombre, '.');
    if (!p) return 0;
    return str_empieza(p + 1, ext) && strlen(p + 1) == strlen(ext);
}

formato formato_detectar(const void *datos, size_t n, const char *nombre) {
    const unsigned char *b = (const unsigned char *)datos;
    if (!b || n < 4) return FMT_DESCONOCIDO;
    if (n >= 5 && !memcmp(b, "%PDF-", 5)) return FMT_PDF;
    /* algunos PDF traen basura antes de la cabecera */
    { size_t lim = n < 1024 ? n : 1024; for (size_t i = 0; i + 5 <= lim; i++) if (!memcmp(b + i, "%PDF-", 5)) return FMT_PDF; }
    if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return FMT_JPEG;
    if (n >= 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) return FMT_PNG;
    if ((!memcmp(b, "II*\0", 4)) || (!memcmp(b, "MM\0*", 4))) return FMT_TIFF;
    if (b[0] == 'B' && b[1] == 'M') return FMT_BMP;
    if (n >= 6 && (!memcmp(b, "GIF87a", 6) || !memcmp(b, "GIF89a", 6))) return FMT_GIF;
    if (n >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4)) return FMT_WEBP;
    if (n >= 12 && !memcmp(b + 4, "ftyp", 4) && (!memcmp(b + 8, "heic", 4) || !memcmp(b + 8, "heix", 4) || !memcmp(b + 8, "mif1", 4) || !memcmp(b + 8, "heif", 4))) return FMT_HEIC;
    if (!memcmp(b, "PK\x03\x04", 4) || !memcmp(b, "PK\x05\x06", 4)) return FMT_ZIP;
    /* XML / texto: saltar BOM y espacios */
    size_t i = 0;
    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i = 3;
    /* UTF-16 LE con BOM */
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) return FMT_XML; /* lo tratamos como XML/texto UTF-16; el lector lo convertirá */
    while (i < n && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n')) i++;
    if (i < n && b[i] == '<') return FMT_XML;
    if (i < n && (b[i] == '{' || b[i] == '[')) return FMT_JSON;
    /* ¿texto plano? */
    size_t lim = n < 2048 ? n : 2048; size_t raros = 0;
    for (size_t k = i; k < lim; k++) { unsigned char c = b[k]; if (c < 0x09 || (c > 0x0D && c < 0x20)) raros++; }
    if (raros * 50 < lim) return FMT_TEXTO;
    if (extension_es(nombre, "pdf")) return FMT_PDF;
    if (extension_es(nombre, "xml")) return FMT_XML;
    if (extension_es(nombre, "txt")) return FMT_TEXTO;
    return FMT_DESCONOCIDO;
}

const char *formato_nombre(formato f) {
    switch (f) {
    case FMT_PDF: return "pdf"; case FMT_XML: return "xml"; case FMT_ZIP: return "zip"; case FMT_JPEG: return "jpeg"; case FMT_PNG: return "png";
    case FMT_TIFF: return "tiff"; case FMT_BMP: return "bmp"; case FMT_GIF: return "gif"; case FMT_WEBP: return "webp"; case FMT_HEIC: return "heic";
    case FMT_TEXTO: return "texto"; case FMT_JSON: return "json"; default: return "desconocido";
    }
}
int formato_es_imagen(formato f) { return f == FMT_JPEG || f == FMT_PNG || f == FMT_TIFF || f == FMT_BMP || f == FMT_GIF || f == FMT_WEBP || f == FMT_HEIC; }

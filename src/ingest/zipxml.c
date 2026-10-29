#include "zipxml.h"
#include "../core/str.h"
#include <string.h>
#include <stdint.h>
#ifdef OCRX_HAVE_ZLIB
#include <zlib.h>
#endif

static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t u32(const unsigned char *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

typedef struct { const unsigned char *local; char nombre[256]; uint16_t metodo; uint32_t comp, descomp; } entrada;

static int puntaje_nombre(const char *n) {
    char u[256]; str_normalizar(n, u, sizeof u, true);
    const char *base = strrchr(u, '/'); base = base ? base + 1 : u;
    size_t L = strlen(base);
    if (L < 4 || strcmp(base + L - 4, ".XML") != 0) return -100;
    int p = 1;
    if (str_empieza(base, "R-") || str_contiene(base, "CDR")) p -= 50;     /* constancia de recepción, no el comprobante */
    if (L >= 11) { int dig = 1; for (int i = 0; i < 11; i++) if (base[i] < '0' || base[i] > '9') { dig = 0; break; } if (dig) p += 10; }
    if (str_contiene(base, "-01-") || str_contiene(base, "-03-") || str_contiene(base, "-07-") || str_contiene(base, "-08-")) p += 5;
    if (str_contiene(base, "SIGNED") || str_contiene(base, "FIRMADO")) p += 2;
    return p;
}

char *zipxml_extraer(const void *zipv, size_t n, size_t *out_n, arena *a, char nombre_out[256]) {
    const unsigned char *z = (const unsigned char *)zipv;
    if (n < 22) return NULL;
    /* buscar End Of Central Directory */
    size_t eocd = 0; int hallado = 0;
    for (size_t i = n - 22; i + 22 <= n; i--) { if (u32(z + i) == 0x06054b50) { eocd = i; hallado = 1; break; } if (i == 0) break; if (n - i > 70000) break; }
    entrada mejor; int mejor_p = -1000; memset(&mejor, 0, sizeof mejor);
    if (hallado) {
        uint16_t total = u16(z + eocd + 10); uint32_t cd = u32(z + eocd + 16);
        size_t off = cd;
        for (uint16_t k = 0; k < total && off + 46 <= n; k++) {
            if (u32(z + off) != 0x02014b50) break;
            entrada e; memset(&e, 0, sizeof e);
            e.metodo = u16(z + off + 10); e.comp = u32(z + off + 20); e.descomp = u32(z + off + 24);
            uint16_t ln = u16(z + off + 28), lx = u16(z + off + 30), lc = u16(z + off + 32);
            uint32_t lo = u32(z + off + 42);
            size_t cn = ln < 255 ? ln : 255; memcpy(e.nombre, z + off + 46, cn); e.nombre[cn] = 0;
            if (lo + 30 <= n) e.local = z + lo;
            int p = puntaje_nombre(e.nombre);
            if (p > mejor_p && e.local) { mejor_p = p; mejor = e; }
            off += 46 + ln + lx + lc;
        }
    }
    if (!mejor.local || mejor_p < -50) {
        /* sin directorio central: recorrer cabeceras locales */
        size_t off = 0;
        while (off + 30 <= n && u32(z + off) == 0x04034b50) {
            entrada e; memset(&e, 0, sizeof e);
            e.metodo = u16(z + off + 8); e.comp = u32(z + off + 18); e.descomp = u32(z + off + 22);
            uint16_t ln = u16(z + off + 26), lx = u16(z + off + 28);
            size_t cn = ln < 255 ? ln : 255; memcpy(e.nombre, z + off + 30, cn); e.nombre[cn] = 0;
            e.local = z + off;
            int p = puntaje_nombre(e.nombre);
            if (p > mejor_p) { mejor_p = p; mejor = e; }
            off += 30 + ln + lx + e.comp;
            if (e.comp == 0 && (u16(z + off - 30 - ln - lx + 6) & 8)) break; /* data descriptor: no podemos saltar */
        }
    }
    if (!mejor.local || mejor_p < -50) return NULL;
    const unsigned char *h = mejor.local;
    uint16_t ln = u16(h + 26), lx = u16(h + 28);
    const unsigned char *datos = h + 30 + ln + lx;
    if ((size_t)(datos - z) + mejor.comp > n) return NULL;
    if (nombre_out) str_copiar(nombre_out, 256, mejor.nombre);
    if (mejor.metodo == 0) {
        char *out = (char *)arena_alloc(a, mejor.comp + 1); memcpy(out, datos, mejor.comp); out[mejor.comp] = 0; *out_n = mejor.comp; return out;
    }
#ifdef OCRX_HAVE_ZLIB
    if (mejor.metodo == 8) {
        size_t cap = mejor.descomp ? mejor.descomp + 1 : mejor.comp * 8 + 1024;
        char *out = (char *)arena_alloc(a, cap + 1);
        z_stream s; memset(&s, 0, sizeof s);
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return NULL;
        s.next_in = (Bytef *)datos; s.avail_in = mejor.comp; s.next_out = (Bytef *)out; s.avail_out = (uInt)cap;
        int r = inflate(&s, Z_FINISH);
        size_t got = s.total_out; inflateEnd(&s);
        if (r != Z_STREAM_END && r != Z_OK && r != Z_BUF_ERROR) return NULL;
        out[got] = 0; *out_n = got; return out;
    }
#endif
    return NULL;
}

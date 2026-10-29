#include "qr_imagen.h"
#include <string.h>
#include <stdlib.h>

unsigned char *bgra_a_gris(const unsigned char *bgra, int ancho, int alto, int stride, arena *a) {
    unsigned char *g = (unsigned char *)arena_alloc(a, (size_t)ancho * alto);
    for (int y = 0; y < alto; y++) {
        const unsigned char *f = bgra + (size_t)y * stride; unsigned char *o = g + (size_t)y * ancho;
        for (int x = 0; x < ancho; x++, f += 4) o[x] = (unsigned char)((f[0] * 29 + f[1] * 150 + f[2] * 77) >> 8);
    }
    return g;
}

#ifdef OCRX_HAVE_QUIRC
#include <quirc.h>

bool qr_disponible(void) { return true; }

static size_t decodificar(struct quirc *q, char **textos, size_t cap, arena *a) {
    size_t n = 0;
    int cnt = quirc_count(q);
    for (int i = 0; i < cnt && n < cap; i++) {
        struct quirc_code code; struct quirc_data data;
        quirc_extract(q, i, &code);
        quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err == QUIRC_ERROR_DATA_ECC) { quirc_flip(&code); err = quirc_decode(&code, &data); }
        if (err) continue;
        textos[n++] = arena_strndup(a, (const char *)data.payload, (size_t)data.payload_len);
    }
    return n;
}

/* Reescala por vecino más cercano (para probar varias escalas: QR pequeños en fotos grandes, o muy grandes) */
static unsigned char *escalar(const unsigned char *g, int w, int h, int stride, int nw, int nh, arena *a) {
    unsigned char *o = (unsigned char *)arena_alloc(a, (size_t)nw * nh);
    for (int y = 0; y < nh; y++) { int sy = (int)((long long)y * h / nh); for (int x = 0; x < nw; x++) { int sx = (int)((long long)x * w / nw); o[(size_t)y * nw + x] = g[(size_t)sy * stride + sx]; } }
    return o;
}

size_t qr_leer_gris(const unsigned char *gris, int ancho, int alto, int stride, char **textos, size_t cap, arena *a) {
    size_t n = 0;
    double escalas[] = {1.0, 0.5, 2.0, 0.75, 1.5, 0.33};
    for (size_t e = 0; e < sizeof escalas / sizeof *escalas && n == 0; e++) {
        int nw = (int)(ancho * escalas[e]), nh = (int)(alto * escalas[e]);
        if (nw < 50 || nh < 50 || (long long)nw * nh > 40000000LL) continue;
        struct quirc *q = quirc_new(); if (!q) return 0;
        if (quirc_resize(q, nw, nh) < 0) { quirc_destroy(q); continue; }
        int w, h; uint8_t *buf = quirc_begin(q, &w, &h);
        if (escalas[e] == 1.0 && stride == ancho) memcpy(buf, gris, (size_t)w * h);
        else { unsigned char *s = escalar(gris, ancho, alto, stride, nw, nh, a); memcpy(buf, s, (size_t)w * h); }
        quirc_end(q);
        n = decodificar(q, textos, cap, a);
        quirc_destroy(q);
    }
    return n;
}
#else
bool qr_disponible(void) { return false; }
size_t qr_leer_gris(const unsigned char *gris, int ancho, int alto, int stride, char **textos, size_t cap, arena *a) { (void)gris; (void)ancho; (void)alto; (void)stride; (void)textos; (void)cap; (void)a; return 0; }
#endif

#include "../ingest/imagen.h"
size_t qr_leer_archivo(const void *datos, size_t n, char **textos, size_t cap, arena *a) {
    imagen_gris img; char e[128];
    if (!imagen_cargar_gris(datos, n, &img, a, e, sizeof e)) return 0;
    return qr_leer_gris(img.pix, img.ancho, img.alto, img.ancho, textos, cap, a);
}

#include "tess.h"
#include "../core/str.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ---------------- preparación de imagen (sin dependencias) ---------------- */

static unsigned char *reescalar_bilineal(const unsigned char *g, int w, int h, int nw, int nh, arena *a) {
    unsigned char *o = (unsigned char *)arena_alloc(a, (size_t)nw * nh);
    for (int y = 0; y < nh; y++) {
        float sy = (y + 0.5f) * h / nh - 0.5f; int y0 = (int)floorf(sy); float fy = sy - y0; if (y0 < 0) { y0 = 0; fy = 0; } int y1 = y0 + 1 < h ? y0 + 1 : y0;
        for (int x = 0; x < nw; x++) {
            float sx = (x + 0.5f) * w / nw - 0.5f; int x0 = (int)floorf(sx); float fx = sx - x0; if (x0 < 0) { x0 = 0; fx = 0; } int x1 = x0 + 1 < w ? x0 + 1 : x0;
            float v = g[(size_t)y0 * w + x0] * (1 - fx) * (1 - fy) + g[(size_t)y0 * w + x1] * fx * (1 - fy) + g[(size_t)y1 * w + x0] * (1 - fx) * fy + g[(size_t)y1 * w + x1] * fx * fy;
            o[(size_t)y * nw + x] = (unsigned char)(v + 0.5f);
        }
    }
    return o;
}

/* Reduce con promedio de cajas (fotos de 12-48 MP) */
static unsigned char *reducir_cajas(const unsigned char *g, int w, int h, int f, arena *a) {
    int nw = w / f, nh = h / f;
    unsigned char *o = (unsigned char *)arena_alloc(a, (size_t)nw * nh);
    for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) {
        unsigned s = 0; for (int dy = 0; dy < f; dy++) for (int dx = 0; dx < f; dx++) s += g[(size_t)(y * f + dy) * w + x * f + dx];
        o[(size_t)y * nw + x] = (unsigned char)(s / (unsigned)(f * f));
    }
    return o;
}

void tess_preparar(imagen_gris *img, arena *a, int *dpi_estimado) {
    int w = img->ancho, h = img->alto;
    /* 1) estimar dpi: si la imagen lo trae, usarlo; si no, suponer que el lado largo es una hoja A4 (297 mm) o un ticket */
    int dpi = img->dpi;
    int largo = w > h ? w : h;
    if (dpi <= 0 || dpi > 1200) dpi = (int)(largo / 11.7 + 0.5); /* A4: 11.7 pulgadas de alto */
    if (dpi < 72) dpi = 72;
    /* 2) llevar a ~300 dpi: Tesseract rinde mejor entre 250 y 400 */
    if (dpi < 200 && largo < 2200) {
        float f = 300.0f / dpi; if (f > 3) f = 3;
        int nw = (int)(w * f), nh = (int)(h * f);
        img->pix = reescalar_bilineal(img->pix, w, h, nw, nh, a); img->ancho = w = nw; img->alto = h = nh; dpi = 300;
    } else if (largo > 4500) {
        int f = largo / 3500 + 1; if (f > 4) f = 4;
        img->pix = reducir_cajas(img->pix, w, h, f, a); img->ancho = w /= f; img->alto = h /= f; dpi /= f;
    }
    /* 3) estirar contraste (percentiles 1 % y 99 %) — ayuda en fotos grises o con poca luz */
    unsigned hist[256] = {0}; size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) hist[img->pix[i]]++;
    size_t acc = 0; int lo = 0, hi = 255;
    for (int i = 0; i < 256; i++) { acc += hist[i]; if (acc >= n / 100) { lo = i; break; } }
    acc = 0; for (int i = 255; i >= 0; i--) { acc += hist[i]; if (acc >= n / 100) { hi = i; break; } }
    if (hi - lo > 20 && (lo > 10 || hi < 245)) {
        float esc = 255.0f / (hi - lo);
        for (size_t i = 0; i < n; i++) { int v = (int)((img->pix[i] - lo) * esc + 0.5f); img->pix[i] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }
    }
    /* 4) si el fondo es oscuro (foto invertida o fondo negro de escáner) y el texto claro, invertir */
    size_t claros = 0; for (size_t i = 0; i < n; i += 7) if (img->pix[i] > 128) claros++;
    if (claros * 7 < n / 3) for (size_t i = 0; i < n; i++) img->pix[i] = (unsigned char)(255 - img->pix[i]);
    if (dpi_estimado) *dpi_estimado = dpi;
    img->dpi = dpi;
}

/* ---------------- Tesseract ---------------- */
#ifdef OCRX_HAVE_TESSERACT
#include <tesseract/capi.h>
#include <leptonica/allheaders.h>

struct tess_motor { TessBaseAPI *api; };

bool tess_disponible(void) { return true; }

tess_motor *tess_crear(const char *dir, const char *idiomas, char *error, size_t cap) {
    tess_motor *t = (tess_motor *)calloc(1, sizeof *t);
    if (!t) return NULL;
    setMsgSeverity(L_SEVERITY_NONE);   /* Leptonica: sin "Error in boxClipToRectangle" por stderr */
    t->api = TessBaseAPICreate();
    if (TessBaseAPIInit3(t->api, dir, idiomas && *idiomas ? idiomas : "spa") != 0) {
        if (error && cap) snprintf(error, cap, "no se pudo cargar %s desde %s (falta spa.traineddata?)", idiomas ? idiomas : "spa", dir ? dir : "(tessdata)");
        TessBaseAPIDelete(t->api); free(t); return NULL;
    }
    /* Ajustes para comprobantes: conservar espacios entre columnas, no inventar diccionario sobre códigos */
    TessBaseAPISetVariable(t->api, "preserve_interword_spaces", "1");
#ifdef _WIN32
    TessBaseAPISetVariable(t->api, "debug_file", "NUL");
#else
    TessBaseAPISetVariable(t->api, "debug_file", "/dev/null");
#endif
    TessBaseAPISetVariable(t->api, "user_defined_dpi", "300");
    TessBaseAPISetVariable(t->api, "tessedit_do_invert", "0");
    /* Alfabeto de comprobante: letras, dígitos y signos habituales. No restringimos del todo (direcciones, nombres), pero quitamos basura */
    TessBaseAPISetVariable(t->api, "tessedit_char_blacklist", "¬¦§¨©ª«®¯²³´¶·¸¹»¼½¾¿×÷");
    if (error && cap) error[0] = 0;
    return t;
}

void tess_destruir(tess_motor *t) { if (!t) return; if (t->api) { TessBaseAPIEnd(t->api); TessBaseAPIDelete(t->api); } free(t); }

float tess_reconocer(tess_motor *t, const imagen_gris *img, int psm, int dpi, doc_texto *d, arena *a) {
    if (!t || !img || !img->pix) return -1;
    TessBaseAPISetPageSegMode(t->api, (TessPageSegMode)psm);
    TessBaseAPISetImage(t->api, img->pix, img->ancho, img->alto, 1, img->ancho);
    if (dpi > 0) TessBaseAPISetSourceResolution(t->api, dpi);
    if (TessBaseAPIRecognize(t->api, NULL) != 0) return -1;
    TessResultIterator *it = TessBaseAPIGetIterator(t->api);
    if (!it) return -1;
    TessPageIterator *pit = TessResultIteratorGetPageIterator(it);
    double suma = 0; int cnt = 0;
    do {
        char *txt = TessResultIteratorGetUTF8Text(it, RIL_TEXTLINE);
        if (!txt) continue;
        float conf = TessResultIteratorConfidence(it, RIL_TEXTLINE) / 100.0f;
        int l, tt, r, b;
        if (!TessPageIteratorBoundingBox(pit, RIL_TEXTLINE, &l, &tt, &r, &b)) { l = tt = r = b = 0; }
        char *s = txt; str_recortar(s);
        if (*s) { doc_texto_agregar(d, a, s, (float)l, (float)tt, (float)r, (float)b, 1, conf); suma += conf; cnt++; }
        TessDeleteText(txt);
    } while (TessPageIteratorNext(pit, RIL_TEXTLINE));
    TessResultIteratorDelete(it);
    return cnt ? (float)(suma / cnt) : 0.0f;
}
#else
struct tess_motor { int nada; };
bool tess_disponible(void) { return false; }
tess_motor *tess_crear(const char *dir, const char *idiomas, char *error, size_t cap) { (void)dir; (void)idiomas; if (error && cap) snprintf(error, cap, "Tesseract no compilado"); return NULL; }
void tess_destruir(tess_motor *t) { (void)t; }
float tess_reconocer(tess_motor *t, const imagen_gris *img, int psm, int dpi, doc_texto *d, arena *a) { (void)t; (void)img; (void)psm; (void)dpi; (void)d; (void)a; return -1; }
#endif

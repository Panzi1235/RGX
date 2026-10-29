#include "pdf.h"
#include "../core/str.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

bool pdf_texto_es_util(const doc_texto *d) {
    if (!d || d->n < 2) return false;
    /* Heurística: proporción de caracteres legibles, presencia de dígitos y palabras clave de comprobante */
    size_t total = 0, legibles = 0, digitos = 0; bool clave = false;
    for (size_t i = 0; i < d->n; i++) {
        const char *s = d->lineas[i].norm;
        for (const char *p = s; *p; p++) {
            total++;
            unsigned char c = (unsigned char)*p;
            if (isalnum(c) || c == ' ' || c == '.' || c == ',' || c == ':' || c == '-' || c == '/' || c == '%' || c == '(' || c == ')' || c == '$' || c >= 0x80) legibles++;
            if (isdigit(c)) digitos++;
        }
        if (str_contiene(s, "RUC") || str_contiene(s, "TOTAL") || str_contiene(s, "FACTURA") || str_contiene(s, "BOLETA") || str_contiene(s, "IGV") || str_contiene(s, "FECHA") || str_contiene(s, "SOLES") || str_contiene(s, "S/")) clave = true;
    }
    if (total < 40) return false;
    if (legibles * 100 < total * 90) return false;
    if (digitos < 8) return false;
    /* texto "fantasma" (todo en una fuente símbolo) suele no tener palabras clave */
    return clave;
}

#ifdef OCRX_HAVE_PDFIUM
#include <fpdfview.h>
#include <fpdf_text.h>

static int g_init = 0;
bool pdf_disponible(void) { return true; }
void pdf_init_global(void) { if (!g_init) { FPDF_LIBRARY_CONFIG cfg; memset(&cfg, 0, sizeof cfg); cfg.version = 2; FPDF_InitLibraryWithConfig(&cfg); g_init = 1; } }
void pdf_fin_global(void) { if (g_init) { FPDF_DestroyLibrary(); g_init = 0; } }

/* UTF-16LE -> UTF-8 */
static size_t utf16_a_utf8(const unsigned short *u, int n, char *out, size_t cap) {
    size_t o = 0;
    for (int i = 0; i < n && u[i]; i++) {
        uint32_t cp = u[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (u[i + 1] - 0xDC00); i++; }
        if (cp == 0xFFFE || cp == 0xFEFF) continue;
        /* ligaduras comunes */
        if (cp == 0xFB01) { if (o + 2 < cap) { out[o++] = 'f'; out[o++] = 'i'; } continue; }
        if (cp == 0xFB02) { if (o + 2 < cap) { out[o++] = 'f'; out[o++] = 'l'; } continue; }
        char b[4]; size_t k = utf8_codificar(cp, b);
        if (o + k >= cap) break;
        memcpy(out + o, b, k); o += k;
    }
    out[o] = 0; return o;
}

int pdf_texto(const void *datos, size_t n, doc_texto *d, arena *a, int max_paginas, bool *texto_util) {
    pdf_init_global();
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(datos, (int)n, NULL);
    if (!doc) {
        unsigned long e = FPDF_GetLastError();
        if (e == FPDF_ERR_PASSWORD) { /* intentar contraseñas vacías/comunes */ doc = FPDF_LoadMemDocument(datos, (int)n, ""); }
        if (!doc) return -1;
    }
    int paginas = FPDF_GetPageCount(doc);
    int lim = (max_paginas > 0 && max_paginas < paginas) ? max_paginas : paginas;
    for (int p = 0; p < lim; p++) {
        FPDF_PAGE page = FPDF_LoadPage(doc, p);
        if (!page) continue;
        double ancho = FPDF_GetPageWidth(page), alto = FPDF_GetPageHeight(page);
        if (p == 0) { d->ancho_pagina = (float)ancho; d->alto_pagina = (float)alto; }
        FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
        if (tp) {
            int nrect = FPDFText_CountRects(tp, 0, -1);
            for (int r = 0; r < nrect; r++) {
                double l, t, rr, b;
                if (!FPDFText_GetRect(tp, r, &l, &t, &rr, &b)) continue;
                unsigned short buf[2048];
                int nc = FPDFText_GetBoundedText(tp, l, t, rr, b, buf, 2047);
                if (nc <= 0) continue;
                char u8[8192]; utf16_a_utf8(buf, nc, u8, sizeof u8);
                /* coordenadas PDF: origen abajo-izquierda; convertir a arriba-izquierda para ordenar natural */
                float y0 = (float)(alto - t), y1 = (float)(alto - b);
                doc_texto_agregar(d, a, u8, (float)l, y0, (float)rr, y1, p + 1, 1.0f);
            }
            FPDFText_ClosePage(tp);
        }
        FPDF_ClosePage(page);
    }
    FPDF_CloseDocument(doc);
    doc_texto_ordenar(d, a);
    if (texto_util) *texto_util = pdf_texto_es_util(d);
    return paginas;
}

bool pdf_render(const void *datos, size_t n, int pagina, int dpi, pdf_imagen *out, arena *a) {
    pdf_init_global();
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(datos, (int)n, NULL);
    if (!doc) return false;
    FPDF_PAGE page = FPDF_LoadPage(doc, pagina - 1);
    if (!page) { FPDF_CloseDocument(doc); return false; }
    double w = FPDF_GetPageWidth(page), h = FPDF_GetPageHeight(page);
    int W = (int)(w * dpi / 72.0 + 0.5), H = (int)(h * dpi / 72.0 + 0.5);
    if (W < 1 || H < 1 || (long long)W * H > 80000000LL) { FPDF_ClosePage(page); FPDF_CloseDocument(doc); return false; }
    FPDF_BITMAP bm = FPDFBitmap_Create(W, H, 0);
    FPDFBitmap_FillRect(bm, 0, 0, W, H, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bm, page, 0, 0, W, H, 0, FPDF_ANNOT | FPDF_LCD_TEXT);
    int stride = FPDFBitmap_GetStride(bm);
    out->pix = (unsigned char *)arena_alloc(a, (size_t)stride * H);
    memcpy(out->pix, FPDFBitmap_GetBuffer(bm), (size_t)stride * H);
    out->ancho = W; out->alto = H; out->stride = stride; out->pagina = pagina;
    FPDFBitmap_Destroy(bm); FPDF_ClosePage(page); FPDF_CloseDocument(doc);
    return true;
}
#else
bool pdf_disponible(void) { return false; }
void pdf_init_global(void) {}
void pdf_fin_global(void) {}
int  pdf_texto(const void *datos, size_t n, doc_texto *d, arena *a, int max_paginas, bool *texto_util) { (void)datos; (void)n; (void)d; (void)a; (void)max_paginas; if (texto_util) *texto_util = false; return -1; }
bool pdf_render(const void *datos, size_t n, int pagina, int dpi, pdf_imagen *out, arena *a) { (void)datos; (void)n; (void)pagina; (void)dpi; (void)out; (void)a; return false; }
#endif

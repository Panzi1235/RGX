/* Motor OCRX: orquesta lectores, consolida y decide si requiere revisión. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../include/ocrx.h"
#include "core/arena.h"
#include "core/str.h"
#include "core/json.h"
#include "sunat/sunat.h"
#include "sunat/qr.h"
#include "sunat/ubl.h"
#include "ingest/formato.h"
#include "ingest/zipxml.h"
#include "ingest/pdf.h"
#include "extraer/texto.h"
#include "qr/qr_imagen.h"
#include "ingest/imagen.h"
#include "ocr/tess.h"
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#define MAX_TESS 8
struct ocrx_motor {
    ocrx_config cfg;
    char dir_modelos[1100];
    char dir_tessdata[1200];
    /* pool de instancias de Tesseract (no es reentrante): se crean bajo demanda, hasta cfg.hilos */
    tess_motor *tess[MAX_TESS]; bool tess_ocupado[MAX_TESS]; int n_tess, max_tess;
    bool tess_fallo; char tess_error[200];
#ifdef _WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t cs;
#endif
};
static void m_lock(ocrx_motor *m) {
#ifdef _WIN32
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->cs);
#endif
}
static void m_unlock(ocrx_motor *m) {
#ifdef _WIN32
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->cs);
#endif
}
static int cpus(void) {
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 2;
#endif
}
/* Carpeta del ejecutable (para hallar models\ aunque el cwd sea otro) */
static void dir_ejecutable(char *out, size_t cap) {
#ifdef _WIN32
    char b[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, b, MAX_PATH);
    if (n == 0) { str_copiar(out, cap, "."); return; }
    char *s = strrchr(b, '\\'); if (s) *s = 0; str_copiar(out, cap, b);
#else
    char b[4096]; ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    if (n <= 0) { str_copiar(out, cap, "."); return; }
    b[n] = 0; char *s = strrchr(b, '/'); if (s) *s = 0; str_copiar(out, cap, b);
#endif
}
static bool existe_archivo(const char *ruta) { FILE *f = fopen(ruta, "rb"); if (!f) return false; fclose(f); return true; }

/* Toma una instancia de Tesseract del pool (creándola si hace falta). NULL si no está disponible. */
static tess_motor *tess_tomar(ocrx_motor *m, int *idx) {
    if (!tess_disponible() || m->tess_fallo) return NULL;
    for (;;) {
        m_lock(m);
        for (int i = 0; i < m->n_tess; i++) if (!m->tess_ocupado[i]) { m->tess_ocupado[i] = true; *idx = i; tess_motor *t = m->tess[i]; m_unlock(m); return t; }
        if (m->n_tess < m->max_tess) {
            int i = m->n_tess;
            tess_motor *t = tess_crear(m->dir_tessdata, "spa", m->tess_error, sizeof m->tess_error);
            if (!t) { m->tess_fallo = true; m_unlock(m); return NULL; }
            m->tess[i] = t; m->tess_ocupado[i] = true; m->n_tess++; *idx = i; m_unlock(m); return t;
        }
        m_unlock(m);
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
}
static void tess_soltar(ocrx_motor *m, int idx) { m_lock(m); m->tess_ocupado[idx] = false; m_unlock(m); }
struct ocrx_resultado { arena a; ocrx_comprobante c; char *json; bool json_bonito; formato fmt; char nota[300]; };

const char *ocrx_version(void) { return OCRX_VERSION; }
const char *ocrx_error_texto(ocrx_error e) {
    switch (e) {
    case OCRX_OK: return "ok"; case OCRX_E_PARAM: return "parametro invalido"; case OCRX_E_MEMORIA: return "sin memoria";
    case OCRX_E_ARCHIVO: return "no se pudo leer el archivo"; case OCRX_E_FORMATO: return "formato no reconocido";
    case OCRX_E_PDF: return "no se pudo procesar el PDF"; case OCRX_E_XML: return "XML invalido o no es un comprobante UBL";
    case OCRX_E_IMAGEN: return "imagen ilegible"; case OCRX_E_SIN_DATOS: return "no se encontraron datos de comprobante";
    case OCRX_E_NO_DISPONIBLE: return "funcion no disponible en esta compilacion"; default: return "error";
    }
}

ocrx_motor *ocrx_crear(const ocrx_config *cfg, ocrx_error *err) {
    ocrx_motor *m = (ocrx_motor *)calloc(1, sizeof *m);
    if (!m) { if (err) *err = OCRX_E_MEMORIA; return NULL; }
    if (cfg) m->cfg = *cfg;
    if (m->cfg.dpi_render <= 0) m->cfg.dpi_render = 300;
    if (m->cfg.umbral_revision <= 0) m->cfg.umbral_revision = 0.85f;
#ifdef _WIN32
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutex_init(&m->cs, NULL);
#endif
    m->max_tess = m->cfg.hilos > 0 ? m->cfg.hilos : (cpus() / 2 > 1 ? cpus() / 2 : 1);
    if (m->max_tess > MAX_TESS) m->max_tess = MAX_TESS;
    /* carpeta de modelos: la configurada, o models\ junto al ejecutable, o models\ en el cwd */
    if (m->cfg.dir_modelos && *m->cfg.dir_modelos) str_copiar(m->dir_modelos, sizeof m->dir_modelos, m->cfg.dir_modelos);
    else { char exe[1024]; dir_ejecutable(exe, sizeof exe); snprintf(m->dir_modelos, sizeof m->dir_modelos, "%s/models", exe); }
    snprintf(m->dir_tessdata, sizeof m->dir_tessdata, "%s/tessdata", m->dir_modelos);
    { char prueba[1400]; snprintf(prueba, sizeof prueba, "%s/spa.traineddata", m->dir_tessdata);
      if (!existe_archivo(prueba)) { snprintf(prueba, sizeof prueba, "models/tessdata/spa.traineddata"); if (existe_archivo(prueba)) str_copiar(m->dir_tessdata, sizeof m->dir_tessdata, "models/tessdata"); } }
    pdf_init_global();
    if (err) *err = OCRX_OK;
    return m;
}
void ocrx_destruir(ocrx_motor *m) {
    if (!m) return;
    for (int i = 0; i < m->n_tess; i++) tess_destruir(m->tess[i]);
#ifdef _WIN32
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->cs);
#endif
    free(m);
}

/* ---------------- OCR de una imagen en gris: prueba varios modos de segmentación y se queda con el mejor ---------------- */
static int puntaje_extraccion(const ocrx_comprobante *c) {
    int p = 0;
    if (c->emisor.ruc[0]) p += 3;
    if (c->serie[0] && c->numero[0]) p += 3;
    if (c->fecha_emision[0]) p += 2;
    if (c->totales.total > 0) p += 2;
    if (c->totales.gravada > 0) p += 1;
    if (c->totales.igv > 0) p += 1;
    if (c->totales.cuadra) p += 3;
    if (c->emisor.razon_social[0]) p += 1;
    if (c->cliente.ruc[0] || c->cliente.num_doc[0]) p += 1;
    if (c->totales.monto_en_letras[0]) p += 1;
    if (c->tipo != OCRX_CPE_DESCONOCIDO) p += 1;
    return p;
}

static bool ocr_imagen_gris(ocrx_motor *m, ocrx_resultado *r, imagen_gris *img) {
    int idx = -1; tess_motor *t = tess_tomar(m, &idx);
    if (!t) return false;
    int dpi = 0; tess_preparar(img, &r->a, &dpi);
    /* Modos: 4 = columna única (facturas A4), 6 = bloque uniforme (tickets), 3 = automático. Se evalúa cada uno con las reglas SUNAT. */
    const int modos[] = {4, 6, 3};
    ocrx_comprobante mejor; memset(&mejor, 0, sizeof mejor); int mejor_p = -1; float mejor_conf = 0; doc_texto *mejor_d = NULL; int mejor_modo = 0;
    ocrx_comprobante base = r->c; /* lo que ya trajo el QR */
    for (size_t k = 0; k < sizeof modos / sizeof *modos; k++) {
        doc_texto *d = doc_texto_nuevo(&r->a);
        float conf = tess_reconocer(t, img, modos[k], dpi, d, &r->a);
        if (conf < 0 || d->n == 0) continue;
        doc_texto_ordenar(d, &r->a);
        ocrx_comprobante c = base;
        extraer_desde_texto(d, &c, &r->a, OCRX_ORIGEN_OCR);
        ocrx_totales tt = c.totales; double letras = 0; ocrx_moneda mo; bool hl = tt.monto_en_letras[0] && sunat_letras_a_monto(tt.monto_en_letras, &letras, &mo);
        sunat_totales_reparar(&tt, letras, hl); c.totales.cuadra = tt.cuadra;
        int p = puntaje_extraccion(&c);
        if (p > mejor_p || (p == mejor_p && conf > mejor_conf)) { mejor = c; mejor_p = p; mejor_conf = conf; mejor_d = d; mejor_modo = modos[k]; }
        if (p >= 17) break;  /* ya salió todo: no gastar más tiempo */
    }
    tess_soltar(m, idx);
    if (mejor_p < 0) return false;
    if (m->cfg.volcar_texto && mejor_d) {
        extraer_depurar = 1;
        { ocrx_comprobante tmp = base; extraer_desde_texto(mejor_d, &tmp, &r->a, OCRX_ORIGEN_OCR); }
        extraer_depurar = 0;
        fprintf(stderr, "--- texto OCR (modo %d, confianza %.0f%%, puntaje %d) ---\n", mejor_modo, mejor_conf * 100, mejor_p);
        for (size_t i = 0; i < mejor_d->n; i++) fprintf(stderr, "[%3.0f%%] %s\n", mejor_d->lineas[i].confianza * 100, mejor_d->lineas[i].texto);
        fprintf(stderr, "--- fin texto OCR ---\n");
    }
    r->c = mejor;
    r->c.calidad_imagen = mejor_conf;
    /* la confianza del OCR acota la de los campos leídos por OCR */
    ocrx_campo_meta *ms[] = {&r->c.m_tipo, &r->c.m_serie, &r->c.m_numero, &r->c.m_fecha, &r->c.emisor.m_ruc, &r->c.emisor.m_razon_social, &r->c.cliente.m_ruc, &r->c.cliente.m_razon_social, &r->c.totales.m_total, &r->c.totales.m_igv, &r->c.totales.m_gravada};
    for (size_t i = 0; i < sizeof ms / sizeof *ms; i++) if (ms[i]->origen == OCRX_ORIGEN_OCR && mejor_conf > 0 && mejor_conf < 0.75f) ms[i]->confianza *= (0.8f + mejor_conf * 0.25f);
    return true;
}

static double ahora_ms(void) {
#ifdef _WIN32
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

static ocrx_resultado *resultado_nuevo(void) {
    ocrx_resultado *r = (ocrx_resultado *)calloc(1, sizeof *r);
    if (!r) return NULL;
    arena_init(&r->a, 256 * 1024);
    return r;
}
void ocrx_resultado_liberar(ocrx_resultado *r) { if (!r) return; arena_liberar(&r->a); free(r); }
const ocrx_comprobante *ocrx_resultado_comprobante(const ocrx_resultado *r) { return r ? &r->c : NULL; }

/* ---------------- consolidación final ---------------- */
static void agregar_motivo(ocrx_comprobante *c, const char *m) {
    c->requiere_revision = true;
    char nuevo[OCRX_MAX_LEN * 2 + 400];
    if (c->motivo_revision[0]) snprintf(nuevo, sizeof nuevo, "%s; %s", c->motivo_revision, m);
    else snprintf(nuevo, sizeof nuevo, "%s", m);
    str_copiar(c->motivo_revision, OCRX_MAX_LEN, nuevo);
}

static void finalizar(ocrx_motor *m, ocrx_comprobante *c) {
    float umbral = m->cfg.umbral_revision;
    /* Tipo por serie si falta */
    if (c->tipo == OCRX_CPE_DESCONOCIDO && c->serie[0]) c->tipo = sunat_tipo_desde_serie(c->serie, OCRX_CPE_DESCONOCIDO);
    if (c->moneda == OCRX_MON_DESCONOCIDA) c->moneda = OCRX_MON_PEN;

    /* Monto en letras */
    double letras = 0; bool hay_letras = false; ocrx_moneda mon_letras = OCRX_MON_DESCONOCIDA;
    if (c->totales.monto_en_letras[0] && sunat_letras_a_monto(c->totales.monto_en_letras, &letras, &mon_letras)) {
        hay_letras = letras > 0;
        if (mon_letras != OCRX_MON_DESCONOCIDA && c->m_moneda.origen != OCRX_ORIGEN_XML) { if (c->moneda != mon_letras) c->m_moneda.corregido = true; c->moneda = mon_letras; c->m_moneda.presente = true; c->m_moneda.confianza = 0.95f; c->m_moneda.origen = OCRX_ORIGEN_LETRAS; }
    }

    /* Totales: con XML no se tocan; con QR el total e IGV son fijos; el resto se completa */
    bool xml = c->tenia_xml;
    if (!xml) {
        if (hay_letras && c->totales.m_total.origen == OCRX_ORIGEN_QR && !sunat_monto_igual(letras, c->totales.total)) {
            /* QR manda, pero dejar aviso: puede ser que las letras sean de otra página */
            agregar_motivo(c, "monto en letras no coincide con el total del QR");
        }
        bool cuadra = sunat_totales_reparar(&c->totales, letras, hay_letras && c->totales.m_total.origen != OCRX_ORIGEN_QR);
        if (!cuadra && c->totales.total > 0 && (c->totales.gravada > 0 || c->totales.igv > 0)) agregar_motivo(c, "los totales no cuadran");
        /* Si cuadra y además coincide con el monto en letras, los montos quedan confirmados por dos vías independientes */
        if (cuadra && c->totales.gravada > 0 && c->totales.igv > 0) {
            float base = (hay_letras && sunat_monto_igual(letras, c->totales.total)) ? 0.97f : 0.9f;
            ocrx_campo_meta *mm[] = {&c->totales.m_total, &c->totales.m_igv, &c->totales.m_gravada};
            for (int i = 0; i < 3; i++) if (mm[i]->presente && mm[i]->origen != OCRX_ORIGEN_XML && mm[i]->origen != OCRX_ORIGEN_QR && mm[i]->confianza < base) { mm[i]->confianza = base; mm[i]->dudoso = false; }
        }
        if (c->totales.total <= 0) agregar_motivo(c, "sin importe total");
    } else {
        sunat_cuadrar(&c->totales);
    }
    if (c->totales.tasa_igv == 0 && c->totales.gravada > 0 && c->totales.igv > 0) c->totales.tasa_igv = floor(c->totales.igv / c->totales.gravada * 1000 + 0.5) / 1000;

    /* Validaciones de identidad */
    if (!c->emisor.ruc[0]) agregar_motivo(c, "sin RUC del emisor");
    else if (!sunat_ruc_valido(c->emisor.ruc)) agregar_motivo(c, "RUC del emisor invalido");
    if (c->cliente.ruc[0] && !sunat_ruc_valido(c->cliente.ruc)) agregar_motivo(c, "RUC del cliente invalido");
    if (c->cliente.ruc[0] && c->emisor.ruc[0] && !strcmp(c->cliente.ruc, c->emisor.ruc)) { c->cliente.ruc[0] = 0; c->cliente.num_doc[0] = 0; }
    if (!c->serie[0] || !c->numero[0]) agregar_motivo(c, "sin serie/numero");
    else if (c->tipo != OCRX_CPE_DESCONOCIDO && c->tipo != OCRX_CPE_TICKET_MAQUINA && !sunat_serie_valida(c->serie, c->tipo)) agregar_motivo(c, "la serie no corresponde al tipo de comprobante");
    if (!c->fecha_emision[0]) agregar_motivo(c, "sin fecha de emision");
    else if (!sunat_fecha_razonable(c->fecha_emision)) agregar_motivo(c, "fecha de emision fuera de rango");
    if (c->tipo == OCRX_CPE_DESCONOCIDO) agregar_motivo(c, "tipo de comprobante no identificado");
    if (c->tipo == OCRX_CPE_FACTURA && !c->cliente.ruc[0]) agregar_motivo(c, "factura sin RUC del cliente");
    if ((c->tipo == OCRX_CPE_NOTA_CREDITO || c->tipo == OCRX_CPE_NOTA_DEBITO) && !c->referencia.serie[0]) agregar_motivo(c, "nota sin documento de referencia");
    if (c->es_manual && !c->es_electronico) agregar_motivo(c, "comprobante fisico/manual");
    if (c->totales.total >= 700 && c->tipo == OCRX_CPE_BOLETA && !c->cliente.num_doc[0]) agregar_motivo(c, "boleta >= S/ 700 sin documento del cliente");

    /* Confianzas bajas */
    ocrx_campo_meta *ms[] = {&c->m_tipo, &c->m_serie, &c->m_numero, &c->m_fecha, &c->emisor.m_ruc, &c->totales.m_total, &c->totales.m_igv, &c->totales.m_gravada};
    const char *nm[] = {"tipo", "serie", "numero", "fecha", "ruc emisor", "total", "igv", "gravada"};
    for (size_t i = 0; i < sizeof ms / sizeof *ms; i++) {
        if (ms[i]->presente && ms[i]->confianza < umbral) { ms[i]->dudoso = true; char b[64]; snprintf(b, sizeof b, "%s con baja confianza", nm[i]); agregar_motivo(c, b); }
        else if (ms[i]->dudoso) { char b[64]; snprintf(b, sizeof b, "%s dudoso", nm[i]); agregar_motivo(c, b); }
    }
    if (c->totales.m_total.corregido && c->totales.m_total.origen != OCRX_ORIGEN_LETRAS) agregar_motivo(c, "total corregido por cuadre");
    if (c->totales.m_gravada.corregido) agregar_motivo(c, "gravada corregida por cuadre");
    if (c->totales.m_igv.corregido) agregar_motivo(c, "igv corregido por cuadre");
}

/* ---------------- procesamiento ---------------- */
static ocrx_error procesar_xml(ocrx_motor *m, ocrx_resultado *r, const char *xml, size_t n) {
    (void)m;
    /* UTF-16 -> UTF-8 si hace falta */
    if (n >= 2 && (unsigned char)xml[0] == 0xFF && (unsigned char)xml[1] == 0xFE) {
        char *u8 = (char *)arena_alloc(&r->a, n * 2 + 4); size_t o = 0;
        for (size_t i = 2; i + 1 < n; i += 2) { uint32_t cp = (unsigned char)xml[i] | ((unsigned char)xml[i + 1] << 8); char b[4]; size_t k = utf8_codificar(cp, b); memcpy(u8 + o, b, k); o += k; }
        u8[o] = 0; xml = u8; n = o;
    }
    ocrx_error e = ubl_leer(xml, n, &r->c, &r->a);
    return e;
}

static ocrx_error procesar_pdf(ocrx_motor *m, ocrx_resultado *r, const void *datos, size_t n) {
    if (!pdf_disponible()) return OCRX_E_NO_DISPONIBLE;
    doc_texto *d = doc_texto_nuevo(&r->a);
    bool util = false;
    int paginas = pdf_texto(datos, n, d, &r->a, 10, &util);
    if (paginas < 0) return OCRX_E_PDF;
    r->c.paginas = paginas;
    /* QR: buscar en la primera y última página renderizadas a 200 dpi (suficiente para QR) */
    if (qr_disponible()) {
        int pags[2] = {1, paginas}; int np = paginas > 1 ? 2 : 1;
        for (int k = 0; k < np && !r->c.tenia_qr; k++) {
            pdf_imagen img; if (!pdf_render(datos, n, pags[k], 200, &img, &r->a)) continue;
            unsigned char *g = bgra_a_gris(img.pix, img.ancho, img.alto, img.stride, &r->a);
            char *textos[8]; size_t nq = qr_leer_gris(g, img.ancho, img.alto, img.ancho, textos, 8, &r->a);
            for (size_t q = 0; q < nq; q++) { if (!sunat_qr_parece(textos[q])) continue; sunat_qr sq = sunat_qr_parsear(textos[q]); if (sq.ok) { str_copiar(r->c.qr_crudo, 512, textos[q]); sunat_qr_aplicar(&sq, &r->c); break; } }
        }
    }
    if (util) extraer_desde_texto(d, &r->c, &r->a, OCRX_ORIGEN_PDF_TEXTO);
    else {
        /* PDF escaneado o con texto basura: OCR de la primera página (y la segunda si la primera no dio totales) */
        bool ok = false;
        int lim = paginas < 2 ? paginas : 2;
        for (int pg = 1; pg <= lim && !ok; pg++) {
            pdf_imagen img; if (!pdf_render(datos, n, pg, m->cfg.dpi_render, &img, &r->a)) break;
            imagen_gris g; memset(&g, 0, sizeof g);
            g.pix = bgra_a_gris(img.pix, img.ancho, img.alto, img.stride, &r->a); g.ancho = img.ancho; g.alto = img.alto; g.dpi = m->cfg.dpi_render;
            if (ocr_imagen_gris(m, r, &g)) ok = r->c.totales.total > 0 || r->c.emisor.ruc[0];
        }
        if (!ok && !tess_disponible()) { if (!r->c.tenia_qr) { snprintf(r->nota, sizeof r->nota, "PDF sin capa de texto util: OCR no compilado"); return OCRX_E_NO_DISPONIBLE; } snprintf(r->nota, sizeof r->nota, "PDF sin capa de texto util: datos solo del QR (OCR no compilado)"); }
        else if (!ok && m->tess_fallo) { snprintf(r->nota, sizeof r->nota, "OCR no disponible: %s", m->tess_error); if (!r->c.tenia_qr) return OCRX_E_NO_DISPONIBLE; }
        else snprintf(r->nota, sizeof r->nota, "PDF escaneado: texto por OCR%s", r->c.tenia_qr ? " + QR" : "");
    }
    return OCRX_OK;
}

static ocrx_error procesar_imagen(ocrx_motor *m, ocrx_resultado *r, const void *datos, size_t n) {
    (void)m;
    r->c.paginas = 1;
    if (qr_disponible()) {
        char *textos[8]; size_t nq = qr_leer_archivo(datos, n, textos, 8, &r->a);
        for (size_t q = 0; q < nq; q++) { if (!sunat_qr_parece(textos[q])) continue; sunat_qr sq = sunat_qr_parsear(textos[q]); if (sq.ok) { str_copiar(r->c.qr_crudo, 512, textos[q]); sunat_qr_aplicar(&sq, &r->c); break; } }
    }
    /* OCR del texto */
    imagen_gris g; char e[160];
    if (!imagen_cargar_gris(datos, n, &g, &r->a, e, sizeof e)) { snprintf(r->nota, sizeof r->nota, "imagen: %s", e); return r->c.tenia_qr ? OCRX_OK : OCRX_E_IMAGEN; }
    bool ok = ocr_imagen_gris(m, r, &g);
    if (!ok) {
        if (!tess_disponible()) { snprintf(r->nota, sizeof r->nota, "imagen: OCR no compilado%s", r->c.tenia_qr ? "; datos del QR" : ""); }
        else snprintf(r->nota, sizeof r->nota, "imagen: OCR no disponible: %s", m->tess_error[0] ? m->tess_error : "sin texto reconocible");
        return r->c.tenia_qr ? OCRX_OK : OCRX_E_NO_DISPONIBLE;
    }
    snprintf(r->nota, sizeof r->nota, "imagen: texto por OCR (confianza %.0f%%)%s", r->c.calidad_imagen * 100, r->c.tenia_qr ? " + QR" : "");
    return OCRX_OK;
}

ocrx_resultado *ocrx_procesar_memoria(ocrx_motor *m, const void *datos, size_t n, const char *pista, ocrx_error *err) {
    ocrx_error e = OCRX_OK;
    if (!m || !datos || !n) { if (err) *err = OCRX_E_PARAM; return NULL; }
    ocrx_resultado *r = resultado_nuevo();
    if (!r) { if (err) *err = OCRX_E_MEMORIA; return NULL; }
    double t0 = ahora_ms();
    formato f = formato_detectar(datos, n, pista);
    r->fmt = f;
    switch (f) {
    case FMT_XML: e = procesar_xml(m, r, (const char *)datos, n); break;
    case FMT_ZIP: {
        size_t xn = 0; char nombre[256] = {0};
        char *xml = zipxml_extraer(datos, n, &xn, &r->a, nombre);
        if (!xml) { e = OCRX_E_XML; snprintf(r->nota, sizeof r->nota, "ZIP sin XML de comprobante"); break; }
        e = procesar_xml(m, r, xml, xn);
        break; }
    case FMT_PDF: e = procesar_pdf(m, r, datos, n); break;
    case FMT_TEXTO: {
        char *t = arena_strndup(&r->a, (const char *)datos, n);
        /* ¿es el texto de un QR pegado? */
        if (sunat_qr_parece(t) && strlen(t) < 600 && !strchr(t, '\n')) { sunat_qr q = sunat_qr_parsear(t); if (q.ok) { str_copiar(r->c.qr_crudo, 512, t); sunat_qr_aplicar(&q, &r->c); break; } }
        doc_texto *d = doc_texto_desde_plano(t, &r->a);
        extraer_desde_texto(d, &r->c, &r->a, OCRX_ORIGEN_OCR);
        break; }
    case FMT_JSON: e = OCRX_E_FORMATO; break;
    default:
        if (formato_es_imagen(f)) e = procesar_imagen(m, r, datos, n);
        else e = OCRX_E_FORMATO;
    }
    if (e == OCRX_OK || (e == OCRX_E_NO_DISPONIBLE && (r->c.tenia_qr || r->c.emisor.ruc[0]))) {
        finalizar(m, &r->c);
        if (e == OCRX_E_NO_DISPONIBLE) { agregar_motivo(&r->c, r->nota); e = OCRX_OK; }
        if (!r->c.emisor.ruc[0] && !r->c.totales.total && !r->c.serie[0]) e = OCRX_E_SIN_DATOS;
    }
    r->c.ms_total = ahora_ms() - t0;
    if (err) *err = e;
    if (e != OCRX_OK && e != OCRX_E_SIN_DATOS) { /* devolver igual el resultado con nota, para que el JSON explique */ }
    return r;
}

ocrx_resultado *ocrx_procesar_archivo(ocrx_motor *m, const char *ruta, ocrx_error *err) {
    FILE *f = fopen(ruta, "rb");
    if (!f) { if (err) *err = OCRX_E_ARCHIVO; return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 200L * 1024 * 1024) { fclose(f); if (err) *err = OCRX_E_ARCHIVO; return NULL; }
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); if (err) *err = OCRX_E_MEMORIA; return NULL; }
    size_t leidos = fread(buf, 1, (size_t)sz, f); fclose(f);
    ocrx_resultado *r = ocrx_procesar_memoria(m, buf, leidos, ruta, err);
    free(buf);
    return r;
}

ocrx_resultado *ocrx_procesar_texto(ocrx_motor *m, const char *texto, ocrx_error *err) {
    if (!texto) { if (err) *err = OCRX_E_PARAM; return NULL; }
    return ocrx_procesar_memoria(m, texto, strlen(texto), "texto.txt", err);
}

float ocrx_calidad_imagen(ocrx_motor *m, const void *datos, size_t n, char *motivo, size_t cap) {
    (void)m; (void)datos; (void)n;
    if (motivo && cap) str_copiar(motivo, cap, "analisis de calidad disponible en fase 2");
    return -1.0f;
}

/* ---------------- JSON ---------------- */
static const char *origen_txt(ocrx_origen o) {
    switch (o) { case OCRX_ORIGEN_XML: return "xml"; case OCRX_ORIGEN_QR: return "qr"; case OCRX_ORIGEN_PDF_TEXTO: return "pdf_texto"; case OCRX_ORIGEN_OCR: return "ocr"; case OCRX_ORIGEN_PADRON: return "padron"; case OCRX_ORIGEN_CALCULO: return "calculo"; case OCRX_ORIGEN_LETRAS: return "letras"; default: return "ninguno"; }
}
static void meta_json(jw *w, const char *clave, const ocrx_campo_meta *m) {
    if (!m->presente) return;
    jw_obj_abrir(w, clave);
    jw_str(w, "origen", origen_txt(m->origen));
    jw_num(w, "confianza", floor(m->confianza * 100 + 0.5) / 100);
    if (m->corregido) jw_bool(w, "corregido", true);
    if (m->dudoso) jw_bool(w, "dudoso", true);
    jw_obj_cerrar(w);
}
static const char *estado_ruc_txt(ocrx_estado_ruc e) { switch (e) { case OCRX_RUC_ACTIVO: return "ACTIVO"; case OCRX_RUC_BAJA: return "BAJA"; case OCRX_RUC_SUSPENDIDO: return "SUSPENDIDO"; case OCRX_RUC_NO_HALLADO: return "NO_HALLADO"; default: return NULL; } }

static void parte_json(jw *w, const char *clave, const ocrx_parte *p) {
    jw_obj_abrir(w, clave);
    jw_str_o_omitir(w, "ruc", p->ruc);
    if (p->num_doc[0]) { jw_str(w, "tipoDocumento", sunat_tipo_doc_codigo(p->tipo_doc)); jw_str(w, "numeroDocumento", p->num_doc); }
    jw_str_o_omitir(w, "razonSocial", p->razon_social);
    jw_str_o_omitir(w, "nombreComercial", p->nombre_comercial);
    jw_str_o_omitir(w, "direccion", p->direccion);
    jw_str_o_omitir(w, "ubigeo", p->ubigeo);
    jw_str_o_omitir(w, "estadoRuc", estado_ruc_txt(p->estado_ruc));
    jw_str_o_omitir(w, "condicionRuc", p->condicion_ruc);
    jw_obj_cerrar(w);
}

const char *ocrx_resultado_json(ocrx_resultado *r, bool bonito) {
    if (!r) return "{}";
    if (r->json && r->json_bonito == bonito) return r->json;
    const ocrx_comprobante *c = &r->c;
    jw w; jw_init(&w, &r->a, bonito);
    jw_obj_abrir(&w, NULL);
    jw_str(&w, "version", OCRX_VERSION);
    jw_str(&w, "formatoEntrada", formato_nombre(r->fmt));
    jw_str(&w, "tipo", sunat_tipo_codigo(c->tipo));
    jw_str(&w, "tipoNombre", sunat_tipo_nombre(c->tipo));
    jw_str_o_omitir(&w, "serie", c->serie);
    jw_str_o_omitir(&w, "numero", c->numero);
    if (c->serie[0] && c->numero[0]) { char id[32]; snprintf(id, sizeof id, "%s-%08ld", c->serie, atol(c->numero)); jw_str(&w, "serieNumero", id); }
    jw_str_o_omitir(&w, "fechaEmision", c->fecha_emision);
    jw_str_o_omitir(&w, "horaEmision", c->hora_emision);
    jw_str_o_omitir(&w, "fechaVencimiento", c->fecha_vencimiento);
    jw_str(&w, "moneda", sunat_moneda_codigo(c->moneda));
    jw_num_o_omitir(&w, "tipoCambio", c->tipo_cambio);
    jw_str_o_omitir(&w, "formaPago", c->forma_pago);
    jw_str_o_omitir(&w, "ordenCompra", c->orden_compra);
    jw_str_o_omitir(&w, "guiaRelacionada", c->guia_relacionada);
    parte_json(&w, "emisor", &c->emisor);
    parte_json(&w, "cliente", &c->cliente);
    const ocrx_totales *t = &c->totales;
    jw_obj_abrir(&w, "totales");
    jw_num(&w, "gravada", t->gravada); jw_num(&w, "exonerada", t->exonerada); jw_num(&w, "inafecta", t->inafecta);
    jw_num_o_omitir(&w, "exportacion", t->exportacion); jw_num_o_omitir(&w, "gratuita", t->gratuita);
    jw_num_o_omitir(&w, "descuentos", t->descuentos); jw_num_o_omitir(&w, "otrosCargos", t->otros_cargos);
    jw_num(&w, "igv", t->igv); jw_num_o_omitir(&w, "isc", t->isc); jw_num_o_omitir(&w, "icbper", t->icbper); jw_num_o_omitir(&w, "otrosTributos", t->otros_tributos);
    jw_num(&w, "total", t->total);
    if (t->tasa_igv > 0) jw_num(&w, "tasaIgv", floor(t->tasa_igv * 1000 + 0.5) / 1000);
    jw_bool(&w, "cuadra", t->cuadra);
    jw_str_o_omitir(&w, "montoEnLetras", t->monto_en_letras);
    if (t->detraccion_monto > 0 || t->detraccion_pct > 0) { jw_obj_abrir(&w, "detraccion"); jw_num(&w, "porcentaje", t->detraccion_pct); jw_num(&w, "monto", t->detraccion_monto); jw_str_o_omitir(&w, "codigo", t->detraccion_codigo); jw_str_o_omitir(&w, "cuenta", t->detraccion_cuenta); jw_obj_cerrar(&w); }
    jw_num_o_omitir(&w, "retencion", t->retencion_monto);
    jw_num_o_omitir(&w, "percepcion", t->percepcion_monto);
    jw_obj_cerrar(&w);
    if (c->n_items) {
        jw_arr_abrir(&w, "items");
        for (size_t i = 0; i < c->n_items; i++) {
            const ocrx_item *it = &c->items[i];
            jw_obj_abrir(&w, NULL);
            jw_num(&w, "cantidad", it->cantidad); jw_str(&w, "unidad", it->unidad); jw_str_o_omitir(&w, "codigo", it->codigo); jw_str(&w, "descripcion", it->descripcion);
            jw_num(&w, "precioUnitario", it->precio_unitario); jw_num_o_omitir(&w, "precioUnitarioConIgv", it->precio_unitario_con_igv);
            jw_num(&w, "valorVenta", it->valor_venta); jw_num(&w, "igv", it->igv); jw_num(&w, "importe", it->importe); jw_str_o_omitir(&w, "afectacion", it->afectacion);
            jw_obj_cerrar(&w);
        }
        jw_arr_cerrar(&w);
    }
    if (c->referencia.serie[0] || c->referencia.motivo[0]) {
        jw_obj_abrir(&w, "referencia");
        if (c->referencia.tipo) jw_str(&w, "tipo", sunat_tipo_codigo(c->referencia.tipo));
        jw_str_o_omitir(&w, "serie", c->referencia.serie); jw_str_o_omitir(&w, "numero", c->referencia.numero);
        jw_str_o_omitir(&w, "motivoCodigo", c->referencia.motivo_codigo); jw_str_o_omitir(&w, "motivo", c->referencia.motivo);
        jw_obj_cerrar(&w);
    }
    jw_obj_abrir(&w, "campos");
    meta_json(&w, "tipo", &c->m_tipo); meta_json(&w, "serie", &c->m_serie); meta_json(&w, "numero", &c->m_numero); meta_json(&w, "fechaEmision", &c->m_fecha); meta_json(&w, "moneda", &c->m_moneda);
    meta_json(&w, "emisor.ruc", &c->emisor.m_ruc); meta_json(&w, "emisor.razonSocial", &c->emisor.m_razon_social); meta_json(&w, "cliente.ruc", &c->cliente.m_ruc); meta_json(&w, "cliente.razonSocial", &c->cliente.m_razon_social);
    meta_json(&w, "totales.gravada", &t->m_gravada); meta_json(&w, "totales.igv", &t->m_igv); meta_json(&w, "totales.total", &t->m_total);
    jw_obj_cerrar(&w);
    jw_obj_abrir(&w, "diagnostico");
    jw_bool(&w, "electronico", c->es_electronico); jw_bool(&w, "manual", c->es_manual);
    jw_bool(&w, "teniaQr", c->tenia_qr); jw_bool(&w, "teniaXml", c->tenia_xml); jw_bool(&w, "teniaTextoPdf", c->tenia_texto_pdf);
    jw_int(&w, "paginas", c->paginas);
    jw_str_o_omitir(&w, "qr", c->qr_crudo);
    jw_str_o_omitir(&w, "hash", c->hash_cpe);
    jw_str_o_omitir(&w, "nota", r->nota);
    jw_num(&w, "ms", floor(c->ms_total * 10 + 0.5) / 10);
    jw_obj_cerrar(&w);
    jw_bool(&w, "requiereRevision", c->requiere_revision);
    jw_str_o_omitir(&w, "motivoRevision", c->motivo_revision);
    jw_obj_cerrar(&w);
    r->json = (char *)jw_fin(&w); r->json_bonito = bonito;
    return r->json;
}

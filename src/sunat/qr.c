#include "qr.h"
#include "sunat.h"
#include "../core/str.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

static void url_decode(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = {s[1], s[2], 0}; out[o++] = (char)strtol(h, NULL, 16); s += 2;
        } else if (*s == '+') out[o++] = ' ';
        else out[o++] = *s;
    }
    out[o] = 0;
}

bool sunat_qr_parece(const char *t) {
    if (!t) return false;
    int pipes = 0; for (const char *p = t; *p; p++) if (*p == '|') pipes++;
    if (pipes >= 5) return true;
    /* URL de consulta con parámetros ruc/serie/numero */
    if (str_contiene(t, "ruc=") && (str_contiene(t, "serie=") || str_contiene(t, "numero="))) return true;
    /* ruc + serie en texto plano separado por otros caracteres (raro) */
    return false;
}

/* Extrae un parámetro de query string (clave=valor&...) */
static bool param(const char *url, const char *clave, char *out, size_t cap) {
    const char *p = url;
    size_t lk = strlen(clave);
    while ((p = str_buscar(p, clave)) != NULL) {
        if ((p == url || p[-1] == '?' || p[-1] == '&') && p[lk] == '=') {
            const char *v = p + lk + 1; size_t o = 0;
            while (*v && *v != '&' && *v != '#' && o + 1 < cap) out[o++] = *v++;
            out[o] = 0; return true;
        }
        p += lk;
    }
    return false;
}

sunat_qr sunat_qr_parsear(const char *texto_in) {
    sunat_qr q = {0};
    if (!texto_in) return q;
    char texto[1024]; url_decode(texto_in, texto, sizeof texto);
    str_recortar(texto);

    char campos[16][160]; int n = 0;
    memset(campos, 0, sizeof campos);

    if (str_contiene(texto, "ruc=") || str_contiene(texto, "http")) {
        q.parece_url = true;
        char v[160];
        if (param(texto, "ruc", v, sizeof v)) { str_copiar(campos[0], 160, v); n = 1; }
        if (param(texto, "tipo", v, sizeof v) || param(texto, "tipoComprobante", v, sizeof v) || param(texto, "tipocomprobante", v, sizeof v) || param(texto, "tipo_comprobante", v, sizeof v)) str_copiar(campos[1], 160, v);
        if (param(texto, "serie", v, sizeof v)) str_copiar(campos[2], 160, v);
        if (param(texto, "numero", v, sizeof v) || param(texto, "correlativo", v, sizeof v) || param(texto, "nro", v, sizeof v)) str_copiar(campos[3], 160, v);
        if (param(texto, "igv", v, sizeof v)) str_copiar(campos[4], 160, v);
        if (param(texto, "total", v, sizeof v) || param(texto, "monto", v, sizeof v) || param(texto, "importe", v, sizeof v)) str_copiar(campos[5], 160, v);
        if (param(texto, "fecha", v, sizeof v) || param(texto, "fechaEmision", v, sizeof v) || param(texto, "fecha_emision", v, sizeof v)) str_copiar(campos[6], 160, v);
        if (param(texto, "tipoDocCliente", v, sizeof v) || param(texto, "tipodoc", v, sizeof v)) str_copiar(campos[7], 160, v);
        if (param(texto, "numDocCliente", v, sizeof v) || param(texto, "docCliente", v, sizeof v) || param(texto, "cliente", v, sizeof v)) str_copiar(campos[8], 160, v);
        n = 9;
        /* algunos facturadores ponen el texto SUNAT completo dentro de un parámetro; si hay pipes, parsear como texto */
        if (strchr(texto, '|')) { const char *pp = strchr(texto, '|'); while (pp > texto && pp[-1] != '=' && pp[-1] != '/') pp--; q.parece_url = false; n = 0; memset(campos, 0, sizeof campos);
            texto_in = pp; url_decode(texto_in, texto, sizeof texto); }
    }
    if (!q.parece_url) {
        const char *p = texto; n = 0;
        while (n < 16) {
            const char *s = strchr(p, '|');
            size_t l = s ? (size_t)(s - p) : strlen(p);
            if (l > 159) l = 159;
            memcpy(campos[n], p, l); campos[n][l] = 0; str_recortar(campos[n]); n++;
            if (!s) break;
            p = s + 1;
        }
    }
    q.campos = n;
    if (n < 4) return q;

    /* Tolerar un primer campo con prefijo basura (p.ej. "RUC:20123..."), o campos desplazados cuando el emisor
     * antepone algo. Heurística: el RUC es el primer campo con 11 dígitos válido. */
    int base = 0;
    for (int i = 0; i < n && i < 3; i++) {
        char r[12]; bool corr;
        if (sunat_ruc_corregir(campos[i], r, &corr)) { str_copiar(q.ruc, 12, r); base = i; break; }
    }
    if (!q.ruc[0]) { char d[64]; str_solo_digitos(campos[0], d, sizeof d); if (strlen(d) == 11) str_copiar(q.ruc, 12, d); }

    const char *f_tipo = campos[base + 1], *f_serie = campos[base + 2], *f_num = campos[base + 3];
    const char *f_igv = n > base + 4 ? campos[base + 4] : "", *f_total = n > base + 5 ? campos[base + 5] : "";
    const char *f_fecha = n > base + 6 ? campos[base + 6] : "", *f_tdoc = n > base + 7 ? campos[base + 7] : "", *f_ndoc = n > base + 8 ? campos[base + 8] : "";

    q.tipo = sunat_tipo_desde_codigo(f_tipo);
    /* serie y número */
    char ser[8] = {0}, num[12] = {0}; bool corr = false;
    char junto[400]; snprintf(junto, sizeof junto, "%s-%s", f_serie, f_num);
    if (sunat_serie_numero_parsear(junto, q.tipo, ser, num, &corr)) { str_copiar(q.serie, 8, ser); str_copiar(q.numero, 12, num); }
    else {
        /* serie suelta y número suelto */
        char s2[16]; str_normalizar(f_serie, s2, sizeof s2, true); str_copiar(q.serie, 8, s2);
        char d[32]; str_solo_digitos(f_num, d, sizeof d); const char *z = d; while (*z == '0' && z[1]) z++; str_copiar(q.numero, 12, z);
    }
    if (q.tipo == OCRX_CPE_DESCONOCIDO) q.tipo = sunat_tipo_desde_serie(q.serie, OCRX_CPE_DESCONOCIDO);

    sunat_monto m = sunat_monto_parsear(f_igv); if (m.ok) q.igv = m.valor;
    m = sunat_monto_parsear(f_total); if (m.ok) q.total = m.valor;
    /* algunos invierten igv/total: el total nunca es menor al igv */
    if (q.igv > q.total && q.total > 0) { double t = q.igv; q.igv = q.total; q.total = t; }

    char f[11];
    if (sunat_fecha_parsear(f_fecha, f)) str_copiar(q.fecha, 11, f);
    else {
        /* ¿fecha en otro campo? (campos desplazados) */
        for (int i = base + 4; i < n; i++) if (sunat_fecha_parsear(campos[i], f)) { str_copiar(q.fecha, 11, f); break; }
    }
    q.tipo_doc_cliente = sunat_tipo_doc_desde_codigo(f_tdoc);
    { char d[32]; str_normalizar(f_ndoc, d, sizeof d, true); str_copiar(q.num_doc_cliente, 32, d); }
    if (q.tipo_doc_cliente == OCRX_DOC_SIN && q.num_doc_cliente[0]) q.tipo_doc_cliente = sunat_tipo_doc_por_forma(q.num_doc_cliente);
    if (n > base + 9 && campos[base + 9][0]) str_copiar(q.hash, 64, campos[base + 9]);

    q.ok = q.ruc[0] && ((q.serie[0] && q.numero[0]) || q.total > 0);
    return q;
}

static void meta_qr(ocrx_campo_meta *m) { m->origen = OCRX_ORIGEN_QR; m->confianza = 0.99f; m->presente = true; m->dudoso = false; }

void sunat_qr_aplicar(const sunat_qr *q, ocrx_comprobante *c) {
    if (!q->ok) return;
    c->tenia_qr = true; c->es_electronico = true;
    if (q->ruc[0]) { str_copiar(c->emisor.ruc, 12, q->ruc); meta_qr(&c->emisor.m_ruc); }
    if (q->tipo) { c->tipo = q->tipo; meta_qr(&c->m_tipo); }
    if (q->serie[0]) { str_copiar(c->serie, 8, q->serie); meta_qr(&c->m_serie); }
    if (q->numero[0]) { str_copiar(c->numero, 12, q->numero); meta_qr(&c->m_numero); }
    if (q->fecha[0]) { str_copiar(c->fecha_emision, 11, q->fecha); meta_qr(&c->m_fecha); }
    if (q->total > 0) { c->totales.total = q->total; meta_qr(&c->totales.m_total); }
    if (q->igv >= 0 && (q->total > 0)) { c->totales.igv = q->igv; meta_qr(&c->totales.m_igv); }
    if (q->num_doc_cliente[0]) {
        str_copiar(c->cliente.num_doc, 32, q->num_doc_cliente);
        c->cliente.tipo_doc = q->tipo_doc_cliente;
        if (q->tipo_doc_cliente == OCRX_DOC_RUC) { str_copiar(c->cliente.ruc, 12, q->num_doc_cliente); meta_qr(&c->cliente.m_ruc); }
    }
    if (q->hash[0]) str_copiar(c->hash_cpe, 64, q->hash);
}

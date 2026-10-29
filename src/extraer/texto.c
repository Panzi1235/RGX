#include "texto.h"
#include "../sunat/sunat.h"
#include "../core/str.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

int extraer_depurar = 0;
/* ============================ doc_texto ============================ */

doc_texto *doc_texto_nuevo(arena *a) { doc_texto *d = (doc_texto *)arena_alloc(a, sizeof *d); return d; }

void doc_texto_agregar(doc_texto *d, arena *a, const char *texto, float x0, float y0, float x1, float y1, int pagina, float conf) {
    if (!texto) return;
    if (d->n + 1 > d->cap) {
        size_t nc = d->cap ? d->cap * 2 : 64;
        d->lineas = (doc_linea *)arena_realloc(a, d->lineas, d->cap * sizeof(doc_linea), nc * sizeof(doc_linea));
        d->cap = nc;
    }
    doc_linea *l = &d->lineas[d->n++];
    l->texto = arena_strdup(a, texto);
    str_recortar(l->texto); str_colapsar_espacios(l->texto);
    size_t cap = strlen(l->texto) * 2 + 16;
    l->norm = (char *)arena_alloc(a, cap);
    str_normalizar(l->texto, l->norm, cap, true);
    l->x0 = x0; l->y0 = y0; l->x1 = x1; l->y1 = y1; l->pagina = pagina; l->confianza = conf;
    if (x1 > x0 || y1 > y0) d->con_coordenadas = true;
    if (l->texto[0] == 0) d->n--;  /* descartar vacías */
}

doc_texto *doc_texto_desde_plano(const char *texto, arena *a) {
    doc_texto *d = doc_texto_nuevo(a);
    if (!texto) return d;
    const char *p = texto; int pag = 1;
    while (*p) {
        const char *e = p; while (*e && *e != '\n' && *e != '\r') e++;
        if (*p == '\f') { pag++; p++; continue; }
        char *l = arena_strndup(a, p, (size_t)(e - p));
        doc_texto_agregar(d, a, l, 0, 0, 0, 0, pag, 1.0f);
        if (*e == '\r' && e[1] == '\n') e++;
        if (*e) e++;
        p = e;
    }
    return d;
}

static int cmp_linea(const void *pa, const void *pb) {
    const doc_linea *a = (const doc_linea *)pa, *b = (const doc_linea *)pb;
    if (a->pagina != b->pagina) return a->pagina - b->pagina;
    float ha = a->y1 - a->y0, hb = b->y1 - b->y0;
    float tol = (ha + hb) * 0.5f * 0.6f; if (tol < 1) tol = 1;
    float cya = (a->y0 + a->y1) * 0.5f, cyb = (b->y0 + b->y1) * 0.5f;
    if (fabsf(cya - cyb) > tol) return cya < cyb ? -1 : 1;
    return a->x0 < b->x0 ? -1 : (a->x0 > b->x0 ? 1 : 0);
}

void doc_texto_ordenar(doc_texto *d, arena *a) {
    if (!d->con_coordenadas || d->n < 2) return;
    qsort(d->lineas, d->n, sizeof(doc_linea), cmp_linea);
    /* Unir fragmentos de la misma fila separados por un hueco pequeño; conservar separados (" | ") si el hueco es grande,
     * así "IGV ........ 18.00" sigue siendo una línea lógica y las columnas se distinguen. */
    doc_linea *out = (doc_linea *)arena_alloc(a, d->n * sizeof(doc_linea));
    size_t m = 0;
    for (size_t i = 0; i < d->n; i++) {
        doc_linea *l = &d->lineas[i];
        if (m > 0) {
            doc_linea *u = &out[m - 1];
            float hu = u->y1 - u->y0, hl = l->y1 - l->y0; float tol = (hu + hl) * 0.5f * 0.6f; if (tol < 1) tol = 1;
            float cyu = (u->y0 + u->y1) * 0.5f, cyl = (l->y0 + l->y1) * 0.5f;
            if (u->pagina == l->pagina && fabsf(cyu - cyl) <= tol && l->x0 >= u->x0) {
                float hueco = l->x0 - u->x1;
                float alto = hu > 1 ? hu : 10;
                const char *sep = hueco > alto * 2.5f ? "   " : " ";
                size_t n1 = strlen(u->texto), n2 = strlen(l->texto);
                char *t = (char *)arena_alloc(a, n1 + n2 + 8);
                memcpy(t, u->texto, n1); strcpy(t + n1, sep); strcat(t, l->texto);
                u->texto = t;
                size_t cap = strlen(t) * 2 + 16; u->norm = (char *)arena_alloc(a, cap); str_normalizar(t, u->norm, cap, true);
                if (l->x1 > u->x1) u->x1 = l->x1;
                if (l->y0 < u->y0) u->y0 = l->y0;
                if (l->y1 > u->y1) u->y1 = l->y1;
                if (l->confianza < u->confianza) u->confianza = l->confianza;
                continue;
            }
        }
        out[m++] = *l;
    }
    d->lineas = out; d->n = m; d->cap = m;
}

char *doc_texto_plano(const doc_texto *d, arena *a) {
    size_t tot = 1; for (size_t i = 0; i < d->n; i++) tot += strlen(d->lineas[i].texto) + 1;
    char *s = (char *)arena_alloc(a, tot); size_t o = 0;
    for (size_t i = 0; i < d->n; i++) { size_t n = strlen(d->lineas[i].texto); memcpy(s + o, d->lineas[i].texto, n); o += n; s[o++] = '\n'; }
    s[o] = 0; return s;
}

/* ============================ utilidades ============================ */

typedef struct { const char *etq; } etiqueta;

static bool linea_tiene_alguna(const char *norm, const char *const *etqs) {
    for (int i = 0; etqs[i]; i++) if (str_contiene(norm, etqs[i])) return true;
    return false;
}
/* devuelve puntero al final de la etiqueta más larga que aparezca en la línea, o NULL */
static const char *fin_etiqueta(const char *norm, const char *const *etqs) {
    const char *mejor = NULL; size_t lmejor = 0;
    for (int i = 0; etqs[i]; i++) {
        const char *f = str_buscar(norm, etqs[i]);
        if (f) { size_t l = strlen(etqs[i]); if (l > lmejor) { lmejor = l; mejor = f + l; } }
    }
    return mejor;
}
/* texto a la derecha de una etiqueta: salta ':', '.', espacios */
static const char *valor_tras(const char *fin) {
    if (!fin) return NULL;
    while (*fin == ' ' || *fin == ':' || *fin == '.' || *fin == '-' || *fin == '=' || *fin == ')' || *fin == '(') fin++;
    return fin;
}
/* "E./.R.L.", "S.A.C", "S A C", "EIRL" -> compara sin puntuacion: tokens finales EIRL/SAC/SA/SAA/SRL/SCRL */
static bool sufijo_legal_compacto(const char *n) {
    char c[600]; size_t o = 0; bool esp = true;
    for (const char *p = n; *p && o + 1 < sizeof c; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isdigit(ch)) { char l = ocr_a_letra((char)ch); c[o++] = l ? l : (char)ch; esp = false; }   /* E.1.R.L -> EIRL */
        else if (isalnum(ch)) { c[o++] = (char)ch; esp = false; }
        else if (ch == '/' || ch == '|' || ch == '!' || ch == '\\' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '(' || ch == ')') { c[o++] = 'I'; esp = false; }  /* E./.R.L., E.[.R.L. -> EIRL */
        else if (ch == ' ' && !esp) { c[o++] = ' '; esp = true; }
        /* puntos y comas: se ignoran (unen S.A.C. en SAC) */
    }
    c[o] = 0;
    static const char *tok[] = {"EIRL", "SAC", "SAA", "SRL", "SCRL", "SA", "LTDA", "SRLTDA", "EIRLTDA", NULL};
    /* "E. I. R. L." -> quitar espacios y mirar el final */
    { char sin[600]; size_t k = 0; for (const char *p = c; *p && k + 1 < sizeof sin; p++) if (*p != ' ') sin[k++] = *p; sin[k] = 0;
      for (int i = 0; tok[i]; i++) { size_t lt = strlen(tok[i]); if (k > lt + 2 && strcmp(sin + k - lt, tok[i]) == 0) {
          /* exigir que en la versión con espacios el sufijo esté partido (hay un espacio dentro de los últimos lt+3 chars) o sea palabra aparte */
          size_t L = strlen(c); for (size_t z = (L > lt + 4 ? L - lt - 4 : 0); z < L; z++) if (c[z] == ' ') return true;
      } } }
    for (int i = 0; tok[i]; i++) {
        size_t lt = strlen(tok[i]);
        const char *f = c;
        while ((f = strstr(f, tok[i])) != NULL) {
            bool ini = (f == c) || f[-1] == ' ';
            bool fin = f[lt] == 0 || f[lt] == ' ';
            if (ini && fin && f != c) return true;   /* no al inicio de la linea: "SA" suelto como primera palabra no cuenta */
            f += lt;
        }
    }
    return false;
}
static bool tiene_sufijo_legal(const char *n) {
    if (sufijo_legal_compacto(n)) return true;
    static const char *suf[] = {"S.A.C", "SAC", "S.A.", "S A C", "E.I.R.L", "EIRL", "S.R.L", "SRL", "S.A.A", "SOCIEDAD", "ASOCIACION", "EMPRESA", "CORPORACION", "COMPANIA", "INVERSIONES", "NEGOCIOS", "SERVICIOS", "COMERCIAL", "INDUSTRIA", "CONSORCIO", "COOPERATIVA", "UNIVERSIDAD", "CLINICA", "RESTAURANT", "HOTEL", "GRIFO", "ESTACION", "FARMACIA", "BOTICA", "MINIMARKET", "SUPERMERCADO", "S.C.R.L", "LTDA", "S.A", NULL};
    char tmp[600]; snprintf(tmp, sizeof tmp, " %s ", n);
    for (int i = 0; suf[i]; i++) {
        const char *f = str_buscar(tmp, suf[i]);
        if (!f) continue;
        char antes = f[-1]; char despues = f[strlen(suf[i])];
        if ((antes == ' ' || antes == ',' || antes == '.') && (despues == ' ' || despues == '.' || despues == ',' || despues == 0)) return true;
    }
    return false;
}
static bool es_titulo_documento(const char *n) {
    if (sunat_tipo_desde_titulo(n) == OCRX_CPE_DESCONOCIDO || strlen(n) >= 70) return false;
    /* es titulo si la palabra clave esta al inicio; "CORPORACION X SAC FACTURA ELECTRONICA" no lo es (nombre + titulo en la misma linea) */
    static const char *claves[] = {"FACTURA", "BOLETA", "NOTA DE", "NOTA CREDITO", "NOTA DEBITO", "GUIA", "TICKET", "COMPROBANTE", "RECIBO", "LIQUIDACION", NULL};
    for (int i = 0; claves[i]; i++) { const char *f = str_buscar(n, claves[i]); if (f && (size_t)(f - n) <= 3) return true; }
    return false;
}
static bool linea_es_ruido_cabecera(const char *n) {
    static const char *r[] = {"RUC", "R.U.C", "TELEF", "TELF", "TEL:", "CEL:", "CELULAR", "EMAIL", "E-MAIL", "CORREO", "WWW.", "HTTP", "@", "DIRECCION", "DIRECC", "AV.", "AVENIDA", "JR.", "JIRON", "CALLE", "CAL.", "MZ", "LOTE", "URB.", "PSJE", "PASAJE", "KM.", "CARRETERA", "SUCURSAL", "LOCAL", "ESTABLECIMIENTO", "FECHA", "HORA", "CAJERO", "CAJA", "TICKET", "MESA", "ATENDIDO", "VENDEDOR", NULL};
    return linea_tiene_alguna(n, r);
}
static bool parece_direccion(const char *n) {
    static const char *r[] = {"AV.", "AV ", "AVENIDA", "JR.", "JR ", "JIRON", "CALLE", "CAL.", "CA.", "MZ", "LOTE", "LT.", "URB.", "URB ", "PSJE", "PASAJE", "KM.", "KM ", "CARRETERA", "CDRA", "NRO.", "INT.", "DPTO", "OF.", "OFICINA", "PISO", "LIMA", "CALLAO", "AREQUIPA", "TRUJILLO", "CHICLAYO", "PIURA", "CUSCO", "HUANCAYO", "IQUITOS", "TACNA", "ICA", "PUNO", "JUNIN", "ANCASH", "LAMBAYEQUE", "LA LIBERTAD", "MIRAFLORES", "SURCO", "SAN ISIDRO", "SAN BORJA", "LA MOLINA", "SAN MIGUEL", "SAN JUAN", "LOS OLIVOS", "ATE", "COMAS", "CHORRILLOS", "BARRANCO", "LINCE", "JESUS MARIA", "PUEBLO LIBRE", "MAGDALENA", "BRENA", "RIMAC", "CERCADO", "VILLA EL SALVADOR", "SANTA ANITA", "CARABAYLLO", "INDEPENDENCIA", "PUENTE PIEDRA", "VENTANILLA", NULL};
    return linea_tiene_alguna(n, r);
}

static void poner_meta(ocrx_campo_meta *m, ocrx_origen o, float conf) {
    if (m->origen == OCRX_ORIGEN_XML || m->origen == OCRX_ORIGEN_QR) return; /* no pisar datos seguros */
    m->origen = o; m->confianza = conf; m->presente = true;
}
static bool es_seguro(const ocrx_campo_meta *m) { return m->origen == OCRX_ORIGEN_XML || m->origen == OCRX_ORIGEN_QR; }

/* ============================ extractor ============================ */

typedef struct {
    const doc_texto *d; ocrx_comprobante *c; arena *a; ocrx_origen origen;
    int idx_titulo, idx_ruc_emisor, idx_cliente, idx_totales_inicio, idx_items_inicio;
} ctx;

static const char *ETQ_CLIENTE[] = {"SENOR(ES)", "SENORES", "SENOR", "SEFIOR", "SEN0R", "SEÑOR", "SERIOR", "SEHOR", "SR.(ES)", "SR(ES)", "SRES.", "SRES", "SR.", "CLIENTE", "ADQUIRIENTE", "ADQUIRENTE", "RAZON SOCIAL", "NOMBRE O RAZON", "NOMBRE", "DENOMINACION", "USUARIO", "DESTINATARIO", "FACTURAR A", "COMPRADOR", NULL};
static const char *ETQ_RUC_CLIENTE[] = {"RUC CLIENTE", "RUC DEL CLIENTE", "RUC / DNI", "RUC/DNI", "DNI/RUC", "DOC. IDENTIDAD", "DOCUMENTO DE IDENTIDAD", "DOC IDENTIDAD", "N° DOC", "NRO. DOC", "NRO DOC", "DNI", "CE:", "C.E.", "CARNET", "PASAPORTE", "IDENTIFICACION", NULL};
static const char *ETQ_FECHA[] = {"FECHA DE EMISION", "FECHA EMISION", "FECHA DE EMIS", "F. EMISION", "F.EMISION", "FEC. EMISION", "FEC EMISION", "FEC.EMISION", "FECHA EMIS", "EMISION", "FECHA Y HORA", "FECHA/HORA", "FECHA:", "FECHA", "F. DE EMISION", "FEC.", "EMITIDO", NULL};
static const char *ETQ_VENC[] = {"FECHA DE VENCIMIENTO", "FECHA VENCIMIENTO", "F. VENCIMIENTO", "F.VENC", "FEC. VENC", "VENCIMIENTO", "VENCE", "F. VCTO", "VCTO", NULL};
static const char *ETQ_MONEDA[] = {"MONEDA", "TIPO DE MONEDA", "TIPO MONEDA", "DIVISA", NULL};
static const char *ETQ_FORMA_PAGO[] = {"FORMA DE PAGO", "FORMA PAGO", "CONDICION DE PAGO", "COND. PAGO", "COND. DE PAGO", "CONDICIONES DE PAGO", "MEDIO DE PAGO", "TIPO DE PAGO", "PAGO:", NULL};
static const char *ETQ_OC[] = {"ORDEN DE COMPRA", "O/C", "O.C.", "OC:", "OC N", "NRO. OC", "ORDEN COMPRA", "N° PEDIDO", "PEDIDO", NULL};
static const char *ETQ_GUIA[] = {"GUIA DE REMISION", "GUIA REMISION", "GUIA:", "G/R", "G.R.", NULL};
static const char *ETQ_TC[] = {"TIPO DE CAMBIO", "T.C.", "T/C", "TC:", "TIPO CAMBIO", NULL};
static const char *ETQ_HASH[] = {"HASH", "CODIGO HASH", "RESUMEN", "VALOR RESUMEN", "DIGEST", "FIRMA DIGITAL", NULL};

static const char *ETQ_GRAVADA[] = {"OPERACIONES GRAVADAS", "OPERACION GRAVADA", "OP. GRAVADAS", "OP. GRAVADA", "OP GRAVADAS", "OP GRAVADA", "TOTAL GRAVADA", "TOTAL GRAVADO", "GRAVADA", "GRAVADO", "VALOR DE VENTA", "VALOR VENTA", "TOTAL VALOR VENTA", "BASE IMPONIBLE", "SUB TOTAL", "SUBTOTAL", "SUB-TOTAL", "SUB.TOTAL", "TOTAL NETO", "NETO", "TOTAL AFECTO", "AFECTO", "VENTA NETA", "T. GRAVADA", NULL};
static const char *ETQ_EXONERADA[] = {"OPERACIONES EXONERADAS", "OPERACION EXONERADA", "OP. EXONERADAS", "OP. EXONERADA", "OP EXONERADA", "TOTAL EXONERADA", "EXONERADA", "EXONERADO", "EXONERADAS", "T. EXONERADA", NULL};
static const char *ETQ_INAFECTA[] = {"OPERACIONES INAFECTAS", "OPERACION INAFECTA", "OP. INAFECTAS", "OP. INAFECTA", "OP INAFECTA", "TOTAL INAFECTA", "INAFECTA", "INAFECTO", "INAFECTAS", "T. INAFECTA", "NO GRAVADO", "NO GRAVADA", NULL};
static const char *ETQ_GRATUITA[] = {"GRATUITA", "GRATUITAS", "TRANSFERENCIA GRATUITA", "OP. GRATUITAS", NULL};
static const char *ETQ_EXPORT[] = {"EXPORTACION", "EXPORTACIONES", "OP. EXPORTACION", NULL};
static const char *ETQ_DESCUENTO[] = {"DESCUENTO", "DESCUENTOS", "DSCTO", "DSCTOS", "DCTO", "DESC.", "DESC ", "TOTAL DESCUENTOS", "DESCUENTO GLOBAL", NULL};
static const char *ETQ_IGV[] = {"I.G.V.", "I.G.V", "IGV", "I G V", "LGV", "L.G.V", "1GV", "IG V", "I6V", "IGY", "IMPUESTO GENERAL", "IMPUESTO", "TOTAL IGV", "TOTAL IMPUESTOS", "IVA", NULL};
static const char *ETQ_ISC[] = {"I.S.C.", "I.S.C", "ISC", "IMPUESTO SELECTIVO", NULL};
static const char *ETQ_ICBPER[] = {"ICBPER", "ICBP", "IMPUESTO A LA BOLSA", "IMP. BOLSA", "IMP BOLSA", "BOLSAS PLASTICAS", "BOLSA PLASTICA", NULL};
static const char *ETQ_OTROS_CARGOS[] = {"OTROS CARGOS", "RECARGO AL CONSUMO", "RECARGO CONSUMO", "RECARGO", "PROPINA", "SERVICIO 10%", "CARGO POR SERVICIO", "REDONDEO", "OTROS TRIBUTOS", NULL};
static const char *ETQ_TOTAL[] = {"IMPORTE TOTAL", "TOTAL A PAGAR", "TOTAL PAGAR", "MONTO TOTAL", "TOTAL VENTA", "TOTAL VENTAS", "PRECIO VENTA", "PRECIO DE VENTA", "TOTAL GENERAL", "TOTAL S/", "TOTAL US$", "TOTAL $", "TOTAL:", "TOTAL", "IMPORTE A PAGAR", "TOTAL IMPORTE", "TOTAL COMPROBANTE", "TOTAL FACTURA", "TOTAL BOLETA", "TOTAL NOTA", "TOTAL DOCUMENTO", "TOTAL A COBRAR", "T O T A L", "VALOR TOTAL", "TOTAL CON IGV", "TOTAL INC. IGV", "TOTAL INCLUIDO IGV", NULL};
static const char *ETQ_DETRACCION[] = {"DETRACCION", "DETRACC", "SPOT", "SUJETO A DETRACCION", "DETRAC.", NULL};
static const char *ETQ_RETENCION[] = {"RETENCION", "RETENC", "AGENTE DE RETENCION", "RET. IGV", NULL};
static const char *ETQ_PERCEPCION[] = {"PERCEPCION", "PERCEP", "AGENTE DE PERCEPCION", NULL};
static const char *ETQ_NO_TOTAL[] = {"GRAVAD", "EXONER", "INAFEC", "GRATUIT", "SUB", "DESCUENTO", "DSCTO", "IGV", "I.G.V", "ISC", "ICBPER", "LETRAS", "SON:", "ITEM", "CANT", "DESCRIP", "P.UNIT", "P. UNIT", "PRECIO UNIT", "VALOR UNIT", "DETRACCION", "RETENCION", "PERCEPCION", "ANTICIPO", "CUOTA", "ENTREGADO", "RECIBIDO", "EFECTIVO", "VUELTO", "CAMBIO", "PAGADO", "TARJETA", "VISA", "MASTERCARD", "YAPE", "PLIN", "PAGA CON", "UNIDADES", "ARTICULOS", "PRODUCTOS", "PUNTOS", "AHORRO", "RECARGO", "PROPINA", "OTROS CARGOS", "TOTAL DE ITEMS", "TOTAL ITEMS", "TOTAL ARTICULOS", "TOTAL UNIDADES", "ITEMS:", "HORA", "MESA", NULL};

static bool es_etiqueta_sola(const char *norm) {
    /* línea sin montos ni dígitos relevantes: la etiqueta está sola y el valor viene en la siguiente */
    return str_contar_digitos(norm) < 2;
}

/* obtiene monto a la derecha de la etiqueta, o en la línea siguiente si la etiqueta está sola */
static bool monto_para_etiqueta(ctx *x, size_t i, const char *const *etqs, double *valor, ocrx_moneda *mon, size_t *linea_usada) {
    const doc_linea *l = &x->d->lineas[i];
    const char *fin = fin_etiqueta(l->norm, etqs);
    if (!fin) return false;
    sunat_monto ms[32]; size_t n = sunat_montos_en_linea(l->norm, ms, 32);
    /* preferir el último monto que esté después de la etiqueta */
    if (n) {
        sunat_monto m = ms[n - 1];
        /* si la línea contiene % (p.ej. "IGV 18% 18.00"), el porcentaje ya fue filtrado por el parser de montos */
        *valor = m.valor; if (mon) *mon = m.moneda; if (linea_usada) *linea_usada = i; return true;
    }
    if (es_etiqueta_sola(l->norm)) {
        /* valor en la siguiente línea (o la subsiguiente si la siguiente también es etiqueta) */
        for (size_t k = i + 1; k < x->d->n && k <= i + 2; k++) {
            const doc_linea *s = &x->d->lineas[k];
            if (s->pagina != l->pagina) break;
            size_t nn = sunat_montos_en_linea(s->norm, ms, 32);
            if (nn == 1 && str_contar_digitos(s->norm) <= 12 && utf8_largo(s->norm) <= 20) { *valor = ms[0].valor; if (mon) *mon = ms[0].moneda; if (linea_usada) *linea_usada = k; return true; }
            if (nn >= 1 && !es_etiqueta_sola(s->norm) && str_buscar(s->norm, ":") == NULL && utf8_largo(s->norm) <= 16) { *valor = ms[nn - 1].valor; if (mon) *mon = ms[nn - 1].moneda; if (linea_usada) *linea_usada = k; return true; }
            if (!es_etiqueta_sola(s->norm)) break;
        }
    }
    return false;
}

/* Caso "columna de etiquetas" + "columna de valores" en bloques separados:
 *   OP. GRAVADA        (línea i)
 *   IGV                (línea i+1)
 *   TOTAL              (línea i+2)
 *   100.00             (línea i+3)
 *   18.00
 *   118.00
 * Lo resolvemos emparejando bloque de etiquetas consecutivas con bloque de montos consecutivos. */
static void totales_por_bloques(ctx *x) {
    const doc_texto *d = x->d; ocrx_totales *t = &x->c->totales;
    for (size_t i = 0; i + 1 < d->n; i++) {
        /* contar etiquetas consecutivas sin montos */
        size_t ne = 0; const char *const *tipos[16];
        for (size_t k = i; k < d->n && ne < 16; k++) {
            const char *n = d->lineas[k].norm;
            sunat_monto ms[4]; if (sunat_montos_en_linea(n, ms, 4) > 0) break;
            const char *const *tp = NULL;
            if (linea_tiene_alguna(n, ETQ_IGV)) tp = ETQ_IGV;
            else if (linea_tiene_alguna(n, ETQ_GRAVADA)) tp = ETQ_GRAVADA;
            else if (linea_tiene_alguna(n, ETQ_EXONERADA)) tp = ETQ_EXONERADA;
            else if (linea_tiene_alguna(n, ETQ_INAFECTA)) tp = ETQ_INAFECTA;
            else if (linea_tiene_alguna(n, ETQ_DESCUENTO)) tp = ETQ_DESCUENTO;
            else if (linea_tiene_alguna(n, ETQ_ICBPER)) tp = ETQ_ICBPER;
            else if (linea_tiene_alguna(n, ETQ_ISC)) tp = ETQ_ISC;
            else if (linea_tiene_alguna(n, ETQ_TOTAL) && !linea_tiene_alguna(n, ETQ_NO_TOTAL)) tp = ETQ_TOTAL;
            if (!tp) break;
            tipos[ne++] = tp;
        }
        if (ne < 2) continue;
        /* bloque de montos a continuación */
        size_t j = i + ne; size_t nm = 0; double vals[16];
        for (; j < d->n && nm < ne; j++) {
            sunat_monto ms[4]; size_t c = sunat_montos_en_linea(d->lineas[j].norm, ms, 4);
            if (c != 1 || utf8_largo(d->lineas[j].norm) > 20) break;
            vals[nm++] = ms[0].valor;
        }
        if (nm != ne) { i += ne - 1; continue; }
        for (size_t k = 0; k < ne; k++) {
            double v = vals[k];
            if (tipos[k] == ETQ_IGV && !es_seguro(&t->m_igv)) { t->igv = v; poner_meta(&t->m_igv, x->origen, 0.85f); }
            else if (tipos[k] == ETQ_GRAVADA && !es_seguro(&t->m_gravada)) { t->gravada = v; poner_meta(&t->m_gravada, x->origen, 0.85f); }
            else if (tipos[k] == ETQ_EXONERADA) { t->exonerada = v; poner_meta(&t->m_exonerada, x->origen, 0.85f); }
            else if (tipos[k] == ETQ_INAFECTA) { t->inafecta = v; poner_meta(&t->m_inafecta, x->origen, 0.85f); }
            else if (tipos[k] == ETQ_DESCUENTO) t->descuentos = fabs(v);
            else if (tipos[k] == ETQ_ICBPER) t->icbper = v;
            else if (tipos[k] == ETQ_ISC) t->isc = v;
            else if (tipos[k] == ETQ_TOTAL && !es_seguro(&t->m_total)) { t->total = v; poner_meta(&t->m_total, x->origen, 0.85f); }
        }
        i = j;
    }
}

static void extraer_tipo_y_titulo(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c;
    x->idx_titulo = -1;
    size_t lim = d->n < 40 ? d->n : 40;
    ocrx_tipo_cpe mejor = OCRX_CPE_DESCONOCIDO; int mejor_idx = -1;
    for (size_t i = 0; i < lim; i++) {
        const char *n = d->lineas[i].norm;
        ocrx_tipo_cpe t = sunat_tipo_desde_titulo(n);
        if (t == OCRX_CPE_DESCONOCIDO) continue;
        /* evitar "REPRESENTACION IMPRESA DE LA FACTURA ELECTRONICA" como título único, pero sirve de pista */
        bool leyenda = str_contiene(n, "REPRESENTACION IMPRESA") || str_contiene(n, "AUTORIZADO") || str_contiene(n, "RESOLUCION") || str_contiene(n, "CONSULTE");
        if (leyenda) { c->es_electronico = true; if (mejor == OCRX_CPE_DESCONOCIDO) mejor = t; continue; }
        /* notas de crédito referencian "FACTURA" en "DOCUMENTO QUE MODIFICA: FACTURA": priorizar NC/ND si aparecen */
        if (mejor_idx < 0 || t == OCRX_CPE_NOTA_CREDITO || t == OCRX_CPE_NOTA_DEBITO) { mejor = t; mejor_idx = (int)i; if (t == OCRX_CPE_NOTA_CREDITO || t == OCRX_CPE_NOTA_DEBITO) break; }
    }
    x->idx_titulo = mejor_idx;
    if (mejor != OCRX_CPE_DESCONOCIDO && !es_seguro(&c->m_tipo)) { c->tipo = mejor; poner_meta(&c->m_tipo, x->origen, mejor_idx >= 0 ? 0.9f : 0.7f); }
    for (size_t i = 0; i < d->n; i++) {
        const char *n = d->lineas[i].norm;
        if (str_contiene(n, "REPRESENTACION IMPRESA") || str_contiene(n, "COMPROBANTE ELECTRONICO") || str_contiene(n, "ELECTRONICA") || str_contiene(n, "CONSULTE EN") || str_contiene(n, "SUNAT.GOB.PE") || str_contiene(n, "HASH") || str_contiene(n, "CODIGO QR") || str_contiene(n, "BIENES TRANSFERIDOS")) c->es_electronico = true;
        if (str_contiene(n, "AUTORIZACION DE IMPRESION") || str_contiene(n, "IMPRENTA") || str_contiene(n, "AUT. SUNAT") || str_contiene(n, "AUTORIZACION N")) { if (!c->es_electronico) c->es_manual = true; }
    }
}

static void extraer_serie_numero(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c;
    if (es_seguro(&c->m_serie) && es_seguro(&c->m_numero)) return;
    /* 1) cerca del título */
    int ini = x->idx_titulo >= 0 ? x->idx_titulo : 0;
    int fin = ini + 8 < (int)d->n ? ini + 8 : (int)d->n;
    char ser[8], num[12]; bool corr;
    for (int pasada = 0; pasada < 2; pasada++) {
        int a = pasada == 0 ? ini : 0, b = pasada == 0 ? fin : (int)d->n;
        for (int i = a; i < b; i++) {
            const char *n = d->lineas[i].norm;
            /* descartar referencias de NC/ND y guías relacionadas en la primera pasada */
            if (pasada == 0 && (str_contiene(n, "MODIFICA") || str_contiene(n, "REFERENCIA") || str_contiene(n, "GUIA"))) continue;
            if (str_contiene(n, "RUC")) { /* "RUC: 20123456789  F001-123" sí puede traer serie */ }
            if (sunat_serie_numero_parsear(n, c->tipo, ser, num, &corr)) {
                /* validar contra el tipo; si el tipo es desconocido, deducirlo */
                ocrx_tipo_cpe t = sunat_tipo_desde_serie(ser, c->tipo);
                if (c->tipo != OCRX_CPE_DESCONOCIDO && !sunat_serie_valida(ser, c->tipo) && c->tipo != OCRX_CPE_TICKET_MAQUINA) {
                    /* serie que no corresponde al tipo (p.ej. la factura referenciada en una NC): seguir buscando */
                    if (pasada == 0) continue;
                }
                str_copiar(c->serie, 8, ser); str_copiar(c->numero, 12, num);
                poner_meta(&c->m_serie, x->origen, corr ? 0.75f : 0.9f); poner_meta(&c->m_numero, x->origen, corr ? 0.75f : 0.9f);
                c->m_serie.corregido = c->m_numero.corregido = corr;
                if (c->tipo == OCRX_CPE_DESCONOCIDO && t != OCRX_CPE_DESCONOCIDO) { c->tipo = t; poner_meta(&c->m_tipo, x->origen, 0.7f); }
                if (sunat_serie_es_electronica(ser)) c->es_electronico = true;
                return;
            }
        }
    }
}

static void extraer_rucs(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c;
    x->idx_ruc_emisor = -1; x->idx_cliente = -1;
    /* localizar inicio del bloque cliente */
    for (size_t i = 0; i < d->n; i++) {
        const char *n = d->lineas[i].norm;
        if ((str_empieza(n, "SENOR") || str_empieza(n, "SEFIOR") || str_empieza(n, "SERIOR") || str_empieza(n, "SEHOR") || str_empieza(n, "SR") || str_empieza(n, "CLIENTE") || str_empieza(n, "ADQUIR") || str_contiene(n, "DATOS DEL CLIENTE") || str_contiene(n, "DATOS DEL ADQUIR") || str_empieza(n, "RAZON SOCIAL") || str_empieza(n, "DESTINATARIO") || str_empieza(n, "NOMBRE")) && i > 0) { x->idx_cliente = (int)i; break; }
    }
    /* recolectar RUCs válidos con su línea */
    struct { char ruc[12]; int idx; bool etiqueta_cliente; bool corregido; } hall[16]; int nh = 0;
    for (size_t i = 0; i < d->n && nh < 16; i++) {
        const char *n = d->lineas[i].norm;
        /* candidatos: secuencias de 11 dígitos (o con confusiones) */
        for (const char *p = n; *p; p++) {
            if (!(isdigit((unsigned char)*p) || ocr_a_digito(*p))) continue;
            if (p > n && isalnum((unsigned char)p[-1])) continue;
            char tok[40]; int k = 0; const char *q = p;
            while (*q && k < 39 && (isdigit((unsigned char)*q) || ocr_a_digito(*q) || *q == ' ' || *q == '-' || *q == '.')) { tok[k++] = *q++; }
            tok[k] = 0;
            /* limpiar separadores internos */
            char lim[40]; int lk = 0; for (int z = 0; z < k && lk < 39; z++) if (tok[z] != ' ' && tok[z] != '-' && tok[z] != '.') lim[lk++] = tok[z]; lim[lk] = 0;
            size_t dig = strlen(lim);
            if (dig >= 11 && dig <= 13) {
                /* exigir que la línea mencione RUC o que sean 11 dígitos exactos y válidos */
                char r[12]; bool corr;
                bool cerca_ruc = str_contiene(n, "RUC") || str_contiene(n, "R.U.C");
                if (sunat_ruc_corregir(lim, r, &corr) && (cerca_ruc || (!corr && dig == 11))) {
                    bool dup = false; for (int h = 0; h < nh; h++) if (!strcmp(hall[h].ruc, r)) dup = true;
                    if (!dup) {
                        str_copiar(hall[nh].ruc, 12, r); hall[nh].idx = (int)i; hall[nh].corregido = corr;
                        hall[nh].etiqueta_cliente = linea_tiene_alguna(n, ETQ_RUC_CLIENTE) || linea_tiene_alguna(n, ETQ_CLIENTE) || (x->idx_cliente >= 0 && (int)i >= x->idx_cliente && (int)i < x->idx_cliente + 6);
                        nh++;
                    }
                }
            }
            p = q > p ? q - 1 : p;
        }
    }
    if (!nh) return;
    /* Emisor: RUC seguro del QR/XML si existe; si no, el primero que no tenga etiqueta de cliente */
    int ie = -1, ic = -1;
    if (c->emisor.ruc[0]) { for (int h = 0; h < nh; h++) if (!strcmp(hall[h].ruc, c->emisor.ruc)) ie = h; }
    if (ie < 0) { for (int h = 0; h < nh; h++) if (!hall[h].etiqueta_cliente) { ie = h; break; } }
    if (ie < 0) ie = 0;
    for (int h = 0; h < nh; h++) if (h != ie && strcmp(hall[h].ruc, hall[ie].ruc)) { ic = h; break; }
    /* si el "emisor" elegido está después del cliente y hay otro antes, intercambiar */
    if (ic >= 0 && hall[ic].idx < hall[ie].idx && !hall[ic].etiqueta_cliente && !c->emisor.m_ruc.presente) { int t = ie; ie = ic; ic = t; }
    x->idx_ruc_emisor = hall[ie].idx;
    if (!es_seguro(&c->emisor.m_ruc)) { str_copiar(c->emisor.ruc, 12, hall[ie].ruc); c->emisor.tipo_doc = OCRX_DOC_RUC; str_copiar(c->emisor.num_doc, 32, hall[ie].ruc); poner_meta(&c->emisor.m_ruc, x->origen, hall[ie].corregido ? 0.8f : 0.95f); c->emisor.m_ruc.corregido = hall[ie].corregido; }
    if (ic >= 0 && !es_seguro(&c->cliente.m_ruc)) { str_copiar(c->cliente.ruc, 12, hall[ic].ruc); c->cliente.tipo_doc = OCRX_DOC_RUC; str_copiar(c->cliente.num_doc, 32, hall[ic].ruc); poner_meta(&c->cliente.m_ruc, x->origen, hall[ic].corregido ? 0.8f : 0.95f); if (x->idx_cliente < 0) x->idx_cliente = hall[ic].idx; }
    /* DNI del cliente si no hay RUC de cliente */
    if (!c->cliente.num_doc[0]) {
        for (size_t i = 0; i < d->n; i++) {
            const char *n = d->lineas[i].norm;
            if (!linea_tiene_alguna(n, ETQ_RUC_CLIENTE) && !str_contiene(n, "DNI")) continue;
            const char *f = valor_tras(fin_etiqueta(n, ETQ_RUC_CLIENTE)); if (!f) continue;
            char dgt[32]; ocr_forzar_digitos(f, dgt, sizeof dgt);
            if (strlen(dgt) == 8) { str_copiar(c->cliente.num_doc, 32, dgt); c->cliente.tipo_doc = OCRX_DOC_DNI; break; }
            char alnum[32]; int k = 0; for (const char *p = f; *p && k < 31; p++) { if (isalnum((unsigned char)*p)) alnum[k++] = *p; else if (k) break; } alnum[k] = 0;
            if (k >= 6 && k <= 12) { str_copiar(c->cliente.num_doc, 32, alnum); c->cliente.tipo_doc = sunat_tipo_doc_por_forma(alnum); break; }
        }
    }
}

static void extraer_razon_social_emisor(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c;
    if (es_seguro(&c->emisor.m_razon_social) || c->emisor.razon_social[0]) return;
    int ref = x->idx_ruc_emisor >= 0 ? x->idx_ruc_emisor : (x->idx_titulo >= 0 ? x->idx_titulo : 0);
    int mejor = -1; float mejor_p = 0;
    int lim_sup = (int)d->n < 15 ? (int)d->n : 15;
    if (extraer_depurar) fprintf(stderr, "[emisor] ref=%d idx_ruc=%d idx_titulo=%d idx_cliente=%d lim=%d\n", ref, x->idx_ruc_emisor, x->idx_titulo, x->idx_cliente, lim_sup);
    for (int i = 0; i < lim_sup; i++) {
        if (x->idx_cliente >= 0 && i >= x->idx_cliente) break;
        const char *n = d->lineas[i].norm;
        const char *por = NULL;
        if (es_titulo_documento(n)) por = "titulo";
        else if (linea_tiene_alguna(n, ETQ_CLIENTE) && !(str_empieza(n, "RAZON SOCIAL") && (int)i < x->idx_ruc_emisor + 2)) por = "etiqueta cliente";
        else if (str_contar_digitos(n) > 4 && !tiene_sufijo_legal(n)) por = "digitos";
        else if (linea_es_ruido_cabecera(n) && !tiene_sufijo_legal(n)) por = "ruido cabecera";
        else if (parece_direccion(n) && !tiene_sufijo_legal(n)) por = "direccion";
        else { size_t L = utf8_largo(n); if (L < 4 || L > 90) por = "largo"; }
        if (por) { if (extraer_depurar) fprintf(stderr, "[emisor] %2d descartada (%s): %s\n", i, por, n); continue; }
        float p = 1.0f;
        if (tiene_sufijo_legal(n)) p += 3.0f;
        int dist = abs(i - ref); p += 2.0f / (1 + dist);
        if (i == 0) p += 0.5f;
        /* explicit label */
        if (str_empieza(n, "RAZON SOCIAL") || str_empieza(n, "EMISOR")) { p += 2.0f; }
        if (extraer_depurar) fprintf(stderr, "[emisor] %2d puntaje %.2f sufijo=%d: %s\n", i, p, tiene_sufijo_legal(n), n);
        if (p > mejor_p) { mejor_p = p; mejor = i; }
    }
    if (mejor >= 0) {
        const char *t = d->lineas[mejor].texto; const char *n = d->lineas[mejor].norm;
        if (str_empieza(n, "RAZON SOCIAL") || str_empieza(n, "EMISOR")) { const char *v = strchr(t, ':'); if (v) t = v + 1; }
        char tmp[OCRX_MAX_LEN]; str_copiar(tmp, sizeof tmp, t); str_recortar(tmp);
        /* "CORPORACION MUNDOTOP SAC FACTURA ELECTRONICA": cortar donde empieza el titulo del documento o una etiqueta */
        {
            char tn[OCRX_MAX_LEN * 2]; str_normalizar(tmp, tn, sizeof tn, true);
            static const char *cortes[] = {"FACTURA", "BOLETA", "NOTA DE", "TICKET", "RUC", "R.U.C", "TELF", "TEL.", "DIRECC", "AV.", "JR.", "CALLE", "MZ", NULL};
            size_t mejor_c = strlen(tn);
            for (int k = 0; cortes[k]; k++) { const char *f = str_buscar(tn, cortes[k]); if (f && f != tn && (size_t)(f - tn) < mejor_c) mejor_c = (size_t)(f - tn); }
            if (mejor_c < strlen(tn) && mejor_c >= 4) {
                /* mapear posicion normalizada a original contando codepoints */
                size_t cps = utf8_largo(tn) - utf8_largo(tn + mejor_c);
                const char *p = tmp; for (size_t z = 0; z < cps && *p; z++) utf8_decodificar(&p);
                tmp[p - tmp] = 0; str_recortar(tmp);
                size_t L = strlen(tmp); while (L && (tmp[L - 1] == '-' || tmp[L - 1] == ':' || tmp[L - 1] == '_' || tmp[L - 1] == '|')) tmp[--L] = 0; str_recortar(tmp);
            }
        }
        { size_t L = strlen(tmp); while (L && (tmp[L - 1] == ',' || tmp[L - 1] == ';' || tmp[L - 1] == ':' || tmp[L - 1] == '-' || tmp[L - 1] == '_' || tmp[L - 1] == '|' || tmp[L - 1] == '~')) tmp[--L] = 0; str_recortar(tmp); }
        str_copiar(c->emisor.razon_social, OCRX_MAX_LEN, tmp);
        poner_meta(&c->emisor.m_razon_social, x->origen, tiene_sufijo_legal(n) ? 0.85f : 0.6f);
        /* dirección: líneas siguientes que parezcan dirección */
        for (int k = mejor + 1; k < lim_sup && k < mejor + 5; k++) {
            if (parece_direccion(d->lineas[k].norm) && str_contar_digitos(d->lineas[k].norm) < 11) {
                str_copiar(c->emisor.direccion, OCRX_MAX_LEN, d->lineas[k].texto); poner_meta(&c->emisor.m_direccion, x->origen, 0.7f); break;
            }
        }
    }
}

static const char *ETQ_CLIENTE_FUERTE[] = {"SENOR(ES)", "SENORES", "SENOR", "SEFIOR", "SEN0R", "SERIOR", "SEHOR", "SR.(ES)", "SR(ES)", "SRES.", "CLIENTE:", "CLIENTE :", "ADQUIRIENTE", "ADQUIRENTE", "RAZON SOCIAL", "FACTURAR A", "DESTINATARIO", NULL};
static void extraer_cliente(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c;
    if (c->cliente.razon_social[0]) return;
    for (int pasada = 0; pasada < 2 && !c->cliente.razon_social[0]; pasada++)
    for (size_t i = 0; i < d->n; i++) {
        const char *n = d->lineas[i].norm;
        if (str_empieza(n, "DIRECC") || str_empieza(n, "DIR.") || str_contiene(n, "DIRECCION DEL CLIENTE") || str_contiene(n, "DOMICILIO")) continue;
        /* etiquetas al inicio de la línea o tras separador */
        const char *fin = fin_etiqueta(n, pasada == 0 ? ETQ_CLIENTE_FUERTE : ETQ_CLIENTE);
        if (!fin) continue;
        /* "RAZON SOCIAL" antes del bloque de cliente puede ser del emisor; exigir que esté después del RUC del emisor */
        if (x->idx_ruc_emisor >= 0 && (int)i < x->idx_ruc_emisor && !str_contiene(n, "CLIENTE") && !str_contiene(n, "SENOR")) continue;
        const char *v = valor_tras(fin);
        /* valor en la misma línea */
        char val[OCRX_MAX_LEN] = {0};
        if (v && *v) {
            /* cortar en la siguiente etiqueta (RUC:, DIRECCION:, FECHA:) */
            const char *corte = NULL;
            static const char *cortes[] = {"RUC", "R.U.C", "DNI", "DIRECCION", "DIRECC", "FECHA", "MONEDA", "FORMA", "TELEF", "DOC.", "DOCUMENTO", NULL};
            for (int k = 0; cortes[k]; k++) { const char *f = str_buscar(v, cortes[k]); if (f && (!corte || f < corte)) corte = f; }
            size_t L = corte ? (size_t)(corte - v) : strlen(v);
            /* mapear a texto original: usar la misma posición aproximada (norm no cambia longitud ASCII, pero tildes sí); tomar del texto original por búsqueda */
            const char *orig = d->lineas[i].texto;
            const char *o = strchr(orig, ':'); if (o && (size_t)(o - orig) < strlen(orig)) o++; else o = orig + (fin - n);
            if (o > orig + strlen(orig)) o = orig;
            str_copiar(val, sizeof val, o);
            if (corte) { char *cc = (char *)str_buscar(val, cortes[0]); for (int k = 0; cortes[k]; k++) { char *f = (char *)str_buscar(val, cortes[k]); if (f && (!cc || f < cc)) cc = f; } if (cc) *cc = 0; }
            str_recortar(val);
            (void)L;
            /* quitar separadores finales */
            size_t vl = strlen(val); while (vl && (val[vl - 1] == '-' || val[vl - 1] == ':' || val[vl - 1] == '|')) val[--vl] = 0; str_recortar(val);
        }
        if (!val[0] || str_contar_digitos(val) > 6) {
            /* siguiente línea */
            if (i + 1 < d->n) { const char *s = d->lineas[i + 1].norm; if (!linea_tiene_alguna(s, ETQ_RUC_CLIENTE) && str_contar_digitos(s) < 4 && !sunat_tipo_desde_titulo(s)) str_copiar(val, sizeof val, d->lineas[i + 1].texto); }
        }
        if (val[0] && utf8_largo(val) >= 3) {
            str_copiar(c->cliente.razon_social, OCRX_MAX_LEN, val); poner_meta(&c->cliente.m_razon_social, x->origen, 0.7f);
            if (x->idx_cliente < 0) x->idx_cliente = (int)i;
            /* dirección del cliente en las 3 líneas siguientes */
            for (size_t k = i + 1; k < d->n && k <= i + 3; k++) {
                const char *s = d->lineas[k].norm;
                if (str_empieza(s, "DIRECCION") || str_empieza(s, "DIRECC") || str_empieza(s, "DIR.") || str_empieza(s, "DOMICILIO")) {
                    const char *o = strchr(d->lineas[k].texto, ':'); str_copiar(c->cliente.direccion, OCRX_MAX_LEN, o ? o + 1 : d->lineas[k].texto); str_recortar(c->cliente.direccion); break;
                }
            }
            return;
        }
    }
}

static void extraer_fechas(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c;
    char f[11];
    if (!es_seguro(&c->m_fecha)) {
        bool ok = false;
        for (size_t i = 0; i < d->n && !ok; i++) {
            const char *n = d->lineas[i].norm;
            if (linea_tiene_alguna(n, ETQ_VENC)) continue;
            const char *fin = fin_etiqueta(n, ETQ_FECHA); if (!fin) continue;
            if (str_contiene(n, "NACIMIENTO") || str_contiene(n, "TRASLADO") || str_contiene(n, "INICIO")) continue;
            if (sunat_fecha_en_linea(fin, f, NULL) || sunat_fecha_en_linea(n, f, NULL)) { str_copiar(c->fecha_emision, 11, f); poner_meta(&c->m_fecha, x->origen, 0.9f); ok = true; char h[9]; if (sunat_hora_parsear(n, h)) str_copiar(c->hora_emision, 9, h); }
            else if (i + 1 < d->n && sunat_fecha_en_linea(d->lineas[i + 1].norm, f, NULL)) { str_copiar(c->fecha_emision, 11, f); poner_meta(&c->m_fecha, x->origen, 0.8f); ok = true; }
        }
        if (!ok) {
            /* primera fecha razonable del documento */
            for (size_t i = 0; i < d->n; i++) {
                if (linea_tiene_alguna(d->lineas[i].norm, ETQ_VENC)) continue;
                if (sunat_fecha_en_linea(d->lineas[i].norm, f, NULL) && sunat_fecha_razonable(f)) { str_copiar(c->fecha_emision, 11, f); poner_meta(&c->m_fecha, x->origen, 0.6f); c->m_fecha.dudoso = true; break; }
            }
        }
    }
    if (!c->fecha_vencimiento[0]) {
        for (size_t i = 0; i < d->n; i++) {
            const char *n = d->lineas[i].norm;
            const char *fin = fin_etiqueta(n, ETQ_VENC); if (!fin) continue;
            if (sunat_fecha_en_linea(fin, f, NULL)) { str_copiar(c->fecha_vencimiento, 11, f); break; }
            if (i + 1 < d->n && es_etiqueta_sola(n) && sunat_fecha_en_linea(d->lineas[i + 1].norm, f, NULL)) { str_copiar(c->fecha_vencimiento, 11, f); break; }
        }
    }
    if (!c->hora_emision[0]) { for (size_t i = 0; i < d->n; i++) { char h[9]; if (str_contiene(d->lineas[i].norm, "HORA") && sunat_hora_parsear(d->lineas[i].norm, h)) { str_copiar(c->hora_emision, 9, h); break; } } }
}

static void extraer_moneda_y_pago(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c;
    int pen = 0, usd = 0;
    for (size_t i = 0; i < d->n; i++) {
        const char *n = d->lineas[i].norm;
        if (!es_seguro(&c->m_moneda) && c->moneda == OCRX_MON_DESCONOCIDA) {
            const char *fin = fin_etiqueta(n, ETQ_MONEDA);
            if (fin) { ocrx_moneda m = sunat_moneda_desde_texto(fin); if (m == OCRX_MON_DESCONOCIDA && i + 1 < d->n) m = sunat_moneda_desde_texto(d->lineas[i + 1].norm); if (m) { c->moneda = m; poner_meta(&c->m_moneda, x->origen, 0.9f); } }
        }
        if (str_contiene(n, "S/")) pen++;
        if (str_contiene(n, "US$") || str_contiene(n, "USD") || str_contiene(n, "DOLARES")) usd++;
        if (!c->forma_pago[0]) {
            const char *fin = fin_etiqueta(n, ETQ_FORMA_PAGO);
            if (fin) {
                const char *v = valor_tras(fin);
                if (str_contiene(v, "CONTADO") || str_contiene(v, "EFECTIVO") || str_contiene(v, "TARJETA") || str_contiene(v, "YAPE") || str_contiene(v, "PLIN") || str_contiene(v, "TRANSFERENCIA") || str_contiene(v, "DEPOSITO")) str_copiar(c->forma_pago, 32, "Contado");
                else if (str_contiene(v, "CREDITO")) str_copiar(c->forma_pago, 32, "Credito");
                else if (i + 1 < d->n) { const char *s = d->lineas[i + 1].norm; if (str_contiene(s, "CONTADO")) str_copiar(c->forma_pago, 32, "Contado"); else if (str_contiene(s, "CREDITO")) str_copiar(c->forma_pago, 32, "Credito"); }
            }
        }
        if (!c->orden_compra[0]) { const char *fin = fin_etiqueta(n, ETQ_OC); if (fin) { const char *v = valor_tras(fin); char tok[32]; int k = 0; for (; *v && k < 31 && (isalnum((unsigned char)*v) || *v == '-'); v++) tok[k++] = *v; tok[k] = 0; if (k >= 2) str_copiar(c->orden_compra, 32, tok); } }
        if (!c->guia_relacionada[0] && !linea_tiene_alguna(n, (const char *[]){"TITULO", NULL})) {
            const char *fin = fin_etiqueta(n, ETQ_GUIA);
            if (fin && c->tipo != OCRX_CPE_GUIA_REMITENTE && c->tipo != OCRX_CPE_GUIA_TRANSPORTISTA) { char s[8], nm[12]; bool cr; if (sunat_serie_numero_parsear(fin, OCRX_CPE_GUIA_REMITENTE, s, nm, &cr)) snprintf(c->guia_relacionada, 24, "%s-%s", s, nm); }
        }
        if (c->tipo_cambio == 0) { const char *fin = fin_etiqueta(n, ETQ_TC); if (fin) { sunat_monto m = sunat_monto_parsear(valor_tras(fin)); if (m.ok && m.valor > 1 && m.valor < 10) c->tipo_cambio = m.valor; } }
        if (!c->hash_cpe[0]) {
            const char *fin = fin_etiqueta(n, ETQ_HASH);
            if (fin) { const char *v = valor_tras(fin); char tok[64]; int k = 0; for (; *v && k < 63 && (isalnum((unsigned char)*v) || *v == '+' || *v == '/' || *v == '='); v++) tok[k++] = *v; tok[k] = 0; if (k >= 20) str_copiar(c->hash_cpe, 64, tok); }
        }
    }
    if (c->moneda == OCRX_MON_DESCONOCIDA) {
        if (usd > pen) { c->moneda = OCRX_MON_USD; poner_meta(&c->m_moneda, x->origen, 0.7f); }
        else if (pen > 0) { c->moneda = OCRX_MON_PEN; poner_meta(&c->m_moneda, x->origen, 0.7f); }
        else { c->moneda = OCRX_MON_PEN; poner_meta(&c->m_moneda, x->origen, 0.5f); c->m_moneda.dudoso = true; }
    }
}

static void extraer_totales(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c; ocrx_totales *t = &c->totales;
    double v; ocrx_moneda mon; size_t usada;
    bool vio_total = false;
    for (size_t i = 0; i < d->n; i++) {
        const char *n = d->lineas[i].norm;
        /* monto en letras */
        if (!t->monto_en_letras[0] && sunat_linea_es_monto_en_letras(n)) {
            if (i + 1 < d->n && str_contiene(d->lineas[i + 1].norm, "/100") && !str_contiene(n, "/100")) {
                char junto[OCRX_MAX_LEN * 2]; snprintf(junto, sizeof junto, "%s %s", d->lineas[i].texto, d->lineas[i + 1].texto);
                str_copiar(t->monto_en_letras, OCRX_MAX_LEN, junto);
            } else str_copiar(t->monto_en_letras, OCRX_MAX_LEN, d->lineas[i].texto);
            continue;
        }
        /* porcentaje de IGV declarado */
        if (linea_tiene_alguna(n, ETQ_IGV)) {
            const char *p = str_buscar(n, "%");
            if (p) { const char *q = p; while (q > n && (isdigit((unsigned char)q[-1]) || q[-1] == '.' || q[-1] == ',' || q[-1] == ' ')) q--; while (*q == ' ') q++; double pct = atof(q); if (pct == 18) t->tasa_igv = 0.18; else if (pct > 10 && pct < 11) t->tasa_igv = 0.105; else if (pct == 10) t->tasa_igv = 0.10; else if (pct == 8) t->tasa_igv = 0.08; }
            else if (str_contiene(n, "18%") || str_contiene(n, "18 %")) t->tasa_igv = 0.18;
        }
        /* detracción / retención / percepción */
        if (linea_tiene_alguna(n, ETQ_DETRACCION)) {
            sunat_monto ms[8]; size_t nm = sunat_montos_en_linea(n, ms, 8);
            const char *p = strchr(n, '%');
            if (p) { const char *q = p; while (q > n && (isdigit((unsigned char)q[-1]) || q[-1] == '.' || q[-1] == ',' || q[-1] == ' ')) q--; t->detraccion_pct = atof(q); }
            for (size_t k = 0; k < nm; k++) { if (ms[k].valor > 0 && ms[k].valor != t->detraccion_pct && ms[k].decimales == 2) t->detraccion_monto = ms[k].valor; }
            if (nm == 1 && ms[0].decimales == 2 && !t->detraccion_monto) t->detraccion_monto = ms[0].valor;
            /* cuenta BN */
            const char *cta = str_buscar(n, "CTA"); if (!cta) cta = str_buscar(n, "CUENTA"); if (!cta) cta = str_buscar(n, "NACION");
            if (cta) { char dg[32]; str_solo_digitos(cta, dg, sizeof dg); if (strlen(dg) >= 11 && strlen(dg) <= 20) str_copiar(t->detraccion_cuenta, 24, dg); }
            if (!t->detraccion_cuenta[0]) { char dg[32]; str_solo_digitos(n, dg, sizeof dg); if (strlen(dg) >= 11 && strlen(dg) <= 20 && !sunat_ruc_valido(dg)) str_copiar(t->detraccion_cuenta, 24, dg); }
            /* código catálogo 54 */
            const char *cod = str_buscar(n, "COD"); if (cod) { const char *z = cod; while (*z && !isdigit((unsigned char)*z)) z++; if (isdigit((unsigned char)z[0]) && isdigit((unsigned char)z[1]) && isdigit((unsigned char)z[2]) && !isdigit((unsigned char)z[3])) { memcpy(t->detraccion_codigo, z, 3); t->detraccion_codigo[3] = 0; } }
            continue;
        }
        if (linea_tiene_alguna(n, ETQ_RETENCION) && c->tipo != OCRX_CPE_RETENCION) { if (monto_para_etiqueta(x, i, ETQ_RETENCION, &v, &mon, &usada) && v > 0 && v < 1e7) t->retencion_monto = v; continue; }
        if (linea_tiene_alguna(n, ETQ_PERCEPCION) && c->tipo != OCRX_CPE_PERCEPCION) { if (monto_para_etiqueta(x, i, ETQ_PERCEPCION, &v, &mon, &usada) && v > 0) t->percepcion_monto = v; continue; }
        /* orden: ICBPER antes de IGV; exonerada/inafecta antes de gravada (que incluye "SUBTOTAL"); total al final */
        if (linea_tiene_alguna(n, ETQ_ICBPER)) { if (monto_para_etiqueta(x, i, ETQ_ICBPER, &v, &mon, &usada) && v >= 0 && v < 1000) t->icbper = v; continue; }
        if (linea_tiene_alguna(n, ETQ_ISC)) { if (monto_para_etiqueta(x, i, ETQ_ISC, &v, &mon, &usada) && v >= 0) t->isc = v; continue; }
        if (linea_tiene_alguna(n, ETQ_DESCUENTO) && !str_contiene(n, "SIN DESCUENTO")) { if (monto_para_etiqueta(x, i, ETQ_DESCUENTO, &v, &mon, &usada)) { if (!str_contiene(n, "%") || fabs(v) > 100) t->descuentos = fabs(v); } continue; }
        if (linea_tiene_alguna(n, ETQ_IGV) && !str_contiene(n, "INCLUYE") && !str_contiene(n, "INC. IGV") && !str_contiene(n, "INCLUIDO") && !str_contiene(n, "SIN IGV") && !str_contiene(n, "P. UNIT") && !str_contiene(n, "PRECIO")) {
            if (!es_seguro(&t->m_igv) && monto_para_etiqueta(x, i, ETQ_IGV, &v, &mon, &usada) && v >= 0) { t->igv = v; poner_meta(&t->m_igv, x->origen, 0.85f); }
            continue;
        }
        if (linea_tiene_alguna(n, ETQ_EXONERADA)) { if (monto_para_etiqueta(x, i, ETQ_EXONERADA, &v, &mon, &usada) && v >= 0) { t->exonerada = v; poner_meta(&t->m_exonerada, x->origen, 0.85f); } continue; }
        if (linea_tiene_alguna(n, ETQ_INAFECTA)) { if (monto_para_etiqueta(x, i, ETQ_INAFECTA, &v, &mon, &usada) && v >= 0) { t->inafecta = v; poner_meta(&t->m_inafecta, x->origen, 0.85f); } continue; }
        if (linea_tiene_alguna(n, ETQ_GRATUITA)) { if (monto_para_etiqueta(x, i, ETQ_GRATUITA, &v, &mon, &usada) && v >= 0) t->gratuita = v; continue; }
        if (linea_tiene_alguna(n, ETQ_EXPORT)) { if (monto_para_etiqueta(x, i, ETQ_EXPORT, &v, &mon, &usada) && v >= 0) t->exportacion = v; continue; }
        if (linea_tiene_alguna(n, ETQ_OTROS_CARGOS)) { if (monto_para_etiqueta(x, i, ETQ_OTROS_CARGOS, &v, &mon, &usada) && v >= 0) t->otros_cargos += v; continue; }
        if (linea_tiene_alguna(n, ETQ_GRAVADA) && !str_contiene(n, "TOTAL A PAGAR") && !str_contiene(n, "IMPORTE TOTAL")) {
            if (!es_seguro(&t->m_gravada) && monto_para_etiqueta(x, i, ETQ_GRAVADA, &v, &mon, &usada) && v >= 0) { t->gravada = v; poner_meta(&t->m_gravada, x->origen, 0.85f); if (mon) { if (c->moneda == OCRX_MON_DESCONOCIDA) c->moneda = mon; } }
            continue;
        }
        if (linea_tiene_alguna(n, ETQ_TOTAL) && !linea_tiene_alguna(n, ETQ_NO_TOTAL)) {
            if (monto_para_etiqueta(x, i, ETQ_TOTAL, &v, &mon, &usada) && v > 0) {
                /* el último "TOTAL" del documento suele ser el válido ("IMPORTE TOTAL"); preferir etiquetas fuertes */
                bool fuerte = str_contiene(n, "IMPORTE TOTAL") || str_contiene(n, "TOTAL A PAGAR") || str_contiene(n, "MONTO TOTAL") || str_contiene(n, "TOTAL VENTA") || str_contiene(n, "PRECIO VENTA");
                if (!es_seguro(&t->m_total) && (!vio_total || fuerte || v >= t->total)) { t->total = v; poner_meta(&t->m_total, x->origen, fuerte ? 0.9f : 0.8f); vio_total = true; if (mon && c->moneda == OCRX_MON_DESCONOCIDA) c->moneda = mon; }
            }
            continue;
        }
    }
    if (!t->total && !t->gravada && !t->igv) totales_por_bloques(x);
}

static void extraer_referencia(ctx *x) {
    const doc_texto *d = x->d; ocrx_comprobante *c = x->c;
    if (c->tipo != OCRX_CPE_NOTA_CREDITO && c->tipo != OCRX_CPE_NOTA_DEBITO) return;
    if (c->referencia.serie[0]) return;
    for (size_t i = 0; i < d->n; i++) {
        const char *n = d->lineas[i].norm;
        if (str_contiene(n, "MODIFICA") || str_contiene(n, "REFERENCIA") || str_contiene(n, "DOC. AFECTADO") || str_contiene(n, "DOCUMENTO AFECTADO") || str_contiene(n, "COMPROBANTE QUE") || str_contiene(n, "AFECTA A") || str_contiene(n, "DOC. REF") || str_contiene(n, "RELACIONADO")) {
            char s[8], nm[12]; bool cr;
            const char *src = n;
            if (sunat_serie_numero_parsear(src, OCRX_CPE_DESCONOCIDO, s, nm, &cr) && !(strcmp(s, c->serie) == 0 && strcmp(nm, c->numero) == 0)) { str_copiar(c->referencia.serie, 8, s); str_copiar(c->referencia.numero, 12, nm); c->referencia.tipo = sunat_tipo_desde_serie(s, OCRX_CPE_DESCONOCIDO); }
            else if (i + 1 < d->n && sunat_serie_numero_parsear(d->lineas[i + 1].norm, OCRX_CPE_DESCONOCIDO, s, nm, &cr)) { str_copiar(c->referencia.serie, 8, s); str_copiar(c->referencia.numero, 12, nm); c->referencia.tipo = sunat_tipo_desde_serie(s, OCRX_CPE_DESCONOCIDO); }
            if (str_contiene(n, "MOTIVO") || str_contiene(n, "SUSTENTO")) { const char *o = strchr(d->lineas[i].texto, ':'); if (o) { str_copiar(c->referencia.motivo, OCRX_MAX_LEN, o + 1); str_recortar(c->referencia.motivo); } }
            if (c->referencia.serie[0]) break;
        }
    }
    if (!c->referencia.motivo[0]) {
        for (size_t i = 0; i < d->n; i++) { const char *n = d->lineas[i].norm; if (str_contiene(n, "MOTIVO") || str_contiene(n, "SUSTENTO") || str_contiene(n, "TIPO DE NOTA")) { const char *o = strchr(d->lineas[i].texto, ':'); if (o) { str_copiar(c->referencia.motivo, OCRX_MAX_LEN, o + 1); str_recortar(c->referencia.motivo); if (c->referencia.motivo[0]) break; } else if (i + 1 < d->n) { str_copiar(c->referencia.motivo, OCRX_MAX_LEN, d->lineas[i + 1].texto); break; } } }
    }
    /* código catálogo 09 por texto */
    if (!c->referencia.motivo_codigo[0] && c->referencia.motivo[0]) {
        char m[OCRX_MAX_LEN]; str_normalizar(c->referencia.motivo, m, sizeof m, true);
        const char *cod = NULL;
        if (str_contiene(m, "ANULACION DE LA OPERACION")) cod = "01";
        else if (str_contiene(m, "ANULACION POR ERROR EN EL RUC")) cod = "02";
        else if (str_contiene(m, "CORRECCION POR ERROR EN LA DESCRIPCION")) cod = "03";
        else if (str_contiene(m, "DESCUENTO GLOBAL")) cod = "04";
        else if (str_contiene(m, "DESCUENTO POR ITEM")) cod = "05";
        else if (str_contiene(m, "DEVOLUCION TOTAL")) cod = "06";
        else if (str_contiene(m, "DEVOLUCION POR ITEM")) cod = "07";
        else if (str_contiene(m, "BONIFICACION")) cod = "08";
        else if (str_contiene(m, "DISMINUCION EN EL VALOR")) cod = "09";
        else if (str_contiene(m, "OTROS")) cod = "10";
        else if (str_contiene(m, "ANULACION")) cod = "01";
        if (cod) str_copiar(c->referencia.motivo_codigo, 4, cod);
    }
}

void extraer_desde_texto(const doc_texto *d, ocrx_comprobante *c, arena *a, ocrx_origen origen) {
    ctx x = {0}; x.d = d; x.c = c; x.a = a; x.origen = origen;
    x.idx_titulo = x.idx_ruc_emisor = x.idx_cliente = -1;
    if (!d || !d->n) return;
    if (origen == OCRX_ORIGEN_PDF_TEXTO) c->tenia_texto_pdf = true;
    extraer_tipo_y_titulo(&x);
    extraer_rucs(&x);
    extraer_serie_numero(&x);
    extraer_razon_social_emisor(&x);
    extraer_cliente(&x);
    extraer_fechas(&x);
    extraer_moneda_y_pago(&x);
    extraer_totales(&x);
    extraer_referencia(&x);
    /* tipo por serie si sigue desconocido */
    if (c->tipo == OCRX_CPE_DESCONOCIDO && c->serie[0]) { c->tipo = sunat_tipo_desde_serie(c->serie, OCRX_CPE_DESCONOCIDO); if (c->tipo) poner_meta(&c->m_tipo, origen, 0.6f); }
    /* receptor: si el cliente tiene RUC y aparece etiqueta SEÑOR(ES) sin razón social, dejar en blanco; nada que inventar */
}

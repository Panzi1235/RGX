#include "sunat.h"
#include "../core/str.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

bool sunat_monto_igual(double a, double b) { return fabs(a - b) < 0.011; }

const char *sunat_moneda_codigo(ocrx_moneda m) {
    switch (m) { case OCRX_MON_PEN: return "PEN"; case OCRX_MON_USD: return "USD"; case OCRX_MON_EUR: return "EUR"; default: return ""; }
}

ocrx_moneda sunat_moneda_desde_texto(const char *t) {
    if (!t) return OCRX_MON_DESCONOCIDA;
    char n[128]; str_normalizar(t, n, sizeof n, true);
    if (str_contiene(n, "US$") || str_contiene(n, "USD") || str_contiene(n, "DOLAR") || str_contiene(n, "U$S") || str_contiene(n, "$US")) return OCRX_MON_USD;
    if (str_contiene(n, "EUR") || str_contiene(n, "\xE2\x82\xAC")) return OCRX_MON_EUR;
    if (str_contiene(n, "S/") || str_contiene(n, "PEN") || str_contiene(n, "SOLES") || str_contiene(n, "SOL ") || str_contiene(n, "NUEVOS SOLES") || str_contiene(n, "S /")) return OCRX_MON_PEN;
    /* "$" solo es ambiguo: en Perú casi siempre dólares cuando aparece sin S/ */
    if (strchr(n, '$')) return OCRX_MON_USD;
    return OCRX_MON_DESCONOCIDA;
}

/* Lee desde p un número con separadores de miles/decimales ambiguos. Devuelve chars consumidos (0 si nada). */
static size_t leer_numero(const char *p, double *valor, int *decimales) {
    /* Normalizar confusiones dentro del número: O->0, l/I->1, S->5, B->8 sólo cuando están rodeados de dígitos/separadores */
    char tmp[64]; size_t k = 0; const char *q = p;
    bool vio_digito = false;
    while (*q && k < 63) {
        char c = *q;
        if (isdigit((unsigned char)c)) { tmp[k++] = c; vio_digito = true; q++; continue; }
        if (c == '.' || c == ',' || c == '\'' ) { tmp[k++] = c; q++; continue; }
        if (c == ' ' && vio_digito && isdigit((unsigned char)q[1]) && isdigit((unsigned char)q[2]) && isdigit((unsigned char)q[3]) && !isdigit((unsigned char)q[4])) {
            /* "1 234,56" miles con espacio */
            tmp[k++] = ','; q++; continue;
        }
        char d = ocr_a_digito(c);
        if (d && vio_digito && (isdigit((unsigned char)q[1]) || q[1] == '.' || q[1] == ',' || !isalnum((unsigned char)q[1]))) {
            /* letra confundida en medio de un número: 11O.00, 12.5O */
            tmp[k++] = d; q++; continue;
        }
        break;
    }
    tmp[k] = 0;
    /* recortar separadores finales */
    while (k && (tmp[k - 1] == '.' || tmp[k - 1] == ',' || tmp[k - 1] == '\'')) tmp[--k] = 0;
    if (k == 0) return 0;

    /* Decidir separador decimal: el último . o , si le siguen 1-2 dígitos (o 3 cuando sólo hay uno y hay otro distinto antes) */
    const char *ult_p = strrchr(tmp, '.'); const char *ult_c = strrchr(tmp, ',');
    const char *dec = NULL;
    int n_p = 0, n_c = 0; for (const char *t = tmp; *t; t++) { if (*t == '.') n_p++; else if (*t == ',') n_c++; }
    const char *ult = (ult_p > ult_c) ? ult_p : ult_c;
    if (ult) {
        size_t tras = strlen(ult + 1);
        char sep = *ult;
        int n_sep = sep == '.' ? n_p : n_c;
        if (tras <= 2) dec = ult;
        else if (tras == 3) {
            /* 1.234 -> miles; pero si el otro separador existe y es distinto (1,234.567 raro) tomar el último como decimal */
            if (n_sep == 1 && ((sep == '.' && n_c > 0) || (sep == ',' && n_p > 0))) dec = ult;
            else dec = NULL;
        } else dec = ult;
    }
    char entero[64], frac[8] = "0"; size_t e = 0;
    for (const char *t = tmp; *t; t++) {
        if (t == dec) break;
        if (isdigit((unsigned char)*t)) entero[e++] = *t;
    }
    entero[e] = 0;
    if (dec) { size_t f = 0; for (const char *t = dec + 1; *t && f < 7; t++) if (isdigit((unsigned char)*t)) frac[f++] = *t; frac[f] = 0; *decimales = (int)f; }
    else *decimales = 0;
    if (e == 0 && !dec) return 0;
    double v = atof(entero[0] ? entero : "0");
    if (dec && frac[0]) { double fr = atof(frac); for (size_t i = 0; i < strlen(frac); i++) fr /= 10.0; v += fr; }
    *valor = v;
    return (size_t)(q - p);
}

sunat_monto sunat_monto_parsear(const char *texto) {
    sunat_monto m = {0};
    if (!texto) return m;
    char n[128]; str_normalizar(texto, n, sizeof n, true);
    m.moneda = sunat_moneda_desde_texto(n);
    const char *p = n;
    /* saltar símbolos */
    bool paren = false;
    for (;;) {
        while (*p == ' ' || *p == ':' || *p == '=') p++;
        if (*p == '(') { paren = true; p++; continue; }
        if (*p == '-' && !isdigit((unsigned char)p[1]) && p[1] != ' ') { m.negativo = true; p++; continue; }
        if (*p == '-' ) { m.negativo = true; p++; continue; }
        if (str_empieza(p, "S/.")) { p += 3; continue; }
        if (str_empieza(p, "S/")) { p += 2; continue; }
        if (str_empieza(p, "US$")) { p += 3; continue; }
        if (str_empieza(p, "USD")) { p += 3; continue; }
        if (str_empieza(p, "PEN")) { p += 3; continue; }
        if (*p == '$') { p++; continue; }
        break;
    }
    double v; int dec;
    size_t c = leer_numero(p, &v, &dec);
    if (!c) return m;
    p += c;
    while (*p == ' ') p++;
    if (paren && *p == ')') m.negativo = true;
    if (*p == '-' && !isalnum((unsigned char)p[1])) m.negativo = true;
    m.valor = m.negativo ? -v : v;
    m.decimales = dec;
    m.ok = true;
    return m;
}

size_t sunat_montos_en_linea(const char *linea, sunat_monto *out, size_t cap) {
    size_t n = 0;
    if (!linea) return 0;
    const char *p = linea;
    while (*p && n < cap) {
        /* arrancar en un dígito precedido por no-alfanumérico (o inicio) */
        if (isdigit((unsigned char)*p) && (p == linea || !isalnum((unsigned char)p[-1]))) {
            /* evitar fechas 15/10/2026 y horas 10:30, RUC (11 dígitos), series F001-123 */
            bool tras_barra = p > linea && p[-1] == '/' && !(p - linea >= 2 && (p[-2] == 'S' || p[-2] == 's'));
            bool tras_guion = p > linea && p[-1] == '-' && p - linea >= 2 && isalnum((unsigned char)p[-2]);
            double v; int dec; size_t c = leer_numero(p, &v, &dec);
            const char *fin = p + c;
            bool es_fecha = tras_barra || tras_guion || (*fin == '/' ) || (*fin == '-' && isdigit((unsigned char)fin[1]));
            bool es_hora = *fin == ':' && isdigit((unsigned char)fin[1]);
            /* contar dígitos contiguos crudos */
            size_t dig = 0; for (const char *t = p; t < fin; t++) if (isdigit((unsigned char)*t)) dig++;
            bool es_id = dec == 0 && dig >= 7;   /* RUC, DNI, número de comprobante largo */
            bool pct = (*fin == '%') || (fin[0] == ' ' && fin[1] == '%');
            if (c && !es_fecha && !es_hora && !es_id && !pct) {
                sunat_monto m = {0};
                m.valor = v; m.decimales = dec; m.ok = true;
                /* signo */
                const char *b = p; while (b > linea && b[-1] == ' ') b--;
                if (b > linea && (b[-1] == '-' || b[-1] == '(')) m.negativo = true;
                if (m.negativo) m.valor = -v;
                /* moneda cercana a la izquierda */
                const char *ini = p - 1; int back = 0;
                while (ini > linea && back < 6) { ini--; back++; }
                char ctx[16]; size_t l = (size_t)(p - ini); if (l > 15) l = 15; memcpy(ctx, ini, l); ctx[l] = 0;
                m.moneda = sunat_moneda_desde_texto(ctx);
                out[n++] = m;
            }
            p = fin > p ? fin : p + 1;
            continue;
        }
        p++;
    }
    return n;
}

bool sunat_monto_ultimo_en_linea(const char *linea, sunat_monto *out, const char **inicio) {
    sunat_monto ms[32];
    size_t n = sunat_montos_en_linea(linea, ms, 32);
    if (!n) return false;
    *out = ms[n - 1];
    if (inicio) *inicio = NULL;
    return true;
}

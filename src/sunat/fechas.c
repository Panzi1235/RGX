#include "sunat.h"
#include "../core/str.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static int mes_desde_nombre(const char *s, size_t *consumidos) {
    static const struct { const char *n; int m; } meses[] = {
        {"ENERO",1},{"FEBRERO",2},{"MARZO",3},{"ABRIL",4},{"MAYO",5},{"JUNIO",6},{"JULIO",7},{"AGOSTO",8},
        {"SEPTIEMBRE",9},{"SETIEMBRE",9},{"OCTUBRE",10},{"NOVIEMBRE",11},{"DICIEMBRE",12},
        {"ENE",1},{"FEB",2},{"MAR",3},{"ABR",4},{"MAY",5},{"JUN",6},{"JUL",7},{"AGO",8},{"SEP",9},{"SET",9},{"OCT",10},{"NOV",11},{"DIC",12},
        {"JAN",1},{"APR",4},{"AUG",8},{"DEC",12},
        {NULL,0}};
    for (int i = 0; meses[i].n; i++) {
        size_t l = strlen(meses[i].n);
        if (str_empieza(s, meses[i].n) && !isalpha((unsigned char)s[l])) { *consumidos = l; return meses[i].m; }
    }
    return 0;
}

static bool dia_valido(int a, int m, int d) {
    static const int dm[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12 || d < 1) return false;
    int max = dm[m];
    if (m == 2 && ((a % 4 == 0 && a % 100 != 0) || a % 400 == 0)) max = 29;
    return d <= max;
}

static int anio_normal(int a) { if (a < 100) return a + (a < 70 ? 2000 : 1900); return a; }

/* Intenta parsear desde p; devuelve consumidos o 0 */
static size_t parsear_en(const char *p, char out[11]) {
    const char *s = p;
    int n1 = 0, n2 = 0, n3 = 0; int l1 = 0, l2 = 0, l3 = 0;
    char d[16]; int k;

    /* leer primer grupo numérico (con confusiones OCR permitidas) */
    k = 0; while (*s && k < 5 && (isdigit((unsigned char)*s) || (k > 0 && ocr_a_digito(*s) && (isdigit((unsigned char)s[1]) || s[1] == '/' || s[1] == '-' || s[1] == '.')))) { d[k++] = isdigit((unsigned char)*s) ? *s : ocr_a_digito(*s); s++; }
    d[k] = 0; if (!k) return 0;
    n1 = atoi(d); l1 = k;
    while (*s == ' ') s++;
    /* "15 de octubre de 2026" / "15 OCT 2026" / "15-OCT-2026" */
    if (str_empieza(s, "DE ")) s += 3;
    while (*s == ' ' || *s == '-' || *s == '/' || *s == '.') s++;
    size_t cons = 0;
    int mes = mes_desde_nombre(s, &cons);
    if (mes) {
        s += cons; while (*s == ' ' || *s == '-' || *s == '/' || *s == '.' || *s == ',') s++;
        if (str_empieza(s, "DEL ")) s += 4; else if (str_empieza(s, "DE ")) s += 3;
        while (*s == ' ') s++;
        k = 0; while (*s && k < 4 && isdigit((unsigned char)*s)) d[k++] = *s++;
        d[k] = 0; if (k != 4 && k != 2) return 0;
        int a = anio_normal(atoi(d));
        if (!dia_valido(a, mes, n1)) return 0;
        { char tmp[32]; snprintf(tmp, sizeof tmp, "%04d-%02d-%02d", a % 10000, mes % 100, n1 % 100); memcpy(out, tmp, 11); }
        return (size_t)(s - p);
    }
    /* numérico: n1 sep n2 sep n3 */
    char sep = 0;
    s = p + l1;
    /* retroceder: s debe apuntar al separador */
    if (*s != '/' && *s != '-' && *s != '.' && *s != ' ') return 0;
    sep = *s; s++;
    while (*s == ' ') s++;
    k = 0; while (*s && k < 2 && (isdigit((unsigned char)*s) || ocr_a_digito(*s))) { d[k++] = isdigit((unsigned char)*s) ? *s : ocr_a_digito(*s); s++; }
    d[k] = 0; if (!k) return 0; n2 = atoi(d); l2 = k;
    if (*s != '/' && *s != '-' && *s != '.' && *s != ' ') return 0;
    if (sep != ' ' && *s != sep && !(*s == ' ')) return 0;
    s++;
    while (*s == ' ') s++;
    k = 0; while (*s && k < 4 && (isdigit((unsigned char)*s) || (k > 0 && ocr_a_digito(*s)))) { d[k++] = isdigit((unsigned char)*s) ? *s : ocr_a_digito(*s); s++; }
    d[k] = 0; if (k != 2 && k != 4) return 0; n3 = atoi(d); l3 = k;
    if (isdigit((unsigned char)*s)) return 0;
    (void)l2;
    int a, m, dd;
    if (l1 == 4) { a = n1; m = n2; dd = n3; }               /* aaaa-mm-dd */
    else if (l3 == 4 || l3 == 2) {                             /* dd/mm/aaaa o dd/mm/aa */
        a = anio_normal(n3); m = n2; dd = n1;
        if (!dia_valido(a, m, dd) && dia_valido(a, dd, m) && n1 <= 12) { int t = m; m = dd; dd = t; } /* mm/dd por si acaso */
    } else return 0;
    if (!dia_valido(a, m, dd)) return 0;
    if (a < 1990 || a > 2100) return 0;
    { char tmp[32]; snprintf(tmp, sizeof tmp, "%04d-%02d-%02d", a % 10000, m % 100, dd % 100); memcpy(out, tmp, 11); }
    return (size_t)(s - p);
}

bool sunat_fecha_parsear(const char *texto, char out[11]) {
    if (!texto) return false;
    char n[160]; str_normalizar(texto, n, sizeof n, true);
    const char *p = n;
    while (*p == ' ' || *p == ':') p++;
    if (parsear_en(p, out) > 0) return true;
    return sunat_fecha_en_linea(texto, out, NULL);
}

bool sunat_fecha_en_linea(const char *linea, char out[11], const char **inicio) {
    if (!linea) return false;
    char n[512]; str_normalizar(linea, n, sizeof n, true);
    for (const char *p = n; *p; p++) {
        if (!isdigit((unsigned char)*p)) continue;
        if (p > n && isalnum((unsigned char)p[-1])) continue;
        if (parsear_en(p, out)) { if (inicio) *inicio = linea + (p - n); return true; }
        while (isdigit((unsigned char)*p) ) p++;
        if (!*p) break;
    }
    return false;
}

bool sunat_fecha_razonable(const char *iso) {
    if (!iso || strlen(iso) != 10) return false;
    int a = atoi(iso), m = atoi(iso + 5), d = atoi(iso + 8);
    if (!dia_valido(a, m, d)) return false;
    time_t t = time(NULL); struct tm *hoy = localtime(&t);
    int ha = hoy->tm_year + 1900, hm = hoy->tm_mon + 1, hd = hoy->tm_mday;
    if (a < 2010) return false;
    long v = a * 10000L + m * 100 + d, h = ha * 10000L + hm * 100 + hd + 2; /* +2 días de tolerancia por zona horaria */
    return v <= h;
}

bool sunat_hora_parsear(const char *texto, char out[9]) {
    if (!texto) return false;
    char n[64]; str_normalizar(texto, n, sizeof n, true);
    const char *p = n;
    for (; *p; p++) {
        if (!isdigit((unsigned char)*p)) continue;
        int h, m, s = 0; int c = 0;
        if (sscanf(p, "%2d:%2d:%2d%n", &h, &m, &s, &c) >= 2 && c >= 4) {
            if (h > 23 || m > 59 || s > 59) continue;
            const char *q = p + c; while (*q == ' ') q++;
            if (str_empieza(q, "PM") && h < 12) h += 12;
            if (str_empieza(q, "AM") && h == 12) h = 0;
            snprintf(out, 9, "%02d:%02d:%02d", h, m, s);
            return true;
        }
        if (sscanf(p, "%2d:%2d%n", &h, &m, &c) == 2 && c >= 4) {
            if (h > 23 || m > 59) continue;
            const char *q = p + c; while (*q == ' ') q++;
            if (str_empieza(q, "PM") && h < 12) h += 12;
            if (str_empieza(q, "AM") && h == 12) h = 0;
            snprintf(out, 9, "%02d:%02d:00", h, m);
            return true;
        }
    }
    return false;
}

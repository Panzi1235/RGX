/* Conversión de montos en letras ("SON: CIENTO VEINTE Y OCHO CON 50/100 SOLES") a número. */
#include "sunat.h"
#include "../core/str.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

typedef struct { const char *p; long v; int tipo; } pal; /* tipo: 0 unidad, 1 decena, 2 centena, 3 multiplicador */

static const pal palabras[] = {
    {"CERO",0,0},{"UNO",1,0},{"UN",1,0},{"UNA",1,0},{"DOS",2,0},{"TRES",3,0},{"CUATRO",4,0},{"CINCO",5,0},{"SEIS",6,0},{"SIETE",7,0},{"OCHO",8,0},{"NUEVE",9,0},
    {"DIEZ",10,0},{"ONCE",11,0},{"DOCE",12,0},{"TRECE",13,0},{"CATORCE",14,0},{"QUINCE",15,0},
    {"DIECISEIS",16,0},{"DIECISIETE",17,0},{"DIECIOCHO",18,0},{"DIECINUEVE",19,0},
    {"VEINTE",20,1},{"VEINTIUNO",21,0},{"VEINTIUN",21,0},{"VEINTIUNA",21,0},{"VEINTIDOS",22,0},{"VEINTITRES",23,0},{"VEINTICUATRO",24,0},
    {"VEINTICINCO",25,0},{"VEINTISEIS",26,0},{"VEINTISIETE",27,0},{"VEINTIOCHO",28,0},{"VEINTINUEVE",29,0},
    {"TREINTA",30,1},{"CUARENTA",40,1},{"CINCUENTA",50,1},{"SESENTA",60,1},{"SETENTA",70,1},{"OCHENTA",80,1},{"NOVENTA",90,1},
    {"CIEN",100,2},{"CIENTO",100,2},{"DOSCIENTOS",200,2},{"DOSCIENTAS",200,2},{"TRESCIENTOS",300,2},{"TRESCIENTAS",300,2},
    {"CUATROCIENTOS",400,2},{"CUATROCIENTAS",400,2},{"QUINIENTOS",500,2},{"QUINIENTAS",500,2},{"SEISCIENTOS",600,2},{"SEISCIENTAS",600,2},
    {"SETECIENTOS",700,2},{"SETECIENTAS",700,2},{"OCHOCIENTOS",800,2},{"OCHOCIENTAS",800,2},{"NOVECIENTOS",900,2},{"NOVECIENTAS",900,2},
    {"MIL",1000,3},{"MILLON",1000000,3},{"MILLONES",1000000,3},
    {NULL,0,0}};

/* tolerancia a errores OCR en palabras: distancia <= 1 para palabras de 5+ letras */
static pal g_compuesta;
static const pal *buscar_palabra(const char *tok) {
    for (int i = 0; palabras[i].p; i++) if (strcmp(tok, palabras[i].p) == 0) return &palabras[i];
    /* compuestos de uso peruano: TREINTIDOS, CUARENTICINCO, NOVENTINUEVE (decena + I + unidad) */
    static const struct { const char *pre; long v; } decenas[] = {{"TREINTI",30},{"CUARENTI",40},{"CINCUENTI",50},{"SESENTI",60},{"SETENTI",70},{"OCHENTI",80},{"NOVENTI",90},{NULL,0}};
    for (int i = 0; decenas[i].pre; i++) {
        size_t l = strlen(decenas[i].pre);
        if (strncmp(tok, decenas[i].pre, l) == 0 && tok[l]) {
            for (int j = 0; palabras[j].p; j++) if (palabras[j].tipo == 0 && palabras[j].v >= 1 && palabras[j].v <= 9 && strcmp(tok + l, palabras[j].p) == 0) {
                g_compuesta.p = tok; g_compuesta.v = decenas[i].v + palabras[j].v; g_compuesta.tipo = 0; return &g_compuesta;
            }
        }
    }
    size_t l = strlen(tok);
    if (l >= 5) {
        const pal *mejor = NULL;
        for (int i = 0; palabras[i].p; i++) {
            if (strlen(palabras[i].p) < 5) continue;
            if (str_levenshtein(tok, palabras[i].p, 1) <= 1) { if (mejor) return NULL; mejor = &palabras[i]; }
        }
        return mejor;
    }
    return NULL;
}

bool sunat_linea_es_monto_en_letras(const char *linea) {
    if (!linea) return false;
    char n[512]; str_normalizar(linea, n, sizeof n, true);
    if (str_contiene(n, "SON:") || str_contiene(n, "SON ") || str_contiene(n, "IMPORTE EN LETRAS") || str_contiene(n, "MONTO EN LETRAS")) {
        return str_contiene(n, "/100") || str_contiene(n, "CON ") || str_contiene(n, "SOLES") || str_contiene(n, "DOLARES");
    }
    return str_contiene(n, "/100") && (str_contiene(n, "SOLES") || str_contiene(n, "DOLARES"));
}

bool sunat_letras_a_monto(const char *texto, double *valor, ocrx_moneda *moneda) {
    if (!texto) return false;
    char n[512]; str_normalizar(texto, n, sizeof n, true);
    if (moneda) *moneda = sunat_moneda_desde_texto(n);

    /* saltar prefijo hasta "SON" o "SON:" o "LETRAS:" */
    char *p = n;
    const char *marcas[] = {"IMPORTE EN LETRAS", "MONTO EN LETRAS", "TOTAL EN LETRAS", "SON:", "SON ", "LETRAS:", NULL};
    for (int i = 0; marcas[i]; i++) { const char *f = str_buscar(p, marcas[i]); if (f) { p = (char *)f + strlen(marcas[i]); break; } }
    while (*p == ' ' || *p == ':') p++;

    long total = 0, grupo = 0;   /* grupo acumula hasta un multiplicador */
    long centavos = -1; bool hubo = false;
    char tok[64];
    char *s = p;
    while (*s) {
        while (*s == ' ' || *s == ',' || *s == '-') s++;
        if (!*s) break;
        /* centavos "50/100" o "CON 50/100" o "CON CINCUENTA CENTIMOS" */
        if (isdigit((unsigned char)*s) || (ocr_a_digito(*s) && s[1] && (isdigit((unsigned char)s[1]) || s[1] == '/'))) {
            char d[8]; int k = 0;
            while (*s && k < 7 && (isdigit((unsigned char)*s) || ocr_a_digito(*s))) { d[k++] = isdigit((unsigned char)*s) ? *s : ocr_a_digito(*s); s++; }
            d[k] = 0;
            if (*s == '/' ) { centavos = atol(d); while (*s && *s != ' ') s++; }
            else if (!hubo && k <= 9) { total += atol(d); hubo = true; }  /* "SON 128 CON 50/100" */
            continue;
        }
        int k = 0;
        while (*s && k < 63 && (isalpha((unsigned char)*s) || (k > 0 && ocr_a_letra(*s) && (isalpha((unsigned char)s[1]) || s[1] == ' ' || s[1] == 0 || s[1] == ',')))) {
            tok[k++] = isalpha((unsigned char)*s) ? *s : ocr_a_letra(*s); s++;
        }
        tok[k] = 0;
        if (!k) { s++; continue; }
        if (strcmp(tok, "Y") == 0 || strcmp(tok, "DE") == 0) continue;
        if (strcmp(tok, "CON") == 0) {
            /* después de CON pueden venir centavos en letras: "CON CINCUENTA CENTIMOS" */
            char *t = s; while (*t == ' ') t++;
            if (!isdigit((unsigned char)*t)) {
                long c = 0; bool ok = false;
                for (;;) {
                    while (*t == ' ') t++;
                    char tk[64]; int kk = 0;
                    while (*t && kk < 63 && isalpha((unsigned char)*t)) tk[kk++] = *t++;
                    tk[kk] = 0; if (!kk) break;
                    if (strcmp(tk, "Y") == 0) continue;
                    if (str_empieza(tk, "CENTIMO") || str_empieza(tk, "CENTAVO")) { ok = true; break; }
                    const pal *w = buscar_palabra(tk);
                    if (!w || w->tipo == 3) break;
                    c += w->v;
                }
                if (ok) { centavos = c; s = t; }
            }
            continue;
        }
        if (str_empieza(tok, "SOLES") || str_empieza(tok, "SOL") || str_empieza(tok, "DOLAR") || str_empieza(tok, "NUEVOS") || str_empieza(tok, "AMERICANOS") || str_empieza(tok, "EUROS") || str_empieza(tok, "CENTIMO") || str_empieza(tok, "CENTAVO")) continue;
        const pal *w = buscar_palabra(tok);
        if (!w) { if (hubo) break; else continue; }  /* texto previo irrelevante; tras empezar, una palabra rara termina */
        hubo = true;
        if (w->tipo == 3) {
            if (w->v == 1000) { if (grupo == 0) grupo = 1; total += grupo * 1000; grupo = 0; }
            else { if (grupo == 0) grupo = 1; total = (total + grupo) * w->v; grupo = 0; }
        } else grupo += w->v;
    }
    total += grupo;
    if (!hubo) return false;
    if (centavos < 0) centavos = 0;
    if (centavos > 99) return false;
    *valor = (double)total + (double)centavos / 100.0;
    return true;
}

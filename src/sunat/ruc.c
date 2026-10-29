#include "sunat.h"
#include "../core/str.h"
#include <string.h>
#include <stdlib.h>

int sunat_ruc_digito_verificador(const char *ruc10) {
    static const int pesos[10] = {5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
    if (!ruc10) return -1;
    int suma = 0;
    for (int i = 0; i < 10; i++) {
        if (ruc10[i] < '0' || ruc10[i] > '9') return -1;
        suma += (ruc10[i] - '0') * pesos[i];
    }
    int resto = suma % 11;
    int dv = 11 - resto;
    if (dv == 10) dv = 0;
    if (dv == 11) dv = 1;
    return dv;
}

bool sunat_ruc_valido(const char *ruc) {
    if (!ruc || strlen(ruc) != 11 || !str_es_digitos(ruc)) return false;
    /* prefijos emitidos por SUNAT: 10, 15, 16, 17 personas naturales; 20 jurídicas */
    if (!(ruc[0] == '1' && (ruc[1] == '0' || ruc[1] == '5' || ruc[1] == '6' || ruc[1] == '7')) && !(ruc[0] == '2' && ruc[1] == '0'))
        return false;
    int dv = sunat_ruc_digito_verificador(ruc);
    return dv >= 0 && dv == ruc[10] - '0';
}

const char *sunat_ruc_tipo_contribuyente(const char *ruc) {
    if (!ruc || strlen(ruc) < 2) return "";
    if (ruc[0] == '2' && ruc[1] == '0') return "JURIDICA";
    if (ruc[0] == '1' && ruc[1] == '0') return "NATURAL_DNI";
    if (ruc[0] == '1') return "NATURAL_OTRO";
    return "";
}

/* Corrige una cadena OCR que debería ser un RUC.
 * Estrategia: 1) sólo dígitos; 2) forzar confusiones; 3) si hay 11 dígitos y no valida, probar cambiar un
 * dígito a la vez por sus confusiones inversas (0<->8, 1<->7, 3<->8, 5<->6, 6<->8, 9<->4...) buscando uno válido;
 * 4) si hay 12+ dígitos, probar ventanas de 11 que empiecen en 10/15/16/17/20. */
static const char *vecinos_digito(char d) {
    switch (d) {
    case '0': return "86D9";
    case '1': return "7I4";
    case '2': return "7Z";
    case '3': return "85";
    case '4': return "91";
    case '5': return "63S";
    case '6': return "58b";
    case '7': return "12";
    case '8': return "3690";
    case '9': return "407";
    default: return "";
    }
}

static bool prefijo_ok(const char *r) {
    return (r[0] == '1' && (r[1] == '0' || r[1] == '5' || r[1] == '6' || r[1] == '7')) || (r[0] == '2' && r[1] == '0');
}

static bool reparar_11(char r[12], bool *corregido) {
    if (sunat_ruc_valido(r)) return true;
    /* un dígito distinto */
    char t[12];
    for (int i = 10; i >= 0; i--) {   /* primero el DV: cambiarlo no inventa dígitos del cuerpo */
        const char *v = vecinos_digito(r[i]);
        for (; *v; v++) {
            if (*v < '0' || *v > '9') continue;
            memcpy(t, r, 12); t[i] = *v;
            if (sunat_ruc_valido(t)) { memcpy(r, t, 12); *corregido = true; return true; }
        }
    }
    /* dos dígitos distintos sólo si el prefijo ya es correcto (evitar inventar) */
    if (prefijo_ok(r)) {
        for (int i = 2; i < 11; i++) {
            const char *vi = vecinos_digito(r[i]);
            for (; *vi; vi++) {
                if (*vi < '0' || *vi > '9') continue;
                for (int j = i + 1; j < 11; j++) {
                    const char *vj = vecinos_digito(r[j]);
                    for (; *vj; vj++) {
                        if (*vj < '0' || *vj > '9') continue;
                        memcpy(t, r, 12); t[i] = *vi; t[j] = *vj;
                        if (sunat_ruc_valido(t)) { memcpy(r, t, 12); *corregido = true; return true; }
                    }
                }
            }
        }
    }
    return false;
}

bool sunat_ruc_corregir(const char *texto, char out[12], bool *corregido) {
    bool dummy; if (!corregido) corregido = &dummy;
    *corregido = false; out[0] = 0;
    if (!texto) return false;

    char d[128];
    str_solo_digitos(texto, d, sizeof d);
    size_t n = strlen(d);
    if (n == 11) {
        memcpy(out, d, 12);
        return reparar_11(out, corregido);
    }
    if (n > 11) {
        /* ventanas de 11 */
        for (size_t i = 0; i + 11 <= n; i++) {
            char w[12]; memcpy(w, d + i, 11); w[11] = 0;
            if (prefijo_ok(w) && sunat_ruc_valido(w)) { memcpy(out, w, 12); *corregido = (i != 0 || n != 11); return true; }
        }
        for (size_t i = 0; i + 11 <= n; i++) {
            char w[12]; memcpy(w, d + i, 11); w[11] = 0;
            if (prefijo_ok(w) && reparar_11(w, corregido)) { memcpy(out, w, 12); *corregido = true; return true; }
        }
    }
    /* Forzar letras a dígitos */
    char f[128];
    ocr_forzar_digitos(texto, f, sizeof f);
    n = strlen(f);
    if (n == 11) {
        memcpy(out, f, 12);
        if (reparar_11(out, corregido)) { *corregido = true; return true; }
    }
    if (n > 11) {
        for (size_t i = 0; i + 11 <= n; i++) {
            char w[12]; memcpy(w, f + i, 11); w[11] = 0;
            if (prefijo_ok(w) && reparar_11(w, corregido)) { memcpy(out, w, 12); *corregido = true; return true; }
        }
    }
    out[0] = 0;
    return false;
}

bool sunat_dni_valido(const char *dni) { return dni && strlen(dni) == 8 && str_es_digitos(dni); }

ocrx_tipo_doc_id sunat_tipo_doc_por_forma(const char *num) {
    if (!num || !*num) return OCRX_DOC_SIN;
    size_t n = strlen(num);
    if (n == 11 && str_es_digitos(num)) return OCRX_DOC_RUC;
    if (n == 8 && str_es_digitos(num)) return OCRX_DOC_DNI;
    if (n == 9 && str_es_digitos(num)) return OCRX_DOC_CARNET_EXT;
    if (n >= 6 && n <= 12) {
        bool letras = false;
        for (const char *p = num; *p; p++) if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) letras = true;
        if (letras) return OCRX_DOC_PASAPORTE;
        return OCRX_DOC_CARNET_EXT;
    }
    return OCRX_DOC_OTRO;
}

const char *sunat_tipo_doc_codigo(ocrx_tipo_doc_id t) {
    switch (t) {
    case OCRX_DOC_SIN: return "0"; case OCRX_DOC_DNI: return "1"; case OCRX_DOC_CARNET_EXT: return "4";
    case OCRX_DOC_RUC: return "6"; case OCRX_DOC_PASAPORTE: return "7"; case OCRX_DOC_CED_DIPLOMATICA: return "A";
    case OCRX_DOC_PTP: return "B"; case OCRX_DOC_TAM: return "C"; default: return "-";
    }
}
ocrx_tipo_doc_id sunat_tipo_doc_desde_codigo(const char *c) {
    if (!c || !*c) return OCRX_DOC_SIN;
    switch (c[0]) {
    case '0': return OCRX_DOC_SIN; case '1': return OCRX_DOC_DNI; case '4': return OCRX_DOC_CARNET_EXT;
    case '6': return OCRX_DOC_RUC; case '7': return OCRX_DOC_PASAPORTE; case 'A': case 'a': return OCRX_DOC_CED_DIPLOMATICA;
    case 'B': case 'b': return OCRX_DOC_PTP; case 'C': case 'c': return OCRX_DOC_TAM; default: return OCRX_DOC_OTRO;
    }
}

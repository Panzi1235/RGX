#include "sunat.h"
#include "../core/str.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

ocrx_tipo_cpe sunat_tipo_desde_codigo(const char *c) {
    if (!c) return OCRX_CPE_DESCONOCIDO;
    while (*c == ' ') c++;
    int v = atoi(c);
    switch (v) {
    case 1: return OCRX_CPE_FACTURA; case 2: return OCRX_CPE_RECIBO_HONORARIOS; case 3: return OCRX_CPE_BOLETA;
    case 4: return OCRX_CPE_LIQUIDACION_COMPRA; case 7: return OCRX_CPE_NOTA_CREDITO; case 8: return OCRX_CPE_NOTA_DEBITO;
    case 9: return OCRX_CPE_GUIA_REMITENTE; case 12: return OCRX_CPE_TICKET_MAQUINA; case 14: return OCRX_CPE_RECIBO_SERVICIOS;
    case 20: return OCRX_CPE_RETENCION; case 31: return OCRX_CPE_GUIA_TRANSPORTISTA; case 40: return OCRX_CPE_PERCEPCION;
    default: return OCRX_CPE_DESCONOCIDO;
    }
}
const char *sunat_tipo_codigo(ocrx_tipo_cpe t) {
    switch (t) {
    case OCRX_CPE_FACTURA: return "01"; case OCRX_CPE_RECIBO_HONORARIOS: return "02"; case OCRX_CPE_BOLETA: return "03";
    case OCRX_CPE_LIQUIDACION_COMPRA: return "04"; case OCRX_CPE_NOTA_CREDITO: return "07"; case OCRX_CPE_NOTA_DEBITO: return "08";
    case OCRX_CPE_GUIA_REMITENTE: return "09"; case OCRX_CPE_TICKET_MAQUINA: return "12"; case OCRX_CPE_RECIBO_SERVICIOS: return "14";
    case OCRX_CPE_RETENCION: return "20"; case OCRX_CPE_GUIA_TRANSPORTISTA: return "31"; case OCRX_CPE_PERCEPCION: return "40";
    default: return "00";
    }
}
const char *sunat_tipo_nombre(ocrx_tipo_cpe t) {
    switch (t) {
    case OCRX_CPE_FACTURA: return "FACTURA"; case OCRX_CPE_RECIBO_HONORARIOS: return "RECIBO POR HONORARIOS";
    case OCRX_CPE_BOLETA: return "BOLETA DE VENTA"; case OCRX_CPE_LIQUIDACION_COMPRA: return "LIQUIDACION DE COMPRA";
    case OCRX_CPE_NOTA_CREDITO: return "NOTA DE CREDITO"; case OCRX_CPE_NOTA_DEBITO: return "NOTA DE DEBITO";
    case OCRX_CPE_GUIA_REMITENTE: return "GUIA DE REMISION REMITENTE"; case OCRX_CPE_TICKET_MAQUINA: return "TICKET";
    case OCRX_CPE_RECIBO_SERVICIOS: return "RECIBO DE SERVICIOS PUBLICOS"; case OCRX_CPE_RETENCION: return "COMPROBANTE DE RETENCION";
    case OCRX_CPE_GUIA_TRANSPORTISTA: return "GUIA DE REMISION TRANSPORTISTA"; case OCRX_CPE_PERCEPCION: return "COMPROBANTE DE PERCEPCION";
    default: return "DESCONOCIDO";
    }
}

/* texto ya normalizado (mayúsculas, sin tildes) */
ocrx_tipo_cpe sunat_tipo_desde_titulo(const char *t) {
    if (!t) return OCRX_CPE_DESCONOCIDO;
    /* Orden importa: "NOTA DE CREDITO" contiene palabras que podrían confundirse; chequear compuestos primero */
    if (str_contiene(t, "NOTA DE CREDITO") || str_contiene(t, "NOTA CREDITO") || str_contiene(t, "N. CREDITO") || str_contiene(t, "NOTA DE CR")) return OCRX_CPE_NOTA_CREDITO;
    if (str_contiene(t, "NOTA DE DEBITO") || str_contiene(t, "NOTA DEBITO") || str_contiene(t, "N. DEBITO")) return OCRX_CPE_NOTA_DEBITO;
    if (str_contiene(t, "GUIA DE REMISION") || str_contiene(t, "GUIA REMISION")) {
        if (str_contiene(t, "TRANSPORTISTA")) return OCRX_CPE_GUIA_TRANSPORTISTA;
        return OCRX_CPE_GUIA_REMITENTE;
    }
    if (str_contiene(t, "COMPROBANTE DE RETENCION") || str_contiene(t, "COMPROBANTE RETENCION")) return OCRX_CPE_RETENCION;
    if (str_contiene(t, "COMPROBANTE DE PERCEPCION") || str_contiene(t, "COMPROBANTE PERCEPCION")) return OCRX_CPE_PERCEPCION;
    if (str_contiene(t, "RECIBO POR HONORARIOS") || str_contiene(t, "RECIBO DE HONORARIOS") || str_contiene(t, "R.H.E") || str_contiene(t, "RHE ")) return OCRX_CPE_RECIBO_HONORARIOS;
    if (str_contiene(t, "LIQUIDACION DE COMPRA")) return OCRX_CPE_LIQUIDACION_COMPRA;
    if (str_contiene(t, "FACTURA")) return OCRX_CPE_FACTURA;
    if (str_contiene(t, "BOLETA")) return OCRX_CPE_BOLETA;
    if (str_contiene(t, "TICKET") || str_contiene(t, "TIKET")) return OCRX_CPE_TICKET_MAQUINA;
    return OCRX_CPE_DESCONOCIDO;
}

bool sunat_serie_es_electronica(const char *s) {
    if (!s || strlen(s) != 4) return false;
    char c = (char)toupper((unsigned char)s[0]);
    if (c != 'F' && c != 'B' && c != 'E' && c != 'T' && c != 'R' && c != 'P' && c != 'V') return false;
    for (int i = 1; i < 4; i++) if (!isalnum((unsigned char)s[i])) return false;
    return true;
}

bool sunat_serie_valida(const char *s, ocrx_tipo_cpe tipo) {
    if (!s || !*s) return false;
    size_t n = strlen(s);
    if (n != 4) return false;
    char c = (char)toupper((unsigned char)s[0]);
    switch (tipo) {
    case OCRX_CPE_FACTURA: return c == 'F' || c == 'E';                /* E001 = emitida en portal SUNAT */
    case OCRX_CPE_BOLETA:  return c == 'B' || c == 'E';                /* EB01 = boleta del portal */
    case OCRX_CPE_NOTA_CREDITO: case OCRX_CPE_NOTA_DEBITO: return c == 'F' || c == 'B' || c == 'E';
    case OCRX_CPE_GUIA_REMITENTE: case OCRX_CPE_GUIA_TRANSPORTISTA: return c == 'T' || c == 'E' || c == 'V';
    case OCRX_CPE_RETENCION: return c == 'R' || c == 'E';
    case OCRX_CPE_PERCEPCION: return c == 'P' || c == 'E';
    case OCRX_CPE_RECIBO_HONORARIOS: return c == 'E';
    case OCRX_CPE_TICKET_MAQUINA: return true;                          /* series libres */
    default: return isalnum((unsigned char)s[0]) != 0;
    }
}

ocrx_tipo_cpe sunat_tipo_desde_serie(const char *serie, ocrx_tipo_cpe pista) {
    if (!serie || strlen(serie) < 4) return pista;
    char c = (char)toupper((unsigned char)serie[0]);
    char c2 = (char)toupper((unsigned char)serie[1]);
    /* notas de crédito/débito usan serie F/B: si la pista ya lo dice, respetar */
    if (pista == OCRX_CPE_NOTA_CREDITO || pista == OCRX_CPE_NOTA_DEBITO) return pista;
    switch (c) {
    case 'F': return OCRX_CPE_FACTURA;
    case 'B': return OCRX_CPE_BOLETA;
    case 'T': return OCRX_CPE_GUIA_REMITENTE;
    case 'R': return OCRX_CPE_RETENCION;
    case 'P': return OCRX_CPE_PERCEPCION;
    case 'V': return OCRX_CPE_GUIA_TRANSPORTISTA;
    case 'E':
        if (c2 == 'B') return OCRX_CPE_BOLETA;
        if (pista != OCRX_CPE_DESCONOCIDO) return pista;
        return OCRX_CPE_FACTURA;  /* E001 por defecto factura del portal; el título decide si es RHE */
    default: return pista;
    }
}

/* Letras posibles en primera posición de serie según tipo */
static char letra_serie_esperada(ocrx_tipo_cpe tipo) {
    switch (tipo) {
    case OCRX_CPE_FACTURA: return 'F';
    case OCRX_CPE_BOLETA: return 'B';
    case OCRX_CPE_GUIA_REMITENTE: return 'T';
    case OCRX_CPE_RETENCION: return 'R';
    case OCRX_CPE_PERCEPCION: return 'P';
    case OCRX_CPE_RECIBO_HONORARIOS: return 'E';
    default: return 0;
    }
}

/* Parsea "F001-00000123", "F001 - 123", "F001 N° 123", "Nro. F001-123", "FOO1-00O123", "EB01-45", "0001-000123" (físico) */
bool sunat_serie_numero_parsear(const char *texto, ocrx_tipo_cpe pista, char serie[8], char numero[12], bool *corregido) {
    bool dummy; if (!corregido) corregido = &dummy;
    *corregido = false; serie[0] = 0; numero[0] = 0;
    if (!texto) return false;

    /* Extraer tokens alfanuméricos; buscar patrón SSSS <sep> NNN... */
    char buf[256]; str_normalizar(texto, buf, sizeof buf, true);
    /* quitar prefijos comunes */
    static const char *prefijos[] = {"NRO.", "NRO", "NO.", "N°", "N\xC2\xB0", "NUM.", "NUMERO", "N.", "#", NULL};
    char *p = buf;
    for (;;) {
        bool hubo = false;
        while (*p == ' ' || *p == ':') p++;
        for (int i = 0; prefijos[i]; i++) if (str_empieza(p, prefijos[i])) { p += strlen(prefijos[i]); hubo = true; break; }
        if (!hubo) break;
    }

    /* Recorrer buscando un bloque de 4 alfanuméricos seguido de separador (-, espacio, N°) y dígitos */
    for (char *s = p; *s; s++) {
        if (!isalnum((unsigned char)*s)) continue;
        /* candidato de serie: 4 alfanuméricos */
        char cand[8] = {0}; int k = 0; char *q = s;
        while (*q && isalnum((unsigned char)*q) && k < 7) cand[k++] = *q++;
        cand[k] = 0;
        if (k != 4) { s = q - 1; continue; }
        *corregido = false;   /* cada candidato parte limpio */
        /* separador */
        char *r = q;
        while (*r == ' ' || *r == '-' || *r == '\xE2' || *r == ':' || *r == '.' || *r == '_' || *r == '/') r++;
        if (str_empieza(r, "N°")) { r += 3; while (*r == ' ' || *r == '.' || *r == ':') r++; }
        else if (str_empieza(r, "NRO")) { r += 3; while (*r == '.' || *r == ' ' || *r == ':') r++; }
        else if (str_empieza(r, "NO.")) { r += 3; while (*r == ' ') r++; }
        else if (str_empieza(r, "#")) { r += 1; while (*r == ' ') r++; }
        if (r == q) { /* sin separador: ¿"F00100000123"? */
            if (!(isdigit((unsigned char)*r))) { s = q - 1; continue; }
        }
        /* número: dígitos con posibles confusiones */
        char num[32] = {0}; int nk = 0; char *z = r;
        while (*z && nk < 31 && (isdigit((unsigned char)*z) || ocr_a_digito(*z))) {
            if (isdigit((unsigned char)*z)) num[nk++] = *z;
            else { num[nk++] = ocr_a_digito(*z); *corregido = true; }
            z++;
        }
        if (nk == 0 || nk > 12) { s = q - 1; continue; }
        /* si el número está pegado a más letras, no es un número de comprobante (p.ej. "F001-ABC") */
        if (isalpha((unsigned char)*z) && !(*z == 'N' || *z == 'F')) { /* permitir "FECHA" después */ }

        /* Normalizar serie: 1ª letra, resto dígitos salvo EB01 / E001 */
        char ser[8]; memcpy(ser, cand, 5);
        char esperada = letra_serie_esperada(pista);
        char c0 = ser[0];
        if (isdigit((unsigned char)c0)) {
            /* serie numérica: comprobante físico (0001) o confusión (F->F no numérica). Si la pista espera letra, convertir. */
            if (esperada) { char l = ocr_a_letra(c0); if (l == esperada || (esperada == 'F' && c0 == '7') || (esperada == 'B' && c0 == '8') || (esperada == 'E' && c0 == '3')) { ser[0] = esperada; *corregido = true; } }
        } else {
            /* letra: ¿es una válida? Corregir confusiones de letras entre sí */
            char u = (char)toupper((unsigned char)c0);
            if (esperada && u != esperada) {
                if ((esperada == 'F' && (u == 'P' || u == 'T')) ||
                    (esperada == 'B' && (u == '8' || u == 'R' || u == 'D' || u == 'E')) ||
                    (esperada == 'E' && (u == 'F' || u == 'B'))) { /* B vs E ambiguos: sólo si no es EB01 */
                    if (!(u == 'E' && toupper((unsigned char)ser[1]) == 'B')) { ser[0] = esperada; *corregido = true; }
                } else if (!(u == 'E' && toupper((unsigned char)ser[1]) == 'B')) ser[0] = u;
            } else ser[0] = u;
        }
        /* posiciones 1..3: EB01 permite letra en pos 1; el resto dígitos */
        for (int i = 1; i < 4; i++) {
            char c = ser[i];
            if (isdigit((unsigned char)c)) continue;
            if (i == 1 && (c == 'B' || c == 'b') && ser[0] == 'E') { ser[1] = 'B'; continue; }
            char d = ocr_a_digito(c);
            if (d) { ser[i] = d; *corregido = true; }
            else { ser[0] = 0; break; }
        }
        if (!ser[0]) { s = q - 1; continue; }
        ser[4] = 0;
        /* número: quitar ceros a la izquierda pero mantener al menos 1 */
        char *n0 = num; while (*n0 == '0' && n0[1]) n0++;
        if (strlen(n0) > 8) { s = q - 1; continue; }
        str_copiar(serie, 8, ser);
        str_copiar(numero, 12, n0);
        return true;
    }
    return false;
}

/* Cuentas cruzadas de un comprobante SUNAT y reparación de confusiones OCR. */
#include "sunat.h"
#include "../core/str.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

static const double TASAS[] = {0.18, 0.105, 0.10, 0.08, 0.0}; /* 18 % general; 10.5 % y 8 % (Ley 31556 restaurantes/hoteles MYPE, por tramos de año) */

static double redondear2(double v) { return floor(v * 100.0 + 0.5) / 100.0; }

static double base_imponible(const ocrx_totales *t) { return t->gravada; }

static double total_calculado(const ocrx_totales *t) {
    return t->gravada + t->exonerada + t->inafecta + t->exportacion
         + t->igv + t->isc + t->icbper + t->otros_tributos + t->otros_cargos - t->descuentos;
}

sunat_cuadre sunat_cuadrar(ocrx_totales *t) {
    sunat_cuadre c = {0};
    double calc = total_calculado(t);
    c.diferencia = redondear2(calc - t->total);
    c.cuadra = t->total > 0 && sunat_monto_igual(calc, t->total);
    double base = base_imponible(t);
    c.tasa_detectada = 0;
    if (base > 0 && t->igv > 0) {
        for (int i = 0; TASAS[i] > 0; i++) {
            if (fabs(base * TASAS[i] - t->igv) < 0.02 + base * 0.0005) { c.tasa_detectada = TASAS[i]; break; }
        }
        c.igv_coherente = c.tasa_detectada > 0;
    } else if (base == 0 && t->igv == 0) c.igv_coherente = true;  /* sólo exonerado/inafecto */
    else c.igv_coherente = false;
    if (c.tasa_detectada > 0) t->tasa_igv = c.tasa_detectada;
    snprintf(c.detalle, sizeof c.detalle, "gravada=%.2f exo=%.2f ina=%.2f igv=%.2f dscto=%.2f otros=%.2f calc=%.2f total=%.2f tasa=%.3f",
             t->gravada, t->exonerada, t->inafecta, t->igv, t->descuentos, t->otros_cargos, calc, t->total, c.tasa_detectada);
    t->cuadra = c.cuadra;
    return c;
}

size_t sunat_monto_variantes(double v, double *out, size_t cap) {
    /* Representar con 2 decimales y variar cada dígito por confusiones típicas; también separador decimal corrido */
    char s[32]; snprintf(s, sizeof s, "%.2f", fabs(v));
    size_t n = 0;
    static const char *conf[10] = {"86", "7", "7", "8", "9", "68", "58", "12", "306", "40"};
    for (size_t i = 0; s[i] && n < cap; i++) {
        if (s[i] < '0' || s[i] > '9') continue;
        const char *c = conf[s[i] - '0'];
        for (; *c && n < cap; c++) {
            char t[32]; memcpy(t, s, sizeof t); t[i] = *c;
            double w = atof(t); if (v < 0) w = -w;
            bool dup = false; for (size_t k = 0; k < n; k++) if (sunat_monto_igual(out[k], w)) dup = true;
            if (!dup && !sunat_monto_igual(w, v)) out[n++] = w;
        }
    }
    /* decimal corrido: 1180.0 -> 118.00, 11.80 -> 118.0 */
    if (n < cap) out[n++] = v * 10.0;
    if (n < cap) out[n++] = v / 10.0;
    /* dígito perdido al inicio (1 de 1xx) */
    if (n < cap && fabs(v) < 1000) out[n++] = v + 100.0;
    if (n < cap && fabs(v) < 10000 && fabs(v) >= 1000) out[n++] = v + 1000.0;
    return n;
}

/* Completa lo que falta a partir de lo que hay y verifica. */
static void completar(ocrx_totales *t) {
    bool hay_g = t->gravada > 0, hay_i = t->igv > 0, hay_t = t->total > 0;
    double no_grav = t->exonerada + t->inafecta + t->exportacion + t->otros_cargos - t->descuentos + t->isc + t->icbper + t->otros_tributos;
    if (hay_g && hay_i && !hay_t) { t->total = redondear2(t->gravada + t->igv + no_grav); t->m_total.origen = OCRX_ORIGEN_CALCULO; t->m_total.presente = true; t->m_total.confianza = 0.9f; }
    else if (hay_g && !hay_i && hay_t) {
        double igv = redondear2(t->total - no_grav - t->gravada);
        if (igv >= 0) {
            /* ¿la gravada leída tiene un dígito corrido? Si total/(1+tasa) cae a <= 2 centavos de ella, preferir la derivada (coherente con la tasa) */
            for (int i = 0; TASAS[i] > 0; i++) {
                double g2 = redondear2((t->total - no_grav) / (1.0 + TASAS[i]));
                double igv2 = redondear2(t->total - no_grav - g2);
                double err_leida = fabs(t->gravada * TASAS[i] - igv), err_deriv = fabs(g2 * TASAS[i] - igv2);
                if (fabs(g2 - t->gravada) <= 0.02 && fabs(g2 - t->gravada) > 0.001 && err_deriv < 0.011 && err_deriv + 0.003 < err_leida) {
                    t->gravada = g2; igv = igv2; t->m_gravada.corregido = true; t->tasa_igv = TASAS[i]; break;
                }
            }
            t->igv = igv; t->m_igv.origen = OCRX_ORIGEN_CALCULO; t->m_igv.presente = true; t->m_igv.confianza = 0.85f;
        }
    } else if (!hay_g && hay_i && hay_t) {
        double g = redondear2(t->total - no_grav - t->igv);
        if (g >= 0) { t->gravada = g; t->m_gravada.origen = OCRX_ORIGEN_CALCULO; t->m_gravada.presente = true; t->m_gravada.confianza = 0.85f; }
    } else if (!hay_g && !hay_i && hay_t && no_grav == 0) {
        /* Sólo total (ticket): asumir 18 % salvo que el comprobante diga otra tasa */
        double tasa = t->tasa_igv > 0 ? t->tasa_igv : 0.18;
        t->gravada = redondear2(t->total / (1.0 + tasa));
        t->igv = redondear2(t->total - t->gravada);
        t->m_gravada.origen = t->m_igv.origen = OCRX_ORIGEN_CALCULO;
        t->m_gravada.presente = t->m_igv.presente = true;
        t->m_gravada.confianza = t->m_igv.confianza = 0.6f;
        t->m_gravada.dudoso = t->m_igv.dudoso = true;
    } else if (hay_g && !hay_i && !hay_t) {
        double tasa = t->tasa_igv > 0 ? t->tasa_igv : 0.18;
        t->igv = redondear2(t->gravada * tasa);
        t->total = redondear2(t->gravada + t->igv + no_grav);
        t->m_igv.origen = t->m_total.origen = OCRX_ORIGEN_CALCULO;
        t->m_igv.presente = t->m_total.presente = true;
        t->m_igv.confianza = t->m_total.confianza = 0.6f;
        t->m_igv.dudoso = t->m_total.dudoso = true;
    }
}

bool sunat_totales_reparar(ocrx_totales *t, double monto_letras, bool tiene_letras) {
    /* 0) limpiar negativos absurdos */
    if (t->total < 0) t->total = -t->total;

    /* 1) el monto en letras manda si difiere del total leído y cuadra con el resto */
    if (tiene_letras && monto_letras > 0 && !sunat_monto_igual(monto_letras, t->total)) {
        ocrx_totales p = *t; p.total = monto_letras;
        sunat_cuadre c = sunat_cuadrar(&p);
        if (c.cuadra || t->total == 0) {
            t->total = monto_letras; t->m_total.origen = OCRX_ORIGEN_LETRAS; t->m_total.corregido = true; t->m_total.confianza = 0.95f; t->m_total.presente = true;
        }
    }

    completar(t);
    sunat_cuadre c = sunat_cuadrar(t);
    if (c.cuadra && (c.igv_coherente || t->gravada == 0)) return true;

    /* 2) probar variantes OCR de un campo a la vez: total, igv, gravada, exonerada, inafecta, descuentos */
    double *campos[] = {&t->total, &t->igv, &t->gravada, &t->exonerada, &t->inafecta, &t->descuentos, &t->otros_cargos};
    ocrx_campo_meta *metas[] = {&t->m_total, &t->m_igv, &t->m_gravada, &t->m_exonerada, &t->m_inafecta, NULL, NULL};
    for (int pasada = 0; pasada < 2; pasada++) {
        for (size_t f = 0; f < sizeof campos / sizeof *campos; f++) {
            if (*campos[f] == 0 && f > 2) continue;
            /* no tocar el total si vino de letras o QR/XML */
            if (metas[f] && (metas[f]->origen == OCRX_ORIGEN_QR || metas[f]->origen == OCRX_ORIGEN_XML || metas[f]->origen == OCRX_ORIGEN_LETRAS)) continue;
            double orig = *campos[f];
            double vs[64]; size_t nv = sunat_monto_variantes(orig, vs, 64);
            for (size_t k = 0; k < nv; k++) {
                if (vs[k] < 0) continue;
                *campos[f] = vs[k];
                sunat_cuadre cc = sunat_cuadrar(t);
                if (cc.cuadra && (cc.igv_coherente || t->gravada == 0)) {
                    if (metas[f]) { metas[f]->corregido = true; metas[f]->confianza = 0.8f; metas[f]->dudoso = true; }
                    return true;
                }
            }
            *campos[f] = orig;
        }
        /* 3) tal vez falta el IGV porque no se leyó: recalcular desde gravada */
        if (pasada == 0 && t->gravada > 0) {
            for (int i = 0; TASAS[i] > 0; i++) {
                double igv = redondear2(t->gravada * TASAS[i]);
                ocrx_totales p = *t; p.igv = igv;
                sunat_cuadre cc = sunat_cuadrar(&p);
                if (cc.cuadra) { t->igv = igv; t->m_igv.corregido = true; t->m_igv.origen = OCRX_ORIGEN_CALCULO; t->tasa_igv = TASAS[i]; sunat_cuadrar(t); return true; }
            }
        }
    }
    /* 4) ¿la "gravada" leída era en realidad el total con IGV incluido? (tickets que ponen "SUBTOTAL" = total) */
    if (t->gravada > 0 && t->igv > 0 && sunat_monto_igual(t->gravada, t->total)) {
        double g = redondear2(t->total - t->igv);
        if (fabs(g * 0.18 - t->igv) < 0.05 || fabs(g * 0.105 - t->igv) < 0.05) {
            t->gravada = g; t->m_gravada.corregido = true; t->m_gravada.origen = OCRX_ORIGEN_CALCULO;
            sunat_cuadrar(t); return t->cuadra;
        }
    }
    sunat_cuadrar(t);
    return t->cuadra;
}

const char *sunat_afectacion_nombre(const char *c) {
    if (!c) return "";
    int v = atoi(c);
    switch (v) {
    case 10: return "GRAVADO - OPERACION ONEROSA";
    case 11: case 12: case 13: case 14: case 15: case 16: case 17: return "GRAVADO - RETIRO/GRATUITO";
    case 20: return "EXONERADO - OPERACION ONEROSA";
    case 21: return "EXONERADO - TRANSFERENCIA GRATUITA";
    case 30: return "INAFECTO - OPERACION ONEROSA";
    case 31: case 32: case 33: case 34: case 35: case 36: case 37: return "INAFECTO - RETIRO/GRATUITO";
    case 40: return "EXPORTACION";
    default: return "";
    }
}

const char *sunat_unidad_normalizar(const char *u) {
    if (!u) return "NIU";
    char n[16]; str_normalizar(u, n, sizeof n, true);
    if (!n[0]) return "NIU";
    if (!strcmp(n, "NIU") || !strcmp(n, "UND") || !strcmp(n, "UNID") || !strcmp(n, "UNIDAD") || !strcmp(n, "UNIDADES") || !strcmp(n, "U") || !strcmp(n, "PZA") || !strcmp(n, "PIEZA")) return "NIU";
    if (!strcmp(n, "ZZ") || !strcmp(n, "SERV") || !strcmp(n, "SERVICIO") || !strcmp(n, "SERVICIOS") || !strcmp(n, "SRV")) return "ZZ";
    if (!strcmp(n, "KGM") || !strcmp(n, "KG") || !strcmp(n, "KILO") || !strcmp(n, "KILOS") || !strcmp(n, "KILOGRAMO")) return "KGM";
    if (!strcmp(n, "GRM") || !strcmp(n, "GR") || !strcmp(n, "G") || !strcmp(n, "GRAMO")) return "GRM";
    if (!strcmp(n, "LTR") || !strcmp(n, "LT") || !strcmp(n, "L") || !strcmp(n, "LITRO") || !strcmp(n, "LITROS")) return "LTR";
    if (!strcmp(n, "GLL") || !strcmp(n, "GAL") || !strcmp(n, "GALON") || !strcmp(n, "GALONES") || !strcmp(n, "GLN")) return "GLL";
    if (!strcmp(n, "MTR") || !strcmp(n, "M") || !strcmp(n, "MT") || !strcmp(n, "METRO") || !strcmp(n, "METROS")) return "MTR";
    if (!strcmp(n, "MTK") || !strcmp(n, "M2")) return "MTK";
    if (!strcmp(n, "MTQ") || !strcmp(n, "M3")) return "MTQ";
    if (!strcmp(n, "BX") || !strcmp(n, "CJA") || !strcmp(n, "CAJA") || !strcmp(n, "CAJAS")) return "BX";
    if (!strcmp(n, "PK") || !strcmp(n, "PAQ") || !strcmp(n, "PAQUETE")) return "PK";
    if (!strcmp(n, "DZN") || !strcmp(n, "DOC") || !strcmp(n, "DOCENA")) return "DZN";
    if (!strcmp(n, "HUR") || !strcmp(n, "HR") || !strcmp(n, "HORA") || !strcmp(n, "HORAS")) return "HUR";
    if (!strcmp(n, "DAY") || !strcmp(n, "DIA") || !strcmp(n, "DIAS")) return "DAY";
    if (!strcmp(n, "MON") || !strcmp(n, "MES") || !strcmp(n, "MESES")) return "MON";
    if (!strcmp(n, "BO") || !strcmp(n, "BOT") || !strcmp(n, "BOTELLA")) return "BO";
    if (!strcmp(n, "BG") || !strcmp(n, "BLS") || !strcmp(n, "BOLSA")) return "BG";
    if (!strcmp(n, "SET") || !strcmp(n, "JGO") || !strcmp(n, "JUEGO") || !strcmp(n, "KIT")) return "SET";
    if (!strcmp(n, "TNE") || !strcmp(n, "TON") || !strcmp(n, "TONELADA")) return "TNE";
    if (!strcmp(n, "GLI") || !strcmp(n, "GLN IMP")) return "GLI";
    return "NIU";
}

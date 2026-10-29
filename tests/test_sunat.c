/* Pruebas de las reglas SUNAT y de la extracción por texto. Se compilan y corren con cualquier compilador C. */
#include "../include/ocrx.h"
#include "../src/sunat/sunat.h"
#include "../src/sunat/qr.h"
#include "../src/core/str.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fallos = 0, total = 0;
#define OK(cond, ...) do { total++; if (!(cond)) { fallos++; printf("  FALLO %s:%d: ", __FILE__, __LINE__); printf("" __VA_ARGS__); printf("\n"); } } while (0)
#define EQS(a, b) OK(strcmp((a), (b)) == 0, "'%s' != '%s'", (a), (b))
#define EQD(a, b) OK(fabs((a) - (b)) < 0.011, "%.2f != %.2f", (double)(a), (double)(b))

static void t_ruc(void) {
    OK(sunat_ruc_valido("20100070970"), "RUC valido (Backus)");
    OK(sunat_ruc_valido("20131312955"), "RUC SUNAT");
    OK(sunat_ruc_valido("10467896812") || !sunat_ruc_valido("10467896812"), "no crash");
    OK(!sunat_ruc_valido("20100070971"), "DV incorrecto");
    OK(!sunat_ruc_valido("30100070970"), "prefijo invalido");
    OK(!sunat_ruc_valido("2010007097"), "10 digitos");
    char out[12]; bool corr;
    OK(sunat_ruc_corregir("RUC: 2O1OOO7O97O", out, &corr) && !strcmp(out, "20100070970"), "O->0: %s", out);
    OK(sunat_ruc_corregir("R.U.C. 20100070970", out, &corr) && !strcmp(out, "20100070970") && !corr, "limpio");
    OK(sunat_ruc_corregir("20100070978", out, &corr) && !strcmp(out, "20100070970") && corr, "DV confundido 8/0: %s", out);
    OK(sunat_ruc_corregir("RUC 20131312955 F001", out, &corr) && !strcmp(out, "20131312955"), "con ruido");
    OK(!sunat_ruc_corregir("hola mundo", out, &corr), "sin digitos");
    OK(sunat_ruc_digito_verificador("2010007097") == 0, "dv");
    OK(sunat_dni_valido("46789681") && !sunat_dni_valido("4678968"), "dni");
    OK(sunat_tipo_doc_por_forma("46789681") == OCRX_DOC_DNI, "forma dni");
    OK(sunat_tipo_doc_por_forma("20100070970") == OCRX_DOC_RUC, "forma ruc");
    OK(sunat_tipo_doc_por_forma("001234567") == OCRX_DOC_CARNET_EXT, "forma ce");
}

static void t_serie(void) {
    char s[8], n[12]; bool c;
    OK(sunat_serie_numero_parsear("F001-00000123", OCRX_CPE_FACTURA, s, n, &c) && !strcmp(s, "F001") && !strcmp(n, "123"), "basico %s %s", s, n);
    OK(sunat_serie_numero_parsear("FOO1-OOO123", OCRX_CPE_FACTURA, s, n, &c) && !strcmp(s, "F001") && !strcmp(n, "123") && c, "confusiones %s %s", s, n);
    OK(sunat_serie_numero_parsear("Nro. B001 - 4567", OCRX_CPE_BOLETA, s, n, &c) && !strcmp(s, "B001") && !strcmp(n, "4567"), "espacios %s %s", s, n);
    OK(sunat_serie_numero_parsear("EB01-45", OCRX_CPE_BOLETA, s, n, &c) && !strcmp(s, "EB01") && !strcmp(n, "45"), "EB01 %s %s", s, n);
    OK(sunat_serie_numero_parsear("E001-77", OCRX_CPE_FACTURA, s, n, &c) && !strcmp(s, "E001"), "E001 %s", s);
    OK(sunat_serie_numero_parsear("FACTURA ELECTRONICA F012 N° 0000345", OCRX_CPE_FACTURA, s, n, &c) && !strcmp(s, "F012") && !strcmp(n, "345"), "N° %s %s", s, n);
    OK(sunat_serie_numero_parsear("T001-000045", OCRX_CPE_GUIA_REMITENTE, s, n, &c) && !strcmp(s, "T001"), "guia");
    OK(sunat_serie_numero_parsear("RUC: 20100070970", OCRX_CPE_FACTURA, s, n, &c) == false, "RUC no es serie");
    OK(sunat_serie_numero_parsear("F001-123", OCRX_CPE_DESCONOCIDO, s, n, &c), "sin pista");
    OK(sunat_tipo_desde_serie("F001", OCRX_CPE_DESCONOCIDO) == OCRX_CPE_FACTURA, "tipo F");
    OK(sunat_tipo_desde_serie("B001", OCRX_CPE_DESCONOCIDO) == OCRX_CPE_BOLETA, "tipo B");
    OK(sunat_tipo_desde_serie("EB01", OCRX_CPE_DESCONOCIDO) == OCRX_CPE_BOLETA, "tipo EB");
    OK(sunat_tipo_desde_serie("F001", OCRX_CPE_NOTA_CREDITO) == OCRX_CPE_NOTA_CREDITO, "NC conserva");
    OK(sunat_tipo_desde_titulo("NOTA DE CREDITO ELECTRONICA") == OCRX_CPE_NOTA_CREDITO, "titulo NC");
    OK(sunat_tipo_desde_titulo("BOLETA DE VENTA ELECTRONICA") == OCRX_CPE_BOLETA, "titulo B");
    OK(sunat_tipo_desde_titulo("FACTURA ELECTRONICA") == OCRX_CPE_FACTURA, "titulo F");
    OK(sunat_serie_valida("F001", OCRX_CPE_FACTURA) && !sunat_serie_valida("B001", OCRX_CPE_FACTURA), "serie valida");
}

static void t_montos(void) {
    sunat_monto m;
    m = sunat_monto_parsear("S/ 1,234.56"); OK(m.ok, "ok"); EQD(m.valor, 1234.56); OK(m.moneda == OCRX_MON_PEN, "PEN");
    m = sunat_monto_parsear("S/.1.234,56"); EQD(m.valor, 1234.56);
    m = sunat_monto_parsear("US$ 12.00"); EQD(m.valor, 12.00); OK(m.moneda == OCRX_MON_USD, "USD");
    m = sunat_monto_parsear("-5.00"); EQD(m.valor, -5.00);
    m = sunat_monto_parsear("(5.00)"); EQD(m.valor, -5.00);
    m = sunat_monto_parsear("1 234,56"); EQD(m.valor, 1234.56);
    m = sunat_monto_parsear("118.0O"); EQD(m.valor, 118.00);
    m = sunat_monto_parsear("1.234"); EQD(m.valor, 1234);
    m = sunat_monto_parsear("12,5"); EQD(m.valor, 12.5);
    m = sunat_monto_parsear("0.90"); EQD(m.valor, 0.9);
    sunat_monto ms[8];
    size_t n = sunat_montos_en_linea("IGV 18% S/ 18.00", ms, 8); OK(n == 1, "n=%zu", n); if (n) EQD(ms[0].valor, 18.0);
    n = sunat_montos_en_linea("FECHA: 15/10/2026 TOTAL 118.00", ms, 8); OK(n == 1, "fecha filtrada n=%zu", n); if (n) EQD(ms[0].valor, 118.0);
    n = sunat_montos_en_linea("RUC 20100070970 F001-123", ms, 8); OK(n == 0, "ruc filtrado n=%zu", n);
    n = sunat_montos_en_linea("2 UND  GASEOSA 1.5L  3.50  7.00", ms, 8); OK(n >= 2, "items n=%zu", n); if (n >= 2) EQD(ms[n - 1].valor, 7.0);
    OK(sunat_moneda_desde_texto("SOLES") == OCRX_MON_PEN && sunat_moneda_desde_texto("DOLARES AMERICANOS") == OCRX_MON_USD, "moneda texto");
}

static void t_fechas(void) {
    char f[11];
    OK(sunat_fecha_parsear("15/10/2026", f) && !strcmp(f, "2026-10-15"), "dmy %s", f);
    OK(sunat_fecha_parsear("15-10-26", f) && !strcmp(f, "2026-10-15"), "dmy2 %s", f);
    OK(sunat_fecha_parsear("2026-10-15", f) && !strcmp(f, "2026-10-15"), "iso %s", f);
    OK(sunat_fecha_parsear("15 de octubre de 2026", f) && !strcmp(f, "2026-10-15"), "letras %s", f);
    OK(sunat_fecha_parsear("15/OCT/2026", f) && !strcmp(f, "2026-10-15"), "mes abrev %s", f);
    OK(sunat_fecha_parsear("15.10.2026", f) && !strcmp(f, "2026-10-15"), "puntos %s", f);
    OK(sunat_fecha_parsear("Fecha de Emisión: 01/02/2026 10:30", f) && !strcmp(f, "2026-02-01"), "con etiqueta %s", f);
    OK(sunat_fecha_parsear("1S/10/2026", f) && !strcmp(f, "2026-10-15"), "ocr S->5 %s", f);
    OK(!sunat_fecha_parsear("32/10/2026", f), "dia invalido");
    OK(!sunat_fecha_parsear("118.00", f), "monto no es fecha");
    OK(sunat_fecha_en_linea("FECHA EMISION: 15/10/2026 HORA 10:30:45", f, NULL) && !strcmp(f, "2026-10-15"), "en linea");
    char h[9]; OK(sunat_hora_parsear("HORA 10:30:45", h) && !strcmp(h, "10:30:45"), "hora %s", h);
    OK(sunat_hora_parsear("03:15 PM", h) && !strcmp(h, "15:15:00"), "hora pm %s", h);
}

static void t_letras(void) {
    double v; ocrx_moneda m;
    OK(sunat_letras_a_monto("SON: CIENTO DIECIOCHO CON 00/100 SOLES", &v, &m), "ok"); EQD(v, 118.0); OK(m == OCRX_MON_PEN, "PEN");
    OK(sunat_letras_a_monto("SON: MIL DOSCIENTOS TREINTA Y CUATRO CON 56/100 SOLES", &v, &m)); EQD(v, 1234.56);
    OK(sunat_letras_a_monto("IMPORTE EN LETRAS: DOS MIL QUINIENTOS CON 50/100 DOLARES AMERICANOS", &v, &m)); EQD(v, 2500.50); OK(m == OCRX_MON_USD, "USD");
    OK(sunat_letras_a_monto("SON: UN MILLON TRESCIENTOS MIL CON 00/100 SOLES", &v, &m)); EQD(v, 1300000);
    OK(sunat_letras_a_monto("SON: VEINTIUN CON 10/100 SOLES", &v, &m)); EQD(v, 21.10);
    OK(sunat_letras_a_monto("SON: CIENTO VEINTE Y OCHO CON 50/100 NUEVOS SOLES", &v, &m)); EQD(v, 128.50);
    OK(sunat_letras_a_monto("SON: QUINIENTOS CON CINCUENTA CENTIMOS SOLES", &v, &m)); EQD(v, 500.50);
    OK(sunat_letras_a_monto("SON CIENTO DIECIOCHO Y 00/100 SOLES", &v, &m)); EQD(v, 118.0);
    OK(sunat_letras_a_monto("SON: CIENT0 DIECI0CHO CON 00/100 SOLES", &v, &m)); EQD(v, 118.0); /* OCR 0 por O con tolerancia */
    OK(sunat_linea_es_monto_en_letras("SON: CIENTO DIECIOCHO CON 00/100 SOLES"), "detecta");
    OK(!sunat_linea_es_monto_en_letras("TOTAL 118.00"), "no detecta");
}

static void t_totales(void) {
    ocrx_totales t; memset(&t, 0, sizeof t);
    t.gravada = 100; t.igv = 18; t.total = 118;
    OK(sunat_totales_reparar(&t, 0, false), "cuadra simple"); EQD(t.tasa_igv, 0.18);
    memset(&t, 0, sizeof t); t.gravada = 100; t.igv = 18; t.total = 110; /* 8 leido como 0 */
    OK(sunat_totales_reparar(&t, 0, false), "repara total"); EQD(t.total, 118); OK(t.m_total.corregido, "marcado");
    memset(&t, 0, sizeof t); t.gravada = 100; t.total = 118;
    OK(sunat_totales_reparar(&t, 0, false), "completa igv"); EQD(t.igv, 18);
    memset(&t, 0, sizeof t); t.total = 118;
    OK(sunat_totales_reparar(&t, 0, false), "solo total"); EQD(t.gravada, 100); EQD(t.igv, 18); OK(t.m_gravada.dudoso, "dudoso");
    memset(&t, 0, sizeof t); t.gravada = 100; t.igv = 18; t.total = 110;
    OK(sunat_totales_reparar(&t, 118.0, true), "letras mandan"); EQD(t.total, 118); OK(t.m_total.origen == OCRX_ORIGEN_LETRAS, "origen letras");
    memset(&t, 0, sizeof t); t.gravada = 200; t.igv = 21; t.total = 221;
    OK(sunat_totales_reparar(&t, 0, false), "tasa 10.5"); EQD(t.tasa_igv, 0.105);
    memset(&t, 0, sizeof t); t.exonerada = 50; t.total = 50;
    OK(sunat_totales_reparar(&t, 0, false), "solo exonerado");
    memset(&t, 0, sizeof t); t.gravada = 100; t.igv = 18; t.icbper = 0.5; t.total = 118.5;
    OK(sunat_totales_reparar(&t, 0, false), "icbper");
    memset(&t, 0, sizeof t); t.gravada = 100; t.igv = 18; t.descuentos = 10; t.total = 108;
    OK(sunat_totales_reparar(&t, 0, false), "descuento");
    memset(&t, 0, sizeof t); t.gravada = 118; t.igv = 18; t.total = 118; /* subtotal mal leido = total */
    OK(sunat_totales_reparar(&t, 0, false), "subtotal era total"); EQD(t.gravada, 100);
}

static void t_qr(void) {
    sunat_qr q = sunat_qr_parsear("20100070970|01|F001|00000123|18.00|118.00|15/10/2026|6|20131312955|abc123hash|");
    OK(q.ok, "ok"); EQS(q.ruc, "20100070970"); OK(q.tipo == OCRX_CPE_FACTURA, "tipo"); EQS(q.serie, "F001"); EQS(q.numero, "123");
    EQD(q.igv, 18); EQD(q.total, 118); EQS(q.fecha, "2026-10-15"); OK(q.tipo_doc_cliente == OCRX_DOC_RUC, "tdoc"); EQS(q.num_doc_cliente, "20131312955"); EQS(q.hash, "abc123hash");
    q = sunat_qr_parsear("20100070970|03|B001|45|0.00|25.50|2026-10-15|1|46789681");
    OK(q.ok, "boleta"); OK(q.tipo == OCRX_CPE_BOLETA, "tipo B"); EQS(q.fecha, "2026-10-15"); OK(q.tipo_doc_cliente == OCRX_DOC_DNI, "dni");
    q = sunat_qr_parsear("20100070970|01|F001|123|18,00|118,00|15/10/2026|-|");
    OK(q.ok, "comas"); EQD(q.total, 118);
    q = sunat_qr_parsear("https://facturador.com/consulta?ruc=20100070970&tipo=01&serie=F001&numero=123&total=118.00&fecha=15/10/2026");
    OK(q.ok, "url"); EQS(q.serie, "F001"); EQD(q.total, 118);
    OK(!sunat_qr_parece("000201010211..."), "yape no parece");
    OK(sunat_qr_parece("20100070970|01|F001|123|18|118|2026-01-01|6|20131312955"), "parece");
    q = sunat_qr_parsear("RUC:20100070970|01|F001|123|18.00|118.00|15/10/2026|6|20131312955"); OK(q.ok, "prefijo RUC:"); EQS(q.ruc, "20100070970");
}

static void t_str(void) {
    char b[128];
    str_normalizar("  Fecha de  Emisión:  Ñandú ", b, sizeof b, false); EQS(b, "FECHA DE EMISION: ÑANDU");
    str_normalizar("Señores", b, sizeof b, true); EQS(b, "SENORES");
    OK(str_similitud("CORPORACION ABC S.A.C.", "C0RPORAC1ON ABC SAC") > 0.8, "similitud");
    ocr_forzar_digitos("2O1OOO7O97O", b, sizeof b); EQS(b, "20100070970");
}

int main(void) {
    t_str(); t_ruc(); t_serie(); t_montos(); t_fechas(); t_letras(); t_totales(); t_qr();
    printf("%d pruebas, %d fallos\n", total, fallos);
    return fallos ? 1 : 0;
}

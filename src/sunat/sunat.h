/* Reglas de negocio SUNAT: validación y corrección de datos leídos. */
#ifndef OCRX_SUNAT_H
#define OCRX_SUNAT_H
#include <stdbool.h>
#include <stddef.h>
#include "../../include/ocrx.h"

/* ---- RUC / documentos de identidad (ruc.c) ---- */
bool sunat_ruc_valido(const char *ruc11);                 /* 11 dígitos + dígito verificador módulo 11 */
int  sunat_ruc_digito_verificador(const char *ruc10);     /* -1 si inválido */
/* Intenta reconstruir un RUC a partir de texto OCR con confusiones (O->0, etc.). Devuelve true si logra uno válido. */
bool sunat_ruc_corregir(const char *texto, char out[12], bool *corregido);
bool sunat_dni_valido(const char *dni);                   /* 8 dígitos */
/* Adivina el tipo de documento de identidad por su forma */
ocrx_tipo_doc_id sunat_tipo_doc_por_forma(const char *num);
const char *sunat_tipo_doc_codigo(ocrx_tipo_doc_id t);    /* "1", "6", "A"... */
ocrx_tipo_doc_id sunat_tipo_doc_desde_codigo(const char *c);
/* Tipo de contribuyente a partir de los 2 primeros dígitos: 10 natural, 15/17 natural sin DNI, 20 jurídica */
const char *sunat_ruc_tipo_contribuyente(const char *ruc11);

/* ---- Tipo de comprobante y serie (serie.c) ---- */
ocrx_tipo_cpe sunat_tipo_desde_codigo(const char *cod2);  /* "01" -> FACTURA */
const char *sunat_tipo_codigo(ocrx_tipo_cpe t);           /* FACTURA -> "01" */
const char *sunat_tipo_nombre(ocrx_tipo_cpe t);           /* "FACTURA ELECTRONICA" */
/* Detecta el tipo por el texto del título del documento ("FACTURA ELECTRÓNICA", "BOLETA DE VENTA", "NOTA DE CRÉDITO"...) */
ocrx_tipo_cpe sunat_tipo_desde_titulo(const char *texto_normalizado);
/* Deduce tipo a partir de la serie: F->01, B->03, (F|B)+NC->07..., E001 depende del contexto */
ocrx_tipo_cpe sunat_tipo_desde_serie(const char *serie, ocrx_tipo_cpe pista);
/* Valida y normaliza "F001-00000123", "F001 - 123", "FOO1-123" -> serie "F001", numero "123". Corrige confusiones OCR. */
bool sunat_serie_numero_parsear(const char *texto, ocrx_tipo_cpe pista, char serie[8], char numero[12], bool *corregido);
bool sunat_serie_valida(const char *serie, ocrx_tipo_cpe tipo);
bool sunat_serie_es_electronica(const char *serie);       /* F/B/E/T + 3 alfanum., EB01, etc. */

/* ---- Montos (montos.c) ---- */
typedef struct sunat_monto {
    double valor;
    ocrx_moneda moneda;      /* si el texto traía símbolo */
    bool   negativo;
    bool   ok;
    int    decimales;        /* decimales detectados */
} sunat_monto;
/* "S/ 1,234.56" "S/.1.234,56" "US$ 12.00" "-5.00" "(5.00)" "1 234,56" "118.0O" (OCR) */
sunat_monto sunat_monto_parsear(const char *texto);
/* Busca en una línea el último monto (lo habitual: etiqueta a la izquierda, monto a la derecha) */
bool sunat_monto_ultimo_en_linea(const char *linea, sunat_monto *out, const char **inicio);
/* Busca todos los montos de una línea; devuelve cantidad */
size_t sunat_montos_en_linea(const char *linea, sunat_monto *out, size_t cap);
ocrx_moneda sunat_moneda_desde_texto(const char *texto);  /* "SOLES", "PEN", "S/", "DOLARES", "USD", "US$" */
const char *sunat_moneda_codigo(ocrx_moneda m);
bool sunat_monto_igual(double a, double b);               /* tolerancia 0.011 */

/* ---- Fechas (fechas.c) ---- */
/* dd/mm/aaaa, dd-mm-aa, aaaa-mm-dd, "15 de octubre de 2026", "15/OCT/2026", "15.10.26" -> AAAA-MM-DD */
bool sunat_fecha_parsear(const char *texto, char out[11]);
/* Busca una fecha en una línea; devuelve true y la posición */
bool sunat_fecha_en_linea(const char *linea, char out[11], const char **inicio);
bool sunat_fecha_razonable(const char *iso);              /* entre 2010 y hoy+1 día */
bool sunat_hora_parsear(const char *texto, char out[9]);

/* ---- Monto en letras (letras.c) ---- */
/* "SON: CIENTO VEINTE Y OCHO CON 50/100 SOLES" -> 128.50, moneda */
bool sunat_letras_a_monto(const char *texto, double *valor, ocrx_moneda *moneda);
bool sunat_linea_es_monto_en_letras(const char *linea);

/* ---- Cuentas cruzadas (totales.c) ---- */
typedef struct sunat_cuadre {
    bool   cuadra;
    bool   igv_coherente;        /* igv ≈ gravada * tasa */
    double tasa_detectada;       /* 0.18, 0.105, 0.10 o 0 */
    double diferencia;           /* total_calculado - total_leido */
    char   detalle[256];
} sunat_cuadre;
sunat_cuadre sunat_cuadrar(ocrx_totales *t);
/* Intenta completar campos faltantes y corregir confusiones para que cuadre. Devuelve true si tras el proceso cuadra. */
bool sunat_totales_reparar(ocrx_totales *t, double monto_letras, bool tiene_letras);
/* Genera variantes de un monto por confusiones OCR (118.00 -> 110.00, 116.00, 1180.00 ...) */
size_t sunat_monto_variantes(double v, double *out, size_t cap);

/* ---- Catálogos varios ---- */
const char *sunat_afectacion_nombre(const char *cod); /* "10" -> "GRAVADO - OPERACION ONEROSA" */
const char *sunat_unidad_normalizar(const char *u);   /* "UND"->"NIU", "SERV"->"ZZ", "KG"->"KGM" */

#endif

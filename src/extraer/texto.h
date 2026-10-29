/* Extracción de un comprobante a partir de líneas de texto (capa de texto del PDF u OCR).
 * Cada línea puede traer coordenadas (en puntos o píxeles, el mismo sistema para todo el documento). */
#ifndef OCRX_EXTRAER_TEXTO_H
#define OCRX_EXTRAER_TEXTO_H
#include <stddef.h>
#include <stdbool.h>
#include "../../include/ocrx.h"
#include "../core/arena.h"

typedef struct doc_linea {
    char *texto;          /* UTF-8 original */
    char *norm;           /* mayúsculas sin tildes (se calcula) */
    float x0, y0, x1, y1; /* 0 si no hay coordenadas */
    int   pagina;
    float confianza;      /* 1.0 para texto de PDF; OCR trae la suya */
} doc_linea;

typedef struct doc_texto {
    doc_linea *lineas;
    size_t n, cap;
    bool con_coordenadas;
    float ancho_pagina, alto_pagina;
} doc_texto;

doc_texto *doc_texto_nuevo(arena *a);
void doc_texto_agregar(doc_texto *d, arena *a, const char *texto, float x0, float y0, float x1, float y1, int pagina, float conf);
/* Divide texto plano por saltos de línea */
doc_texto *doc_texto_desde_plano(const char *texto, arena *a);
/* Si hay coordenadas: ordena por página, fila (y) y columna (x), y une fragmentos de la misma fila en una línea lógica */
void doc_texto_ordenar(doc_texto *d, arena *a);
/* Texto plano completo (una línea por elemento) */
char *doc_texto_plano(const doc_texto *d, arena *a);

/* Diagnóstico: cuando es distinto de 0, el extractor explica por stderr algunas decisiones */
extern int extraer_depurar;
/* Rellena el comprobante con lo que encuentre. No pisa campos que ya vengan con origen XML/QR. */
void extraer_desde_texto(const doc_texto *d, ocrx_comprobante *c, arena *a, ocrx_origen origen);

#endif

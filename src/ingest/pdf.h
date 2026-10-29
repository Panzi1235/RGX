/* PDF: capa de texto con coordenadas y render a imagen (PDFium). */
#ifndef OCRX_PDF_H
#define OCRX_PDF_H
#include <stddef.h>
#include <stdbool.h>
#include "../../include/ocrx.h"
#include "../core/arena.h"
#include "../extraer/texto.h"

typedef struct pdf_imagen {   /* bitmap BGRA de una página renderizada */
    unsigned char *pix; int ancho, alto, stride; int pagina;
} pdf_imagen;

bool pdf_disponible(void);
void pdf_init_global(void);
void pdf_fin_global(void);
/* Extrae el texto de todas las páginas (máx. max_paginas; 0 = todas). Devuelve nº de páginas del documento o -1. */
int  pdf_texto(const void *datos, size_t n, doc_texto *d, arena *a, int max_paginas, bool *texto_util);
/* Renderiza una página (base 1) a dpi dados. pix en arena. */
bool pdf_render(const void *datos, size_t n, int pagina, int dpi, pdf_imagen *out, arena *a);
/* ¿El texto extraído es confiable? (no es basura por fuentes sin ToUnicode) */
bool pdf_texto_es_util(const doc_texto *d);

#endif

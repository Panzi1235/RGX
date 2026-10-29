/* Extrae el primer .xml de un ZIP (SUNAT entrega los CPE firmados comprimidos). Requiere zlib para entradas deflate. */
#ifndef OCRX_ZIPXML_H
#define OCRX_ZIPXML_H
#include <stddef.h>
#include "../core/arena.h"
/* Devuelve puntero al XML (en arena) y su tamaño, o NULL. Prefiere nombres que empiecen con un RUC (p.ej. 20123456789-01-F001-123.xml) y evita "R-" (CDR). */
char *zipxml_extraer(const void *zip, size_t n, size_t *out_n, arena *a, char nombre_out[256]);
#endif

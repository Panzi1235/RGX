/* Lector de XML UBL 2.1 SUNAT (Invoice, CreditNote, DebitNote, DespatchAdvice, Retention, Perception). */
#ifndef OCRX_UBL_H
#define OCRX_UBL_H
#include <stddef.h>
#include <stdbool.h>
#include "../../include/ocrx.h"
#include "../core/arena.h"

/* Devuelve OCRX_OK si logró leer un comprobante. Los ítems se reservan en la arena. */
ocrx_error ubl_leer(const char *xml, size_t n, ocrx_comprobante *c, arena *a);
/* ¿Parece XML UBL? (para la detección de formato) */
bool ubl_parece(const void *datos, size_t n);

#endif

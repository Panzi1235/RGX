/* Lectura de códigos QR en imágenes (archivo JPG/PNG/TIFF o bitmap de página PDF). */
#ifndef OCRX_QR_IMAGEN_H
#define OCRX_QR_IMAGEN_H
#include <stddef.h>
#include <stdbool.h>
#include "../core/arena.h"

bool qr_disponible(void);
/* Busca QRs en una imagen en memoria (JPG, PNG, TIFF, BMP). Devuelve cuántos textos encontró (máx cap). */
size_t qr_leer_archivo(const void *datos, size_t n, char **textos, size_t cap, arena *a);
/* Igual, sobre un bitmap gris 8 bits */
size_t qr_leer_gris(const unsigned char *gris, int ancho, int alto, int stride, char **textos, size_t cap, arena *a);
/* Convierte BGRA (PDFium) a gris */
unsigned char *bgra_a_gris(const unsigned char *bgra, int ancho, int alto, int stride, arena *a);

#endif

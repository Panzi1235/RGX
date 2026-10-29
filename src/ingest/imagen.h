/* Carga de imágenes a gris 8 bits (JPEG, PNG, TIFF, BMP) sin Leptonica: libjpeg-turbo, libpng, libtiff y BMP propio.
 * Respeta la orientación EXIF de las fotos de celular (JPEG). */
#ifndef OCRX_IMAGEN_H
#define OCRX_IMAGEN_H
#include <stddef.h>
#include <stdbool.h>
#include "../core/arena.h"

typedef struct imagen_gris {
    unsigned char *pix;   /* ancho*alto, una fila tras otra */
    int ancho, alto;
    int orientacion_exif; /* 1..8; 0 si no había */
    int dpi;              /* 0 si no se conoce */
} imagen_gris;

bool imagen_formato_soportado(const void *datos, size_t n);
/* Devuelve false si el formato no está compilado o el archivo está dañado. Ya viene rotada según EXIF. */
bool imagen_cargar_gris(const void *datos, size_t n, imagen_gris *out, arena *a, char *error, size_t error_cap);
/* Rota/espeja según orientación EXIF (1..8) */
void imagen_aplicar_orientacion(imagen_gris *img, int orientacion, arena *a);

#endif

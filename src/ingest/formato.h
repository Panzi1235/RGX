/* Detección del formato de un archivo por su contenido (nunca por la extensión sola). */
#ifndef OCRX_FORMATO_H
#define OCRX_FORMATO_H
#include <stddef.h>

typedef enum formato { FMT_DESCONOCIDO = 0, FMT_PDF, FMT_XML, FMT_ZIP, FMT_JPEG, FMT_PNG, FMT_TIFF, FMT_BMP, FMT_GIF, FMT_WEBP, FMT_HEIC, FMT_TEXTO, FMT_JSON } formato;

formato formato_detectar(const void *datos, size_t n, const char *nombre_pista);
const char *formato_nombre(formato f);
int formato_es_imagen(formato f);

#endif

/* OCR de texto con Tesseract (API en C). Devuelve líneas con coordenadas y confianza. */
#ifndef OCRX_TESS_H
#define OCRX_TESS_H
#include <stdbool.h>
#include "../core/arena.h"
#include "../extraer/texto.h"
#include "../ingest/imagen.h"

typedef struct tess_motor tess_motor;

bool        tess_disponible(void);
/* dir_tessdata: carpeta con spa.traineddata (p.ej. models\tessdata). idiomas: "spa" o "spa+eng". NULL si falla. */
tess_motor *tess_crear(const char *dir_tessdata, const char *idiomas, char *error, size_t error_cap);
void        tess_destruir(tess_motor *t);
/* Reconoce una imagen en gris. psm: 4 columna única (facturas), 6 bloque uniforme (tickets), 11 texto disperso.
 * Agrega líneas a d con coordenadas en píxeles. Devuelve confianza media 0..1 (o -1 si falló). */
float       tess_reconocer(tess_motor *t, const imagen_gris *img, int psm, int dpi, doc_texto *d, arena *a);

/* Preparación básica de la imagen antes del OCR: escala a ~300 dpi equivalentes, estira contraste, recorta bordes negros. */
void        tess_preparar(imagen_gris *img, arena *a, int *dpi_estimado);

#endif

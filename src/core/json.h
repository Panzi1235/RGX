/* Escritor JSON mínimo sobre buffer creciente (sólo salida). */
#ifndef OCRX_JSON_H
#define OCRX_JSON_H
#include <stddef.h>
#include <stdbool.h>
#include "arena.h"

typedef struct jw {
    arena *a;
    char  *buf; size_t len, cap;
    bool   bonito;
    int    nivel;
    bool   primero[64];   /* pila: ¿es el primer elemento del contenedor? */
    int    sp;
} jw;

void jw_init(jw *w, arena *a, bool bonito);
void jw_obj_abrir(jw *w, const char *clave);   /* clave NULL si va en array */
void jw_obj_cerrar(jw *w);
void jw_arr_abrir(jw *w, const char *clave);
void jw_arr_cerrar(jw *w);
void jw_str(jw *w, const char *clave, const char *valor);     /* NULL -> null */
void jw_str_o_omitir(jw *w, const char *clave, const char *valor); /* omite si vacío */
void jw_num(jw *w, const char *clave, double v);               /* 2 decimales si no es entero */
void jw_num_o_omitir(jw *w, const char *clave, double v);     /* omite si 0 */
void jw_int(jw *w, const char *clave, long long v);
void jw_bool(jw *w, const char *clave, bool v);
void jw_null(jw *w, const char *clave);
const char *jw_fin(jw *w);   /* termina en NUL */

#endif

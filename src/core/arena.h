/* Arena: toda la memoria de un documento se reserva aquí y se libera de una vez. */
#ifndef OCRX_ARENA_H
#define OCRX_ARENA_H
#include <stddef.h>

typedef struct arena_bloque arena_bloque;
typedef struct arena {
    arena_bloque *actual;
    size_t tam_bloque;
    size_t total;
} arena;

void  arena_init(arena *a, size_t tam_bloque);
void *arena_alloc(arena *a, size_t n);          /* alineado a 16, cero-inicializado */
void *arena_realloc(arena *a, void *p, size_t viejo, size_t nuevo);
char *arena_strdup(arena *a, const char *s);
char *arena_strndup(arena *a, const char *s, size_t n);
char *arena_printf(arena *a, const char *fmt, ...);
void  arena_liberar(arena *a);

#endif

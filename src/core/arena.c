#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

struct arena_bloque {
    arena_bloque *anterior;
    size_t usado, cap;
    unsigned char datos[];
};

static arena_bloque *bloque_nuevo(size_t cap) {
    arena_bloque *b = (arena_bloque *)malloc(sizeof(arena_bloque) + cap);
    if (!b) { fputs("ocrx: sin memoria\n", stderr); abort(); }
    b->anterior = NULL; b->usado = 0; b->cap = cap;
    return b;
}

void arena_init(arena *a, size_t tam_bloque) {
    a->tam_bloque = tam_bloque ? tam_bloque : (256 * 1024);
    a->actual = NULL; a->total = 0;
}

void *arena_alloc(arena *a, size_t n) {
    if (n == 0) n = 1;
    size_t alineado = (n + 15) & ~(size_t)15;
    arena_bloque *b = a->actual;
    if (!b || b->usado + alineado > b->cap) {
        size_t cap = alineado > a->tam_bloque ? alineado : a->tam_bloque;
        arena_bloque *nb = bloque_nuevo(cap);
        nb->anterior = b; a->actual = nb; b = nb;
    }
    void *p = b->datos + b->usado;
    b->usado += alineado; a->total += alineado;
    memset(p, 0, alineado);
    return p;
}

void *arena_realloc(arena *a, void *p, size_t viejo, size_t nuevo) {
    if (nuevo <= viejo) return p;
    void *q = arena_alloc(a, nuevo);
    if (p && viejo) memcpy(q, p, viejo);
    return q;
}

char *arena_strndup(arena *a, const char *s, size_t n) {
    char *d = (char *)arena_alloc(a, n + 1);
    if (s) memcpy(d, s, n);
    d[n] = 0;
    return d;
}
char *arena_strdup(arena *a, const char *s) { return arena_strndup(a, s ? s : "", s ? strlen(s) : 0); }

char *arena_printf(arena *a, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return arena_strdup(a, ""); }
    char *d = (char *)arena_alloc(a, (size_t)n + 1);
    vsnprintf(d, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return d;
}

void arena_liberar(arena *a) {
    arena_bloque *b = a->actual;
    while (b) { arena_bloque *ant = b->anterior; free(b); b = ant; }
    a->actual = NULL; a->total = 0;
}

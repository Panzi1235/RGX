#include "json.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static void jw_reservar(jw *w, size_t extra) {
    if (w->len + extra + 1 <= w->cap) return;
    size_t nc = w->cap ? w->cap * 2 : 4096;
    while (nc < w->len + extra + 1) nc *= 2;
    w->buf = (char *)arena_realloc(w->a, w->buf, w->len, nc);
    w->cap = nc;
}
static void jw_put(jw *w, const char *s, size_t n) { jw_reservar(w, n); memcpy(w->buf + w->len, s, n); w->len += n; }
static void jw_puts(jw *w, const char *s) { jw_put(w, s, strlen(s)); }
static void jw_nl(jw *w) {
    if (!w->bonito) return;
    jw_put(w, "\n", 1);
    for (int i = 0; i < w->nivel; i++) jw_put(w, "  ", 2);
}
static void jw_escapar(jw *w, const char *s) {
    jw_put(w, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': jw_puts(w, "\\\""); break;
        case '\\': jw_puts(w, "\\\\"); break;
        case '\n': jw_puts(w, "\\n"); break;
        case '\r': jw_puts(w, "\\r"); break;
        case '\t': jw_puts(w, "\\t"); break;
        default:
            if (*p < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", *p); jw_puts(w, b); }
            else jw_put(w, (const char *)p, 1);
        }
    }
    jw_put(w, "\"", 1);
}
static void jw_pre(jw *w, const char *clave) {
    if (w->sp > 0) {
        if (!w->primero[w->sp - 1]) jw_put(w, ",", 1);
        w->primero[w->sp - 1] = false;
        jw_nl(w);
    }
    if (clave) { jw_escapar(w, clave); jw_puts(w, w->bonito ? ": " : ":"); }
}
static void jw_push(jw *w) { if (w->sp < 64) w->primero[w->sp++] = true; w->nivel++; }
static void jw_pop(jw *w) { bool vacio = w->sp > 0 && w->primero[w->sp - 1]; if (w->sp > 0) w->sp--; w->nivel--; if (!vacio) jw_nl(w); }

void jw_init(jw *w, arena *a, bool bonito) { memset(w, 0, sizeof *w); w->a = a; w->bonito = bonito; }
void jw_obj_abrir(jw *w, const char *clave) { jw_pre(w, clave); jw_put(w, "{", 1); jw_push(w); }
void jw_obj_cerrar(jw *w) { jw_pop(w); jw_put(w, "}", 1); }
void jw_arr_abrir(jw *w, const char *clave) { jw_pre(w, clave); jw_put(w, "[", 1); jw_push(w); }
void jw_arr_cerrar(jw *w) { jw_pop(w); jw_put(w, "]", 1); }
void jw_str(jw *w, const char *clave, const char *valor) { jw_pre(w, clave); if (valor) jw_escapar(w, valor); else jw_puts(w, "null"); }
void jw_str_o_omitir(jw *w, const char *clave, const char *valor) { if (valor && *valor) jw_str(w, clave, valor); }
void jw_num(jw *w, const char *clave, double v) {
    char b[64];
    if (isnan(v) || isinf(v)) { jw_null(w, clave); return; }
    if (fabs(v - floor(v)) < 1e-9 && fabs(v) < 1e15) snprintf(b, sizeof b, "%.0f", v);
    else snprintf(b, sizeof b, "%.2f", v);
    jw_pre(w, clave); jw_puts(w, b);
}
void jw_num_o_omitir(jw *w, const char *clave, double v) { if (fabs(v) > 1e-9) jw_num(w, clave, v); }
void jw_int(jw *w, const char *clave, long long v) { char b[32]; snprintf(b, sizeof b, "%lld", v); jw_pre(w, clave); jw_puts(w, b); }
void jw_bool(jw *w, const char *clave, bool v) { jw_pre(w, clave); jw_puts(w, v ? "true" : "false"); }
void jw_null(jw *w, const char *clave) { jw_pre(w, clave); jw_puts(w, "null"); }
const char *jw_fin(jw *w) { jw_reservar(w, 1); w->buf[w->len] = 0; return w->buf; }

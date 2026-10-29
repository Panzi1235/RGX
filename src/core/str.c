#include "str.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

uint32_t utf8_decodificar(const char **pp) {
    const unsigned char *p = (const unsigned char *)*pp;
    if (!*p) return 0;
    uint32_t cp; int extra;
    if (*p < 0x80) { cp = *p; extra = 0; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
    else { (*pp)++; return 0xFFFD; }
    p++;
    for (int i = 0; i < extra; i++, p++) {
        if ((*p & 0xC0) != 0x80) { *pp = (const char *)p; return 0xFFFD; }
        cp = (cp << 6) | (*p & 0x3F);
    }
    *pp = (const char *)p;
    return cp;
}

size_t utf8_codificar(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

size_t utf8_largo(const char *s) { size_t n = 0; while (*s) { utf8_decodificar(&s); n++; } return n; }

uint32_t cp_sin_tilde(uint32_t cp) {
    switch (cp) {
    case 0xE1: case 0xE0: case 0xE2: case 0xE4: case 0xE3: case 0xE5: return 'a';
    case 0xC1: case 0xC0: case 0xC2: case 0xC4: case 0xC3: case 0xC5: return 'A';
    case 0xE9: case 0xE8: case 0xEA: case 0xEB: return 'e';
    case 0xC9: case 0xC8: case 0xCA: case 0xCB: return 'E';
    case 0xED: case 0xEC: case 0xEE: case 0xEF: return 'i';
    case 0xCD: case 0xCC: case 0xCE: case 0xCF: return 'I';
    case 0xF3: case 0xF2: case 0xF4: case 0xF6: case 0xF5: return 'o';
    case 0xD3: case 0xD2: case 0xD4: case 0xD6: case 0xD5: return 'O';
    case 0xFA: case 0xF9: case 0xFB: case 0xFC: return 'u';
    case 0xDA: case 0xD9: case 0xDB: case 0xDC: return 'U';
    case 0xE7: return 'c'; case 0xC7: return 'C';
    case 0x2019: case 0x2018: case 0x60: case 0xB4: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0xA0: case 0x2007: case 0x202F: return ' ';
    case 0xBA: case 0xB0: return 0xB0; /* º -> ° */
    default: return cp;
    }
}

uint32_t cp_mayuscula(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32; /* latin-1 */
    return cp;
}

void str_normalizar(const char *s, char *out, size_t cap, bool quitar_enie) {
    size_t o = 0; bool ult_esp = true;
    if (!cap) return;
    while (*s) {
        uint32_t cp = utf8_decodificar(&s);
        if (cp == 0xFFFD) continue;
        cp = cp_mayuscula(cp_sin_tilde(cp));
        if (cp == 0xD1 || cp == 0xF1) { if (quitar_enie) cp = 'N'; else cp = 0xD1; }
        if (cp < 0x20 || cp == 0x7F) cp = ' ';
        if (cp == '\t' || cp == '\r' || cp == '\n') cp = ' ';
        if (cp == ' ') { if (ult_esp) continue; ult_esp = true; }
        else ult_esp = false;
        char buf[4]; size_t n = utf8_codificar(cp, buf);
        if (o + n + 1 > cap) break;
        memcpy(out + o, buf, n); o += n;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

void str_recortar(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n && (unsigned char)s[n - 1] <= ' ') s[--n] = 0;
    size_t i = 0; while (s[i] && (unsigned char)s[i] <= ' ') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

void str_colapsar_espacios(char *s) {
    char *w = s; bool esp = false;
    for (char *r = s; *r; r++) {
        unsigned char c = (unsigned char)*r;
        if (c <= ' ') { esp = true; continue; }
        if (esp && w != s) *w++ = ' ';
        esp = false; *w++ = *r;
    }
    *w = 0;
}

void str_mayus_ascii(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; }

bool str_empieza(const char *s, const char *pref) {
    for (; *pref; s++, pref++) if (!*s || toupper((unsigned char)*s) != toupper((unsigned char)*pref)) return false;
    return true;
}
const char *str_buscar(const char *s, const char *sub) {
    if (!s || !sub || !*sub) return s;
    for (; *s; s++) if (str_empieza(s, sub)) return s;
    return NULL;
}
bool str_contiene(const char *s, const char *sub) { return str_buscar(s, sub) != NULL; }

void str_copiar(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        /* no cortar un carácter UTF-8 por la mitad */
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n); dst[n] = 0;
}

void str_solo_digitos(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) if (*s >= '0' && *s <= '9') out[o++] = *s;
    out[o] = 0;
}
size_t str_contar_digitos(const char *s) { size_t n = 0; for (; *s; s++) if (*s >= '0' && *s <= '9') n++; return n; }
bool str_es_digitos(const char *s) { if (!*s) return false; for (; *s; s++) if (*s < '0' || *s > '9') return false; return true; }

char ocr_a_digito(char c) {
    switch (c) {
    case 'O': case 'o': case 'Q': case 'D': return '0';
    case 'I': case 'l': case 'i': case '|': case '!': case 'L': return '1';
    case 'Z': case 'z': return '2';
    case 'E': return '3';
    case 'A': case 'h': return '4';
    case 'S': case 's': return '5';
    case 'G': case 'b': return '6';
    case 'T': case 't': return '7';
    case 'B': case '&': return '8';
    case 'g': case 'q': return '9';
    default: return 0;
    }
}
char ocr_a_letra(char c) {
    switch (c) {
    case '0': return 'O'; case '1': return 'I'; case '2': return 'Z'; case '5': return 'S';
    case '6': return 'G'; case '8': return 'B'; case '4': return 'A'; case '7': return 'T';
    default: return 0;
    }
}
void ocr_forzar_digitos(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) {
        if (*s >= '0' && *s <= '9') out[o++] = *s;
        else { char d = ocr_a_digito(*s); if (d) out[o++] = d; }
    }
    out[o] = 0;
}

int str_levenshtein(const char *a, const char *b, int max) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (abs(la - lb) > max) return max + 1;
    if (la == 0) return lb;
    if (lb == 0) return la;
    int *prev = (int *)malloc(sizeof(int) * (size_t)(lb + 1));
    int *cur  = (int *)malloc(sizeof(int) * (size_t)(lb + 1));
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i; int fila_min = cur[0];
        for (int j = 1; j <= lb; j++) {
            int costo = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int v = prev[j] + 1;
            if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1;
            if (prev[j - 1] + costo < v) v = prev[j - 1] + costo;
            cur[j] = v; if (v < fila_min) fila_min = v;
        }
        if (fila_min > max) { free(prev); free(cur); return max + 1; }
        int *t = prev; prev = cur; cur = t;
    }
    int r = prev[lb]; free(prev); free(cur); return r;
}

double str_similitud(const char *a, const char *b) {
    char na[512], nb[512];
    str_normalizar(a, na, sizeof na, true);
    str_normalizar(b, nb, sizeof nb, true);
    /* comparar solo alfanuméricos: la puntuación (S.A.C. vs SAC) no debe pesar */
    { char *w = na; for (char *r = na; *r; r++) if (isalnum((unsigned char)*r) || (unsigned char)*r >= 0x80) *w++ = *r; *w = 0; }
    { char *w = nb; for (char *r = nb; *r; r++) if (isalnum((unsigned char)*r) || (unsigned char)*r >= 0x80) *w++ = *r; *w = 0; }
    size_t la = strlen(na), lb = strlen(nb);
    size_t m = la > lb ? la : lb;
    if (m == 0) return 1.0;
    int d = str_levenshtein(na, nb, (int)m);
    return 1.0 - (double)d / (double)m;
}

/* Utilidades de texto para texto OCR y documentos en español (UTF-8). */
#ifndef OCRX_STR_H
#define OCRX_STR_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* UTF-8 */
uint32_t utf8_decodificar(const char **p);           /* avanza p; 0xFFFD si inválido */
size_t   utf8_codificar(uint32_t cp, char out[4]);
size_t   utf8_largo(const char *s);                  /* nº de code points */

/* Normalización */
uint32_t cp_sin_tilde(uint32_t cp);                  /* á->a, Ñ se mantiene */
uint32_t cp_mayuscula(uint32_t cp);                  /* ASCII + latin1 básico */
/* Copia s en out: mayúsculas, sin tildes (Ñ->N opcional), espacios colapsados, sin control chars */
void str_normalizar(const char *s, char *out, size_t cap, bool quitar_enie);
void str_recortar(char *s);                           /* trim in place */
void str_colapsar_espacios(char *s);
void str_mayus_ascii(char *s);
bool str_empieza(const char *s, const char *pref);   /* case-insensitive ASCII */
bool str_contiene(const char *s, const char *sub);   /* case-insensitive ASCII */
const char *str_buscar(const char *s, const char *sub); /* case-insensitive ASCII */
void str_copiar(char *dst, size_t cap, const char *src);
void str_solo_digitos(const char *s, char *out, size_t cap);
size_t str_contar_digitos(const char *s);
bool str_es_digitos(const char *s);

/* Confusiones típicas del OCR entre letras y dígitos */
char ocr_a_digito(char c);     /* O->0, I/l->1, S->5, B->8, Z->2, G->6, q->9, T->7 ; devuelve 0 si no aplica */
char ocr_a_letra(char c);      /* 0->O, 1->I, 5->S, 8->B, 2->Z, 6->G ; 0 si no aplica */
/* Convierte agresivamente un token que DEBERÍA ser numérico (RUC, número de serie) */
void ocr_forzar_digitos(const char *s, char *out, size_t cap);

/* Distancia de Levenshtein acotada (para comparar razón social / etiquetas) */
int  str_levenshtein(const char *a, const char *b, int max);
/* Similitud 0..1 basada en Levenshtein sobre cadenas normalizadas */
double str_similitud(const char *a, const char *b);

#endif

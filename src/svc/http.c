/* Servidor HTTP propio, sin dependencias: un hilo por conexión, cuerpo crudo o multipart/form-data.
 *   GET  /v1/salud                 -> {"ok":true,...}
 *   POST /v1/comprobante           -> cuerpo = archivo (PDF/XML/ZIP/JPG/PNG/TIFF) o multipart con campo "archivo"
 *   POST /v1/texto                 -> cuerpo = texto plano ya extraído (pruebas)
 *   ?bonito=1 para JSON con sangría
 */
#include "../../include/ocrx.h"
#include "../core/str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET sock_t;
#  define SOCK_INVALIDO INVALID_SOCKET
#  define cerrar_sock closesocket
#else
#  include <sys/time.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <pthread.h>
#  include <signal.h>
   typedef int sock_t;
#  define SOCK_INVALIDO (-1)
#  define cerrar_sock close
#endif

#define MAX_CUERPO (64u * 1024u * 1024u)

static ocrx_motor *g_motor;
static volatile int g_corriendo = 1;
static sock_t g_escucha = SOCK_INVALIDO;
static long g_atendidos = 0;

typedef struct { sock_t s; char ip[64]; } conexion;

static int enviar_todo(sock_t s, const char *b, size_t n) {
    while (n) { int k = send(s, b, (int)(n > 65536 ? 65536 : n), 0); if (k <= 0) return -1; b += k; n -= (size_t)k; }
    return 0;
}
static void responder(sock_t s, int codigo, const char *tipo, const char *cuerpo, size_t n) {
    const char *txt = codigo == 200 ? "OK" : codigo == 400 ? "Bad Request" : codigo == 404 ? "Not Found" : codigo == 405 ? "Method Not Allowed" : codigo == 413 ? "Payload Too Large" : codigo == 415 ? "Unsupported Media Type" : codigo == 422 ? "Unprocessable Entity" : "Internal Server Error";
    char cab[512];
    int l = snprintf(cab, sizeof cab, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\nServer: ocrx/%s\r\n\r\n", codigo, txt, tipo, n, OCRX_VERSION);
    enviar_todo(s, cab, (size_t)l);
    if (n) enviar_todo(s, cuerpo, n);
}
static void responder_error(sock_t s, int codigo, const char *msg) {
    char b[1024]; int l = snprintf(b, sizeof b, "{\"ok\":false,\"error\":\"%s\"}", msg);
    responder(s, codigo, "application/json; charset=utf-8", b, (size_t)l);
}

/* multipart/form-data: devuelve puntero al contenido del primer archivo (campo "archivo" o cualquiera con filename) */
static const char *multipart_archivo(const char *ct, const char *cuerpo, size_t n, size_t *out_n, char *nombre, size_t nombre_cap) {
    const char *b = str_buscar(ct, "boundary=");
    if (!b) return NULL;
    b += 9; if (*b == '"') b++;
    char lim[200]; size_t k = 0; lim[k++] = '-'; lim[k++] = '-';
    while (*b && *b != ';' && *b != '"' && *b != '\r' && k < 198) lim[k++] = *b++;
    lim[k] = 0; size_t ll = k;
    const char *p = cuerpo, *fin = cuerpo + n;
    const char *mejor = NULL; size_t mejor_n = 0;
    while (p < fin) {
        /* buscar límite */
        const char *q = NULL;
        for (const char *t = p; t + ll <= fin; t++) if (!memcmp(t, lim, ll)) { q = t; break; }
        if (!q) break;
        q += ll; if (q + 2 <= fin && q[0] == '-' && q[1] == '-') break;
        if (q + 2 <= fin && q[0] == '\r' && q[1] == '\n') q += 2;
        /* cabeceras de la parte */
        const char *h = q; const char *hfin = NULL;
        for (const char *t = h; t + 4 <= fin; t++) if (!memcmp(t, "\r\n\r\n", 4)) { hfin = t; break; }
        if (!hfin) break;
        char cabs[1024]; size_t hl = (size_t)(hfin - h); if (hl > 1023) hl = 1023; memcpy(cabs, h, hl); cabs[hl] = 0;
        const char *datos = hfin + 4;
        /* fin de datos = siguiente límite - CRLF */
        const char *d_fin = NULL;
        for (const char *t = datos; t + ll <= fin; t++) if (!memcmp(t, lim, ll)) { d_fin = t; break; }
        if (!d_fin) d_fin = fin;
        size_t dn = (size_t)(d_fin - datos); if (dn >= 2 && datos[dn - 2] == '\r') dn -= 2;
        bool es_archivo = str_contiene(cabs, "filename=");
        bool campo_archivo = str_contiene(cabs, "name=\"archivo\"") || str_contiene(cabs, "name=\"file\"") || str_contiene(cabs, "name=\"documento\"");
        if ((es_archivo || campo_archivo) && (!mejor || campo_archivo)) {
            mejor = datos; mejor_n = dn;
            const char *fn = str_buscar(cabs, "filename=\""); if (fn && nombre) { fn += 10; size_t i = 0; while (*fn && *fn != '"' && i + 1 < nombre_cap) nombre[i++] = *fn++; nombre[i] = 0; }
            if (campo_archivo) break;
        }
        p = d_fin;
    }
    *out_n = mejor_n; return mejor;
}

static void atender(conexion *cx) {
    sock_t s = cx->s;
    size_t cap = 64 * 1024, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { responder_error(s, 500, "sin memoria"); return; }
    /* leer cabeceras */
    char *sep = NULL;
    for (;;) {
        if (len + 4096 > cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); responder_error(s, 500, "sin memoria"); return; } buf = nb; }
        int k = recv(s, buf + len, (int)(cap - len - 1), 0);
        if (k <= 0) { free(buf); return; }
        len += (size_t)k; buf[len] = 0;
        sep = strstr(buf, "\r\n\r\n");
        if (sep) break;
        if (len > 64 * 1024) { responder_error(s, 400, "cabeceras demasiado largas"); free(buf); return; }
    }
    size_t cab_len = (size_t)(sep + 4 - buf);
    char metodo[16] = {0}, ruta[1024] = {0};
    sscanf(buf, "%15s %1023s", metodo, ruta);
    char *qs = strchr(ruta, '?'); bool bonito = false;
    if (qs) { *qs++ = 0; bonito = str_contiene(qs, "bonito=1") || str_contiene(qs, "pretty=1"); }
    /* content-length / content-type */
    size_t clen = 0; char ctype[256] = {0}; bool esperar_100 = false;
    for (char *h = strstr(buf, "\r\n") + 2; h < sep; ) {
        char *e = strstr(h, "\r\n"); if (!e) break; *e = 0;
        if (str_empieza(h, "Content-Length:")) clen = (size_t)strtoull(h + 15, NULL, 10);
        else if (str_empieza(h, "Content-Type:")) { const char *v = h + 13; while (*v == ' ') v++; str_copiar(ctype, sizeof ctype, v); }
        else if (str_empieza(h, "Expect:") && str_contiene(h, "100-continue")) esperar_100 = true;
        *e = '\r'; h = e + 2;
    }
    if (!strcmp(metodo, "GET") || !strcmp(metodo, "HEAD")) {
        if (!strcmp(ruta, "/v1/salud") || !strcmp(ruta, "/health") || !strcmp(ruta, "/salud")) {
            char b[256]; int l = snprintf(b, sizeof b, "{\"ok\":true,\"version\":\"%s\",\"atendidos\":%ld}", OCRX_VERSION, g_atendidos);
            responder(s, 200, "application/json; charset=utf-8", b, (size_t)l);
        } else if (!strcmp(ruta, "/")) {
            const char *h = "<html><body style='font-family:sans-serif'><h2>OCRX</h2><p>POST /v1/comprobante con el archivo en el cuerpo (o multipart, campo <b>archivo</b>). GET /v1/salud.</p>"
                            "<form method=post action='/v1/comprobante?bonito=1' enctype='multipart/form-data'><input type=file name=archivo><button>Procesar</button></form></body></html>";
            responder(s, 200, "text/html; charset=utf-8", h, strlen(h));
        } else responder_error(s, 404, "ruta no encontrada");
        free(buf); return;
    }
    if (strcmp(metodo, "POST")) { responder_error(s, 405, "use POST"); free(buf); return; }
    if (strcmp(ruta, "/v1/comprobante") && strcmp(ruta, "/v1/texto") && strcmp(ruta, "/comprobante") && strcmp(ruta, "/ocr")) { responder_error(s, 404, "ruta no encontrada"); free(buf); return; }
    if (clen == 0) { responder_error(s, 400, "cuerpo vacio: envie el archivo en el cuerpo o como multipart"); free(buf); return; }
    if (clen > MAX_CUERPO) { responder_error(s, 413, "archivo demasiado grande"); free(buf); return; }
    if (esperar_100) enviar_todo(s, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    /* leer cuerpo */
    size_t total = cab_len + clen;
    if (total + 1 > cap) { char *nb = (char *)realloc(buf, total + 1); if (!nb) { free(buf); responder_error(s, 500, "sin memoria"); return; } buf = nb; cap = total + 1; }
    while (len < total) { int k = recv(s, buf + len, (int)(total - len), 0); if (k <= 0) { free(buf); return; } len += (size_t)k; }
    const char *datos = buf + cab_len; size_t n = clen; char nombre[256] = {0};
    if (str_contiene(ctype, "multipart/form-data")) {
        size_t mn; const char *md = multipart_archivo(ctype, datos, n, &mn, nombre, sizeof nombre);
        if (!md || !mn) { responder_error(s, 400, "multipart sin campo de archivo"); free(buf); return; }
        datos = md; n = mn;
    }
    ocrx_error err;
    ocrx_resultado *r = !strcmp(ruta, "/v1/texto") ? ocrx_procesar_memoria(g_motor, datos, n, "texto.txt", &err)
                                                   : ocrx_procesar_memoria(g_motor, datos, n, nombre[0] ? nombre : NULL, &err);
    if (!r) { responder_error(s, 500, ocrx_error_texto(err)); free(buf); return; }
    const char *json = ocrx_resultado_json(r, bonito);
    /* envolver con ok/error */
    size_t jl = strlen(json); char *out = (char *)malloc(jl + 256);
    int l = snprintf(out, jl + 256, "{\"ok\":%s,\"error\":%s%s%s,\"comprobante\":%s}", err == OCRX_OK ? "true" : "false", err == OCRX_OK ? "null" : "\"", err == OCRX_OK ? "" : ocrx_error_texto(err), err == OCRX_OK ? "" : "\"", json);
    int codigo = err == OCRX_OK ? 200 : (err == OCRX_E_FORMATO ? 415 : 422);
    responder(s, codigo, "application/json; charset=utf-8", out, (size_t)l);
    free(out); ocrx_resultado_liberar(r); free(buf);
    g_atendidos++;
}

#ifdef _WIN32
static DWORD WINAPI hilo(LPVOID p) { conexion *cx = (conexion *)p; atender(cx); cerrar_sock(cx->s); free(cx); return 0; }
#else
static void *hilo(void *p) { conexion *cx = (conexion *)p; atender(cx); cerrar_sock(cx->s); free(cx); return NULL; }
#endif

void svc_detener(void) { g_corriendo = 0; if (g_escucha != SOCK_INVALIDO) { cerrar_sock(g_escucha); g_escucha = SOCK_INVALIDO; } }

int svc_servir(const ocrx_config *cfg, int puerto, int hilos) {
    (void)hilos;
    ocrx_error err;
    g_motor = ocrx_crear(cfg, &err);
    if (!g_motor) { fprintf(stderr, "no se pudo iniciar el motor: %s\n", ocrx_error_texto(err)); return 1; }
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    g_escucha = socket(AF_INET, SOCK_STREAM, 0);
    int uno = 1; setsockopt(g_escucha, SOL_SOCKET, SO_REUSEADDR, (const char *)&uno, sizeof uno);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((unsigned short)puerto);
    if (bind(g_escucha, (struct sockaddr *)&a, sizeof a) != 0) { fprintf(stderr, "no se pudo abrir el puerto %d\n", puerto); return 1; }
    listen(g_escucha, 64);
    fprintf(stderr, "ocrx %s escuchando en http://0.0.0.0:%d  (POST /v1/comprobante, GET /v1/salud)\n", OCRX_VERSION, puerto);
    while (g_corriendo) {
        struct sockaddr_in c; socklen_t cl = sizeof c;
        sock_t s = accept(g_escucha, (struct sockaddr *)&c, &cl);
        if (s == SOCK_INVALIDO) { if (!g_corriendo) break; continue; }
#ifdef _WIN32
        DWORD to = 30000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&to, sizeof to);
#else
        struct timeval tv = {30, 0}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
        conexion *cx = (conexion *)calloc(1, sizeof *cx); cx->s = s;
        inet_ntop(AF_INET, &c.sin_addr, cx->ip, sizeof cx->ip);
#ifdef _WIN32
        HANDLE h = CreateThread(NULL, 0, hilo, cx, 0, NULL); if (h) CloseHandle(h); else { atender(cx); cerrar_sock(s); free(cx); }
#else
        pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &at, hilo, cx) != 0) { atender(cx); cerrar_sock(s); free(cx); }
        pthread_attr_destroy(&at);
#endif
    }
    ocrx_destruir(g_motor); g_motor = NULL;
    return 0;
}

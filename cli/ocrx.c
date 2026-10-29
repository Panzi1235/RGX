/* ocrx.exe — herramienta de línea de comandos y servicio.
 *   ocrx archivo.pdf [--json|--bonito] [--modelos DIR]
 *   ocrx --servir [--puerto 8085]
 *   ocrx --instalar [--puerto 8085] | --desinstalar | --estado      (sólo Windows)
 *   ocrx --lote CARPETA  [--salida resultados.csv]
 */
#include "../include/ocrx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int svc_servir(const ocrx_config *cfg, int puerto, int hilos);
int svc_instalar(int puerto, const char *ruta_exe);
int svc_desinstalar(void);
int svc_estado(void);
int svc_main_windows(const ocrx_config *cfg, int puerto);

static void uso(void) {
    fprintf(stderr,
        "OCRX %s - lectura de comprobantes SUNAT\n"
        "  ocrx <archivo>              procesa un PDF/XML/ZIP/imagen/texto y escribe JSON\n"
        "  ocrx <archivo> --bonito     JSON con sangria\n"
        "  ocrx --lote <carpeta>       procesa todos los archivos de la carpeta (CSV por stdout)\n"
        "  ocrx --servir [--puerto N]  servicio HTTP en primer plano\n"
#ifdef _WIN32
        "  ocrx --instalar [--puerto N]  registra e inicia el servicio de Windows\n"
        "  ocrx --desinstalar            detiene y elimina el servicio\n"
        "  ocrx --estado                 estado del servicio\n"
#endif
        "  opciones: --modelos DIR  --padron ARCHIVO  --hilos N  --sin-gpu  --texto (muestra las lineas del OCR)\n", OCRX_VERSION);
}

#ifdef _WIN32
#include <windows.h>
static int listar(const char *carpeta, char ***out, int *n) {
    char patron[MAX_PATH]; snprintf(patron, sizeof patron, "%s\\*", carpeta);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(patron, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int cap = 64; *n = 0; *out = (char **)malloc(sizeof(char *) * cap);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (*n == cap) { cap *= 2; *out = (char **)realloc(*out, sizeof(char *) * cap); }
        char *ruta = (char *)malloc(MAX_PATH); snprintf(ruta, MAX_PATH, "%s\\%s", carpeta, fd.cFileName); (*out)[(*n)++] = ruta;
    } while (FindNextFileA(h, &fd));
    FindClose(h); return 0;
}
#else
#include <dirent.h>
static int listar(const char *carpeta, char ***out, int *n) {
    DIR *d = opendir(carpeta); if (!d) return -1;
    int cap = 64; *n = 0; *out = (char **)malloc(sizeof(char *) * cap);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (*n == cap) { cap *= 2; *out = (char **)realloc(*out, sizeof(char *) * cap); }
        char *ruta = (char *)malloc(4096); snprintf(ruta, 4096, "%s/%s", carpeta, e->d_name); (*out)[(*n)++] = ruta;
    }
    closedir(d); return 0;
}
#endif

static void csv_campo(const char *s) { putchar('"'); for (; *s; s++) { if (*s == '"') putchar('"'); putchar(*s); } putchar('"'); }

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);   /* UTF-8 en la consola */
#endif
    ocrx_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.usar_gpu = true; cfg.dpi_render = 300; cfg.umbral_revision = 0.85f;
    const char *archivo = NULL, *lote = NULL; bool bonito = false, servir = false, instalar = false, desinstalar = false, estado = false, modo_servicio = false;
    int puerto = 8085;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--bonito") || !strcmp(a, "--pretty")) bonito = true;
        else if (!strcmp(a, "--json")) bonito = false;
        else if (!strcmp(a, "--servir")) servir = true;
        else if (!strcmp(a, "--servicio")) modo_servicio = true;   /* invocado por el SCM de Windows */
        else if (!strcmp(a, "--instalar")) instalar = true;
        else if (!strcmp(a, "--desinstalar")) desinstalar = true;
        else if (!strcmp(a, "--estado")) estado = true;
        else if (!strcmp(a, "--sin-gpu")) cfg.usar_gpu = false;
        else if (!strcmp(a, "--texto")) cfg.volcar_texto = true;
        else if (!strcmp(a, "--puerto") && i + 1 < argc) puerto = atoi(argv[++i]);
        else if (!strcmp(a, "--hilos") && i + 1 < argc) cfg.hilos = atoi(argv[++i]);
        else if (!strcmp(a, "--modelos") && i + 1 < argc) cfg.dir_modelos = argv[++i];
        else if (!strcmp(a, "--padron") && i + 1 < argc) cfg.ruta_padron = argv[++i];
        else if (!strcmp(a, "--lote") && i + 1 < argc) lote = argv[++i];
        else if (!strcmp(a, "--umbral") && i + 1 < argc) cfg.umbral_revision = (float)atof(argv[++i]);
        else if (!strcmp(a, "-h") || !strcmp(a, "--help") || !strcmp(a, "/?")) { uso(); return 0; }
        else if (a[0] == '-' ) { fprintf(stderr, "opcion desconocida: %s\n", a); uso(); return 2; }
        else archivo = a;
    }
#ifdef _WIN32
    if (instalar) return svc_instalar(puerto, argv[0]);
    if (desinstalar) return svc_desinstalar();
    if (estado) return svc_estado();
    if (modo_servicio) return svc_main_windows(&cfg, puerto);
#else
    (void)instalar; (void)desinstalar; (void)estado; (void)modo_servicio;
#endif
    if (servir) return svc_servir(&cfg, puerto, cfg.hilos);

    ocrx_error err;
    ocrx_motor *m = ocrx_crear(&cfg, &err);
    if (!m) { fprintf(stderr, "no se pudo iniciar el motor: %s\n", ocrx_error_texto(err)); return 1; }

    if (lote) {
        char **rutas; int n;
        if (listar(lote, &rutas, &n) < 0) { fprintf(stderr, "no se pudo abrir la carpeta %s\n", lote); return 1; }
        printf("archivo,estado,tipo,serie,numero,fecha,rucEmisor,razonSocial,moneda,gravada,igv,total,cuadra,revision,motivo,ms\n");
        for (int i = 0; i < n; i++) {
            ocrx_resultado *r = ocrx_procesar_archivo(m, rutas[i], &err);
            const ocrx_comprobante *c = r ? ocrx_resultado_comprobante(r) : NULL;
            csv_campo(rutas[i]); putchar(',');
            csv_campo(ocrx_error_texto(err)); putchar(',');
            if (c) {
                extern const char *sunat_tipo_codigo(ocrx_tipo_cpe); extern const char *sunat_moneda_codigo(ocrx_moneda);
                printf("%s,%s,%s,%s,%s,", sunat_tipo_codigo(c->tipo), c->serie, c->numero, c->fecha_emision, c->emisor.ruc);
                csv_campo(c->emisor.razon_social);
                printf(",%s,%.2f,%.2f,%.2f,%d,%d,", sunat_moneda_codigo(c->moneda), c->totales.gravada, c->totales.igv, c->totales.total, c->totales.cuadra, c->requiere_revision);
                csv_campo(c->motivo_revision); printf(",%.0f", c->ms_total);
            } else printf(",,,,,,,,,,,,,");
            putchar('\n');
            ocrx_resultado_liberar(r);
        }
        ocrx_destruir(m);
        return 0;
    }
    if (!archivo) { uso(); return 2; }
    ocrx_resultado *r = ocrx_procesar_archivo(m, archivo, &err);
    if (!r) { fprintf(stderr, "error: %s\n", ocrx_error_texto(err)); ocrx_destruir(m); return 1; }
    if (err != OCRX_OK) fprintf(stderr, "aviso: %s\n", ocrx_error_texto(err));
    puts(ocrx_resultado_json(r, bonito));
    ocrx_resultado_liberar(r);
    ocrx_destruir(m);
    return err == OCRX_OK ? 0 : 3;
}

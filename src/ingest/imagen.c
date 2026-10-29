#include "imagen.h"
#include "../core/str.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdint.h>

static void err(char *e, size_t cap, const char *m) { if (e && cap) str_copiar(e, cap, m); }

/* ---------------- EXIF (orientación) en JPEG ---------------- */
static uint16_t rd16(const unsigned char *p, int le) { return le ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const unsigned char *p, int le) { return le ? (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) : (uint32_t)(((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

static int jpeg_orientacion_exif(const unsigned char *d, size_t n) {
    size_t i = 2;
    while (i + 4 <= n && d[i] == 0xFF) {
        unsigned char m = d[i + 1]; size_t len = ((size_t)d[i + 2] << 8) | d[i + 3];
        if (m == 0xDA || m == 0xD9) break;
        if (m == 0xE1 && len >= 14 && i + 2 + len <= n && !memcmp(d + i + 4, "Exif\0\0", 6)) {
            const unsigned char *t = d + i + 10; size_t tn = len - 8;
            if (tn < 8) return 0;
            int le = t[0] == 'I';
            uint32_t off = rd32(t + 4, le);
            if (off + 2 > tn) return 0;
            uint16_t cnt = rd16(t + off, le);
            for (uint16_t k = 0; k < cnt; k++) {
                size_t e = off + 2 + (size_t)k * 12; if (e + 12 > tn) break;
                if (rd16(t + e, le) == 0x0112) { uint16_t v = rd16(t + e + 8, le); return (v >= 1 && v <= 8) ? v : 0; }
            }
            return 0;
        }
        i += 2 + len;
    }
    return 0;
}

void imagen_aplicar_orientacion(imagen_gris *img, int o, arena *a) {
    if (o <= 1 || o > 8 || !img->pix) return;
    int w = img->ancho, h = img->alto;
    int nw = (o >= 5) ? h : w, nh = (o >= 5) ? w : h;
    unsigned char *out = (unsigned char *)arena_alloc(a, (size_t)nw * nh);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int nx, ny;
        switch (o) {
        case 2: nx = w - 1 - x; ny = y; break;
        case 3: nx = w - 1 - x; ny = h - 1 - y; break;
        case 4: nx = x; ny = h - 1 - y; break;
        case 5: nx = y; ny = x; break;
        case 6: nx = h - 1 - y; ny = x; break;
        case 7: nx = h - 1 - y; ny = w - 1 - x; break;
        case 8: nx = y; ny = w - 1 - x; break;
        default: nx = x; ny = y;
        }
        out[(size_t)ny * nw + nx] = img->pix[(size_t)y * w + x];
    }
    img->pix = out; img->ancho = nw; img->alto = nh;
}

/* ---------------- BMP (propio) ---------------- */
static bool cargar_bmp(const unsigned char *d, size_t n, imagen_gris *out, arena *a, char *e, size_t ec) {
    if (n < 54) { err(e, ec, "BMP truncado"); return false; }
    uint32_t off = rd32(d + 10, 1); int32_t w = (int32_t)rd32(d + 18, 1), h = (int32_t)rd32(d + 22, 1); uint16_t bpp = rd16(d + 28, 1); uint32_t comp = rd32(d + 30, 1);
    bool abajo = h > 0; if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || (comp != 0 && comp != 3) || (bpp != 24 && bpp != 32 && bpp != 8)) { err(e, ec, "BMP no soportado"); return false; }
    size_t fila = (((size_t)w * bpp + 31) / 32) * 4;
    if (off + fila * (size_t)h > n) { err(e, ec, "BMP truncado"); return false; }
    const unsigned char *pal = d + 54;
    out->pix = (unsigned char *)arena_alloc(a, (size_t)w * h); out->ancho = w; out->alto = h;
    for (int y = 0; y < h; y++) {
        const unsigned char *f = d + off + fila * (size_t)(abajo ? h - 1 - y : y);
        unsigned char *o = out->pix + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            if (bpp == 8) { const unsigned char *c = pal + 4 * f[x]; o[x] = (unsigned char)((c[0] * 29 + c[1] * 150 + c[2] * 77) >> 8); }
            else { const unsigned char *c = f + x * (bpp / 8); o[x] = (unsigned char)((c[0] * 29 + c[1] * 150 + c[2] * 77) >> 8); }
        }
    }
    return true;
}

/* ---------------- JPEG ---------------- */
#ifdef OCRX_HAVE_JPEG
#include <jpeglib.h>
struct jerr { struct jpeg_error_mgr pub; jmp_buf salto; char msg[JMSG_LENGTH_MAX]; };
static void jerr_exit(j_common_ptr c) { struct jerr *j = (struct jerr *)c->err; (*c->err->format_message)(c, j->msg); longjmp(j->salto, 1); }
static void jerr_silencio(j_common_ptr c) { (void)c; }

static bool cargar_jpeg(const unsigned char *d, size_t n, imagen_gris *out, arena *a, char *e, size_t ec) {
    struct jpeg_decompress_struct ci; struct jerr je;
    ci.err = jpeg_std_error(&je.pub); je.pub.error_exit = jerr_exit; je.pub.output_message = jerr_silencio;
    if (setjmp(je.salto)) { jpeg_destroy_decompress(&ci); err(e, ec, je.msg); return false; }
    jpeg_create_decompress(&ci);
    jpeg_mem_src(&ci, (unsigned char *)d, (unsigned long)n);
    if (jpeg_read_header(&ci, TRUE) != JPEG_HEADER_OK) { jpeg_destroy_decompress(&ci); err(e, ec, "cabecera JPEG invalida"); return false; }
    ci.out_color_space = JCS_GRAYSCALE;
    ci.dct_method = JDCT_IFAST;
    /* fotos gigantes (48 MP): reducir en el decodificador a <= ~12 MP para no agotar memoria */
    while ((long long)(ci.image_width / ci.scale_denom) * (ci.image_height / ci.scale_denom) > 12000000LL && ci.scale_denom < 8) ci.scale_denom *= 2;
    jpeg_start_decompress(&ci);
    int w = (int)ci.output_width, h = (int)ci.output_height;
    out->pix = (unsigned char *)arena_alloc(a, (size_t)w * h); out->ancho = w; out->alto = h;
    if (ci.density_unit == 1) out->dpi = ci.X_density; else if (ci.density_unit == 2) out->dpi = (int)(ci.X_density * 2.54);
    while (ci.output_scanline < ci.output_height) { JSAMPROW fila = out->pix + (size_t)ci.output_scanline * w; jpeg_read_scanlines(&ci, &fila, 1); }
    jpeg_finish_decompress(&ci); jpeg_destroy_decompress(&ci);
    out->orientacion_exif = jpeg_orientacion_exif(d, n);
    if (out->orientacion_exif > 1) imagen_aplicar_orientacion(out, out->orientacion_exif, a);
    return true;
}
#endif

/* ---------------- PNG ---------------- */
#ifdef OCRX_HAVE_PNG
#include <png.h>
typedef struct { const unsigned char *d; size_t n, pos; } pngsrc;
static void png_leer(png_structp p, png_bytep out, png_size_t len) { pngsrc *s = (pngsrc *)png_get_io_ptr(p); if (s->pos + len > s->n) png_error(p, "PNG truncado"); memcpy(out, s->d + s->pos, len); s->pos += len; }
static bool cargar_png(const unsigned char *d, size_t n, imagen_gris *out, arena *a, char *e, size_t ec) {
    png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL); if (!p) return false;
    png_infop info = png_create_info_struct(p); if (!info) { png_destroy_read_struct(&p, NULL, NULL); return false; }
    if (setjmp(png_jmpbuf(p))) { png_destroy_read_struct(&p, &info, NULL); err(e, ec, "PNG invalido"); return false; }
    pngsrc s = { d, n, 0 }; png_set_read_fn(p, &s, png_leer);
    png_read_info(p, info);
    png_uint_32 w, h; int depth, tipo;
    png_get_IHDR(p, info, &w, &h, &depth, &tipo, NULL, NULL, NULL);
    if ((long long)w * h > 80000000LL) { png_destroy_read_struct(&p, &info, NULL); err(e, ec, "PNG demasiado grande"); return false; }
    if (tipo == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(p);
    if (tipo == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(p);
    if (png_get_valid(p, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(p);
    if (depth == 16) png_set_strip_16(p);
    if ((tipo & PNG_COLOR_MASK_ALPHA) || png_get_valid(p, info, PNG_INFO_tRNS)) { png_color_16 blanco = {0, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}; png_set_background(p, &blanco, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0); } /* alfa sobre blanco */
    if (tipo == PNG_COLOR_TYPE_RGB || tipo == PNG_COLOR_TYPE_RGB_ALPHA || tipo == PNG_COLOR_TYPE_PALETTE) png_set_rgb_to_gray_fixed(p, 1, 21268, 71514);
    png_read_update_info(p, info);
    size_t fila = png_get_rowbytes(p, info);
    unsigned char *buf = (unsigned char *)arena_alloc(a, fila);
    out->pix = (unsigned char *)arena_alloc(a, (size_t)w * h); out->ancho = (int)w; out->alto = (int)h;
    int canales = png_get_channels(p, info);
    for (png_uint_32 y = 0; y < h; y++) {
        png_read_row(p, buf, NULL);
        unsigned char *o = out->pix + (size_t)y * w;
        if (canales == 1) memcpy(o, buf, w);
        else for (png_uint_32 x = 0; x < w; x++) o[x] = buf[x * canales];
    }
    png_uint_32 rx, ry; int unit; if (png_get_pHYs(p, info, &rx, &ry, &unit) && unit == PNG_RESOLUTION_METER) out->dpi = (int)(rx * 0.0254 + 0.5);
    png_destroy_read_struct(&p, &info, NULL);
    return true;
}
#endif

/* ---------------- TIFF ---------------- */
#ifdef OCRX_HAVE_TIFF
#include <tiffio.h>
typedef struct { const unsigned char *d; size_t n, pos; } tifsrc;
static tmsize_t t_read(thandle_t h, void *b, tmsize_t l) { tifsrc *s = (tifsrc *)h; if (s->pos + (size_t)l > s->n) l = (tmsize_t)(s->n - s->pos); memcpy(b, s->d + s->pos, (size_t)l); s->pos += (size_t)l; return l; }
static tmsize_t t_write(thandle_t h, void *b, tmsize_t l) { (void)h; (void)b; (void)l; return 0; }
static toff_t t_seek(thandle_t h, toff_t o, int w) { tifsrc *s = (tifsrc *)h; if (w == SEEK_SET) s->pos = (size_t)o; else if (w == SEEK_CUR) s->pos += (size_t)o; else s->pos = s->n + (size_t)o; return (toff_t)s->pos; }
static int t_close(thandle_t h) { (void)h; return 0; }
static toff_t t_size(thandle_t h) { return (toff_t)((tifsrc *)h)->n; }
static void t_silencio(const char *m, const char *f, va_list ap) { (void)m; (void)f; (void)ap; }

static bool cargar_tiff(const unsigned char *d, size_t n, imagen_gris *out, arena *a, char *e, size_t ec) {
    TIFFSetErrorHandler(t_silencio); TIFFSetWarningHandler(t_silencio);
    tifsrc s = { d, n, 0 };
    TIFF *t = TIFFClientOpen("mem", "rm", (thandle_t)&s, t_read, t_write, t_seek, t_close, t_size, NULL, NULL);
    if (!t) { err(e, ec, "TIFF invalido"); return false; }
    uint32_t w, h; TIFFGetField(t, TIFFTAG_IMAGEWIDTH, &w); TIFFGetField(t, TIFFTAG_IMAGELENGTH, &h);
    if ((long long)w * h > 80000000LL) { TIFFClose(t); err(e, ec, "TIFF demasiado grande"); return false; }
    uint32_t *rgba = (uint32_t *)arena_alloc(a, (size_t)w * h * 4);
    if (!TIFFReadRGBAImageOriented(t, w, h, rgba, ORIENTATION_TOPLEFT, 0)) { TIFFClose(t); err(e, ec, "no se pudo decodificar el TIFF"); return false; }
    float xres = 0; uint16_t unit = 2; if (TIFFGetField(t, TIFFTAG_XRESOLUTION, &xres) && TIFFGetField(t, TIFFTAG_RESOLUTIONUNIT, &unit)) out->dpi = unit == 3 ? (int)(xres * 2.54) : (int)xres;
    TIFFClose(t);
    out->pix = (unsigned char *)arena_alloc(a, (size_t)w * h); out->ancho = (int)w; out->alto = (int)h;
    for (size_t i = 0; i < (size_t)w * h; i++) { uint32_t c = rgba[i]; out->pix[i] = (unsigned char)((TIFFGetR(c) * 77 + TIFFGetG(c) * 150 + TIFFGetB(c) * 29) >> 8); }
    return true;
}
#endif

bool imagen_formato_soportado(const void *dv, size_t n) {
    const unsigned char *d = (const unsigned char *)dv;
    if (n < 4) return false;
#ifdef OCRX_HAVE_JPEG
    if (d[0] == 0xFF && d[1] == 0xD8) return true;
#endif
#ifdef OCRX_HAVE_PNG
    if (n >= 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return true;
#endif
#ifdef OCRX_HAVE_TIFF
    if (!memcmp(d, "II*\0", 4) || !memcmp(d, "MM\0*", 4)) return true;
#endif
    if (d[0] == 'B' && d[1] == 'M') return true;
    return false;
}

bool imagen_cargar_gris(const void *dv, size_t n, imagen_gris *out, arena *a, char *e, size_t ec) {
    const unsigned char *d = (const unsigned char *)dv;
    memset(out, 0, sizeof *out);
    if (n < 4) { err(e, ec, "archivo vacio"); return false; }
    if (d[0] == 0xFF && d[1] == 0xD8) {
#ifdef OCRX_HAVE_JPEG
        return cargar_jpeg(d, n, out, a, e, ec);
#else
        err(e, ec, "JPEG no compilado"); return false;
#endif
    }
    if (n >= 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8)) {
#ifdef OCRX_HAVE_PNG
        return cargar_png(d, n, out, a, e, ec);
#else
        err(e, ec, "PNG no compilado"); return false;
#endif
    }
    if (!memcmp(d, "II*\0", 4) || !memcmp(d, "MM\0*", 4)) {
#ifdef OCRX_HAVE_TIFF
        return cargar_tiff(d, n, out, a, e, ec);
#else
        err(e, ec, "TIFF no compilado"); return false;
#endif
    }
    if (d[0] == 'B' && d[1] == 'M') return cargar_bmp(d, n, out, a, e, ec);
    err(e, ec, "formato de imagen no soportado (use JPG, PNG, TIFF o BMP)");
    return false;
}

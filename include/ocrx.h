/*
 * OCRX — Motor de lectura de comprobantes SUNAT (Perú)
 * API pública en C. Estable entre versiones menores.
 *
 * Flujo:  ocrx_crear() -> ocrx_procesar_*() -> ocrx_comprobante_json() -> ocrx_resultado_liberar() -> ocrx_destruir()
 * Todo lo que devuelve una llamada ocrx_procesar_* vive en el resultado y se libera de una sola vez.
 */
#ifndef OCRX_H
#define OCRX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(OCRX_DLL)
#  ifdef OCRX_BUILD
#    define OCRX_API __declspec(dllexport)
#  else
#    define OCRX_API __declspec(dllimport)
#  endif
#else
#  define OCRX_API
#endif

#define OCRX_VERSION "0.1.0"

/* ---------- Códigos de error ---------- */
typedef enum ocrx_error {
    OCRX_OK = 0,
    OCRX_E_PARAM,          /* parámetro inválido */
    OCRX_E_MEMORIA,
    OCRX_E_ARCHIVO,        /* no se pudo leer el archivo */
    OCRX_E_FORMATO,        /* formato no reconocido */
    OCRX_E_PDF,            /* PDFium falló o no está compilado */
    OCRX_E_XML,            /* XML inválido o no es UBL SUNAT */
    OCRX_E_IMAGEN,         /* imagen ilegible */
    OCRX_E_SIN_DATOS,      /* no se encontró ningún dato de comprobante */
    OCRX_E_NO_DISPONIBLE   /* funcionalidad no compilada (p.ej. OCR sin modelos) */
} ocrx_error;

/* ---------- Origen y confianza de cada campo ---------- */
typedef enum ocrx_origen {
    OCRX_ORIGEN_NINGUNO = 0,
    OCRX_ORIGEN_XML,       /* UBL 2.1: dato oficial */
    OCRX_ORIGEN_QR,        /* código QR del comprobante */
    OCRX_ORIGEN_PDF_TEXTO, /* capa de texto del PDF */
    OCRX_ORIGEN_OCR,       /* reconocido en imagen */
    OCRX_ORIGEN_PADRON,    /* padrón RUC de SUNAT */
    OCRX_ORIGEN_CALCULO,   /* derivado por cuentas cruzadas */
    OCRX_ORIGEN_LETRAS     /* monto en letras */
} ocrx_origen;

typedef struct ocrx_campo_meta {
    ocrx_origen origen;
    float       confianza;   /* 0..1 */
    bool        presente;
    bool        corregido;   /* se cambió respecto a lo leído (confusión OCR, padrón, etc.) */
    bool        dudoso;      /* requiere revisión humana */
} ocrx_campo_meta;

/* ---------- Catálogos SUNAT ---------- */
/* Catálogo 01: tipo de comprobante */
typedef enum ocrx_tipo_cpe {
    OCRX_CPE_DESCONOCIDO = 0,
    OCRX_CPE_FACTURA = 1,          /* 01 */
    OCRX_CPE_BOLETA = 3,           /* 03 */
    OCRX_CPE_NOTA_CREDITO = 7,     /* 07 */
    OCRX_CPE_NOTA_DEBITO = 8,      /* 08 */
    OCRX_CPE_GUIA_REMITENTE = 9,   /* 09 */
    OCRX_CPE_TICKET_MAQUINA = 12,  /* 12 */
    OCRX_CPE_RECIBO_SERVICIOS = 14,/* 14 luz, agua, teléfono */
    OCRX_CPE_RETENCION = 20,       /* 20 */
    OCRX_CPE_GUIA_TRANSPORTISTA = 31, /* 31 */
    OCRX_CPE_PERCEPCION = 40,      /* 40 */
    OCRX_CPE_RECIBO_HONORARIOS = 2,/* 02 (R.H. electrónico) */
    OCRX_CPE_LIQUIDACION_COMPRA = 4/* 04 */
} ocrx_tipo_cpe;

/* Catálogo 06: tipo de documento de identidad */
typedef enum ocrx_tipo_doc_id {
    OCRX_DOC_SIN = 0,     /* 0 - sin documento (boletas < S/ 700) */
    OCRX_DOC_DNI = 1,     /* 1 */
    OCRX_DOC_CARNET_EXT = 4, /* 4 */
    OCRX_DOC_RUC = 6,     /* 6 */
    OCRX_DOC_PASAPORTE = 7, /* 7 */
    OCRX_DOC_CED_DIPLOMATICA = 10, /* A */
    OCRX_DOC_PTP = 11,    /* B */
    OCRX_DOC_TAM = 12,    /* C */
    OCRX_DOC_OTRO = 99
} ocrx_tipo_doc_id;

typedef enum ocrx_moneda { OCRX_MON_DESCONOCIDA = 0, OCRX_MON_PEN, OCRX_MON_USD, OCRX_MON_EUR } ocrx_moneda;

typedef enum ocrx_estado_ruc { OCRX_RUC_SIN_CONSULTAR = 0, OCRX_RUC_ACTIVO, OCRX_RUC_BAJA, OCRX_RUC_SUSPENDIDO, OCRX_RUC_NO_HALLADO } ocrx_estado_ruc;

/* ---------- Modelo del comprobante ---------- */
#define OCRX_MAX_LEN 256

typedef struct ocrx_parte {           /* emisor o cliente */
    char ruc[12];                     /* 11 dígitos + NUL cuando es RUC */
    ocrx_tipo_doc_id tipo_doc;
    char num_doc[32];                 /* DNI / CE / pasaporte / RUC */
    char razon_social[OCRX_MAX_LEN];
    char nombre_comercial[OCRX_MAX_LEN];
    char direccion[OCRX_MAX_LEN];
    char ubigeo[8];
    ocrx_estado_ruc estado_ruc;
    char condicion_ruc[32];           /* HABIDO / NO HABIDO */
    ocrx_campo_meta m_ruc, m_razon_social, m_direccion;
} ocrx_parte;

typedef struct ocrx_item {
    double cantidad;
    char   unidad[8];                 /* NIU, ZZ, KGM ... */
    char   codigo[32];
    char   descripcion[OCRX_MAX_LEN];
    double precio_unitario;           /* sin IGV */
    double precio_unitario_con_igv;
    double valor_venta;               /* cantidad * precio sin IGV */
    double igv;
    double importe;                   /* con IGV */
    char   afectacion[4];             /* catálogo 07: 10 gravado, 20 exonerado, 30 inafecto, 21 gratuito... */
    ocrx_campo_meta meta;
} ocrx_item;

typedef struct ocrx_totales {
    double gravada, exonerada, inafecta, exportacion, gratuita;
    double descuentos, otros_cargos;
    double isc, igv, icbper, otros_tributos;
    double total;
    double tasa_igv;                  /* 0.18 normal, 0.105 restaurantes/hoteles MYPE (Ley 31556), 0.10 ... */
    double detraccion_pct, detraccion_monto;
    char   detraccion_codigo[4];      /* catálogo 54 */
    char   detraccion_cuenta[24];     /* cuenta Banco de la Nación */
    double retencion_monto;           /* 3 % cuando aplica */
    double percepcion_monto;
    char   monto_en_letras[OCRX_MAX_LEN];
    bool   cuadra;                    /* las cuentas cruzadas cierran */
    ocrx_campo_meta m_gravada, m_igv, m_total, m_exonerada, m_inafecta;
} ocrx_totales;

typedef struct ocrx_referencia {      /* notas de crédito/débito y guías */
    ocrx_tipo_cpe tipo;
    char serie[8];
    char numero[12];
    char motivo_codigo[4];            /* catálogo 09/10 */
    char motivo[OCRX_MAX_LEN];
} ocrx_referencia;

typedef struct ocrx_comprobante {
    ocrx_tipo_cpe tipo;
    char serie[8];                    /* F001, B001, E001, T001, EB01 ... */
    char numero[12];                  /* hasta 8 dígitos, sin ceros de relleno garantizados */
    char fecha_emision[11];           /* AAAA-MM-DD */
    char hora_emision[9];             /* HH:MM:SS */
    char fecha_vencimiento[11];
    ocrx_moneda moneda;
    double tipo_cambio;
    char forma_pago[32];              /* Contado / Credito */
    ocrx_parte emisor;
    ocrx_parte cliente;
    ocrx_totales totales;
    ocrx_item  *items;
    size_t      n_items;
    ocrx_referencia referencia;
    char hash_cpe[64];                /* digest del XML si está */
    char qr_crudo[512];
    char observaciones[OCRX_MAX_LEN]; /* texto libre: "GRACIAS POR SU COMPRA", condiciones, etc. */
    char orden_compra[32];
    char guia_relacionada[24];
    ocrx_campo_meta m_tipo, m_serie, m_numero, m_fecha, m_moneda;
    bool requiere_revision;
    char motivo_revision[OCRX_MAX_LEN];
    /* Diagnóstico */
    int   paginas;
    bool  es_electronico;             /* tiene QR o XML o leyenda "Representación impresa" */
    bool  es_manual;                  /* talonario llenado a mano */
    bool  tenia_qr, tenia_xml, tenia_texto_pdf;
    float calidad_imagen;             /* 0..1 */
    double ms_total;
} ocrx_comprobante;

/* ---------- Motor ---------- */
typedef struct ocrx_motor ocrx_motor;

typedef struct ocrx_config {
    const char *dir_modelos;          /* carpeta models\ (puede ser NULL en fase 1) */
    const char *ruta_padron;          /* índice del padrón RUC (puede ser NULL) */
    int   hilos;                      /* 0 = automático */
    int   dpi_render;                 /* 300 por defecto */
    bool  usar_gpu;                   /* DirectML */
    bool  tesseract_respaldo;
    float umbral_revision;            /* confianza mínima para no marcar dudoso, 0.85 por defecto */
    const char *dir_log;
    bool  volcar_texto;               /* escribe por stderr las líneas de texto reconocidas (diagnóstico) */
} ocrx_config;

typedef struct ocrx_resultado ocrx_resultado;

OCRX_API const char *ocrx_version(void);
OCRX_API const char *ocrx_error_texto(ocrx_error e);

OCRX_API ocrx_motor *ocrx_crear(const ocrx_config *cfg, ocrx_error *err);
OCRX_API void        ocrx_destruir(ocrx_motor *m);

/* Procesa un archivo (PDF, JPG, PNG, TIFF, XML, ZIP con XML). Detecta el formato por contenido. */
OCRX_API ocrx_resultado *ocrx_procesar_archivo(ocrx_motor *m, const char *ruta, ocrx_error *err);
/* Procesa un buffer en memoria. nombre_pista es opcional (extensión) */
OCRX_API ocrx_resultado *ocrx_procesar_memoria(ocrx_motor *m, const void *datos, size_t n, const char *nombre_pista, ocrx_error *err);
/* Procesa texto ya extraído (por ejemplo de un PDF) — útil para pruebas */
OCRX_API ocrx_resultado *ocrx_procesar_texto(ocrx_motor *m, const char *texto_utf8, ocrx_error *err);

OCRX_API const ocrx_comprobante *ocrx_resultado_comprobante(const ocrx_resultado *r);
/* JSON UTF-8 terminado en NUL; válido mientras viva el resultado */
OCRX_API const char *ocrx_resultado_json(ocrx_resultado *r, bool bonito);
OCRX_API void        ocrx_resultado_liberar(ocrx_resultado *r);

/* Verificación rápida de calidad de imagen (para pedir repetir la foto al instante). 0..1 y motivo. */
OCRX_API float ocrx_calidad_imagen(ocrx_motor *m, const void *datos, size_t n, char *motivo, size_t motivo_cap);

#ifdef __cplusplus
}
#endif
#endif /* OCRX_H */

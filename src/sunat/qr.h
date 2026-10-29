/* Contenido del código QR de un comprobante electrónico SUNAT.
 * Formato (R.S. 300-2014 y modificatorias), campos separados por "|":
 *   RUC | TIPO | SERIE | NUMERO | IGV | TOTAL | FECHA | TIPO DOC ADQUIRENTE | NUM DOC ADQUIRENTE | [HASH] |
 * Variantes vistas en campo: fecha dd/mm/aaaa o aaaa-mm-dd, separador final opcional, montos con coma,
 * campos vacíos, URL de consulta en vez de texto plano (algunos facturadores). */
#ifndef OCRX_QR_H
#define OCRX_QR_H
#include <stdbool.h>
#include "../../include/ocrx.h"

typedef struct sunat_qr {
    bool ok;
    char ruc[12];
    ocrx_tipo_cpe tipo;
    char serie[8];
    char numero[12];
    double igv, total;
    char fecha[11];
    ocrx_tipo_doc_id tipo_doc_cliente;
    char num_doc_cliente[32];
    char hash[64];
    int  campos;         /* cuántos campos trajo */
    bool parece_url;     /* era una URL con parámetros */
} sunat_qr;

/* Parsea el texto decodificado del QR. Devuelve ok=true si al menos RUC+serie+número o RUC+total son coherentes. */
sunat_qr sunat_qr_parsear(const char *texto);
/* ¿Este texto se parece a un QR SUNAT? (para descartar QRs de pago Yape/Plin, URLs genéricas) */
bool sunat_qr_parece(const char *texto);
/* Vuelca el QR en el comprobante con origen QR */
void sunat_qr_aplicar(const sunat_qr *q, ocrx_comprobante *c);

#endif

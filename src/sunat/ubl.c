#include "ubl.h"
#include "sunat.h"
#include "../core/str.h"
#include <expat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define MAX_PROF 32
#define MAX_TXT 2048

typedef struct {
    ocrx_comprobante *c;
    arena *a;
    const char *pila[MAX_PROF];   /* nombres locales */
    int prof;
    char texto[MAX_TXT]; size_t ntexto;
    char attr_scheme[32];         /* schemeID del elemento actual */
    char attr_locale[16];         /* languageLocaleID */
    char attr_unit[16];           /* unitCode */
    char attr_currency[8];
    char attr_listid[16];
    /* estado de contexto */
    int  en_supplier, en_customer, en_line, en_taxsubtotal, en_pricing_ref, en_allowance, en_payment_terms, en_billing_ref, en_discrepancy, en_delivery, en_seller_supplier, en_signature;
    int  cat_tax;                 /* código de TaxScheme dentro de TaxSubtotal (1000 IGV, 9997 EXO, 9998 INA, 9995 EXP, 2000 ISC, 7152 ICBPER, 9996 GRA) */
    double sub_taxable, sub_tax; char sub_scheme[8]; char sub_exemption[4]; double sub_percent;
    int  tax_total_depth;         /* profundidad del TaxTotal actual (documento vs línea) */
    ocrx_item *item;
    size_t cap_items;
    char allowance_indicator[8]; double allowance_amount; char allowance_reason[4];
    char pt_id[32]; char pt_means[16]; double pt_amount, pt_percent;
    bool raiz_ok;
    char raiz[32];
    double doc_tax_amount;        /* TaxTotal/TaxAmount del documento (respaldo si no hay subtotales) */
} ublctx;

static const char *local(const char *nombre) {
    const char *sep = strrchr(nombre, '|');
    return sep ? sep + 1 : nombre;
}
static bool padre_es(ublctx *x, int n, const char *nombre) { return x->prof > n && strcmp(x->pila[x->prof - 1 - n], nombre) == 0; }
static bool ruta_tiene(ublctx *x, const char *nombre) { for (int i = 0; i < x->prof; i++) if (!strcmp(x->pila[i], nombre)) return true; return false; }

static void set_meta(ocrx_campo_meta *m) { m->origen = OCRX_ORIGEN_XML; m->confianza = 1.0f; m->presente = true; }

static void item_nuevo(ublctx *x) {
    ocrx_comprobante *c = x->c;
    if (c->n_items + 1 > x->cap_items) {
        size_t nc = x->cap_items ? x->cap_items * 2 : 16;
        c->items = (ocrx_item *)arena_realloc(x->a, c->items, x->cap_items * sizeof(ocrx_item), nc * sizeof(ocrx_item));
        x->cap_items = nc;
    }
    x->item = &c->items[c->n_items++];
    memset(x->item, 0, sizeof *x->item);
    set_meta(&x->item->meta);
}

static void inicio(void *ud, const XML_Char *nombre, const XML_Char **atts) {
    ublctx *x = (ublctx *)ud;
    const char *l = local(nombre);
    if (x->prof < MAX_PROF) x->pila[x->prof] = l;
    x->prof++;
    x->ntexto = 0; x->texto[0] = 0;
    x->attr_scheme[0] = x->attr_locale[0] = x->attr_unit[0] = x->attr_currency[0] = x->attr_listid[0] = 0;
    for (int i = 0; atts[i]; i += 2) {
        const char *an = local(atts[i]);
        if (!strcmp(an, "schemeID")) str_copiar(x->attr_scheme, sizeof x->attr_scheme, atts[i + 1]);
        else if (!strcmp(an, "languageLocaleID")) str_copiar(x->attr_locale, sizeof x->attr_locale, atts[i + 1]);
        else if (!strcmp(an, "unitCode")) str_copiar(x->attr_unit, sizeof x->attr_unit, atts[i + 1]);
        else if (!strcmp(an, "currencyID")) str_copiar(x->attr_currency, sizeof x->attr_currency, atts[i + 1]);
        else if (!strcmp(an, "listID")) str_copiar(x->attr_listid, sizeof x->attr_listid, atts[i + 1]);
    }
    if (x->prof == 1) {
        str_copiar(x->raiz, sizeof x->raiz, l);
        if (!strcmp(l, "Invoice")) { x->raiz_ok = true; }
        else if (!strcmp(l, "CreditNote")) { x->raiz_ok = true; x->c->tipo = OCRX_CPE_NOTA_CREDITO; set_meta(&x->c->m_tipo); }
        else if (!strcmp(l, "DebitNote")) { x->raiz_ok = true; x->c->tipo = OCRX_CPE_NOTA_DEBITO; set_meta(&x->c->m_tipo); }
        else if (!strcmp(l, "DespatchAdvice")) { x->raiz_ok = true; x->c->tipo = OCRX_CPE_GUIA_REMITENTE; set_meta(&x->c->m_tipo); }
        else if (!strcmp(l, "Retention")) { x->raiz_ok = true; x->c->tipo = OCRX_CPE_RETENCION; set_meta(&x->c->m_tipo); }
        else if (!strcmp(l, "Perception")) { x->raiz_ok = true; x->c->tipo = OCRX_CPE_PERCEPCION; set_meta(&x->c->m_tipo); }
        else if (!strcmp(l, "SummaryDocuments") || !strcmp(l, "VoidedDocuments")) { x->raiz_ok = false; }
        if (x->raiz_ok) { x->c->tenia_xml = true; x->c->es_electronico = true; }
    }
    if (!strcmp(l, "AccountingSupplierParty") || !strcmp(l, "AgentParty") || !strcmp(l, "DespatchSupplierParty")) x->en_supplier++;
    else if (!strcmp(l, "AccountingCustomerParty") || !strcmp(l, "ReceiverParty") || !strcmp(l, "DeliveryCustomerParty")) x->en_customer++;
    else if (!strcmp(l, "SellerSupplierParty")) x->en_seller_supplier++;
    else if (!strcmp(l, "InvoiceLine") || !strcmp(l, "CreditNoteLine") || !strcmp(l, "DebitNoteLine") || !strcmp(l, "DespatchLine")) { x->en_line++; item_nuevo(x); }
    else if (!strcmp(l, "TaxSubtotal")) { x->en_taxsubtotal++; x->sub_taxable = x->sub_tax = 0; x->sub_scheme[0] = 0; x->sub_exemption[0] = 0; x->sub_percent = 0; }
    else if (!strcmp(l, "PricingReference")) x->en_pricing_ref++;
    else if (!strcmp(l, "AllowanceCharge")) { x->en_allowance++; x->allowance_indicator[0] = 0; x->allowance_amount = 0; x->allowance_reason[0] = 0; }
    else if (!strcmp(l, "PaymentTerms")) { x->en_payment_terms++; x->pt_id[0] = x->pt_means[0] = 0; x->pt_amount = x->pt_percent = 0; }
    else if (!strcmp(l, "BillingReference")) x->en_billing_ref++;
    else if (!strcmp(l, "DiscrepancyResponse")) x->en_discrepancy++;
    else if (!strcmp(l, "Delivery")) x->en_delivery++;
    else if (!strcmp(l, "Signature")) x->en_signature++;
}

static void texto(void *ud, const XML_Char *s, int n) {
    ublctx *x = (ublctx *)ud;
    size_t k = (size_t)n;
    if (x->ntexto + k >= MAX_TXT) k = MAX_TXT - 1 - x->ntexto;
    if (k > 0) { memcpy(x->texto + x->ntexto, s, k); x->ntexto += k; x->texto[x->ntexto] = 0; }
}

static double num(const char *s) { return atof(s); }

static void aplicar_subtotal(ublctx *x) {
    ocrx_totales *t = &x->c->totales;
    int cod = atoi(x->sub_scheme);
    if (x->en_line) {
        if (!x->item) return;
        if (cod == 1000) { x->item->igv = x->sub_tax; if (x->sub_percent > 0 && t->tasa_igv == 0) t->tasa_igv = x->sub_percent / 100.0; }
        if (x->sub_exemption[0]) str_copiar(x->item->afectacion, 4, x->sub_exemption);
        return;
    }
    switch (cod) {
    case 1000: t->gravada += x->sub_taxable; t->igv += x->sub_tax; set_meta(&t->m_gravada); set_meta(&t->m_igv); if (x->sub_percent > 0) t->tasa_igv = x->sub_percent / 100.0; break;
    case 9997: t->exonerada += x->sub_taxable; set_meta(&t->m_exonerada); break;
    case 9998: t->inafecta += x->sub_taxable; set_meta(&t->m_inafecta); break;
    case 9995: t->exportacion += x->sub_taxable; break;
    case 9996: t->gratuita += x->sub_taxable; break;
    case 2000: t->isc += x->sub_tax; break;
    case 7152: t->icbper += x->sub_tax; break;
    default: t->otros_tributos += x->sub_tax; break;
    }
}

static void fin(void *ud, const XML_Char *nombre) {
    ublctx *x = (ublctx *)ud;
    const char *l = local(nombre);
    ocrx_comprobante *c = x->c;
    char v[MAX_TXT]; str_copiar(v, sizeof v, x->texto); str_recortar(v);
    bool en_ext = ruta_tiene(x, "UBLExtensions");

    if (!en_ext && !x->en_signature) {
        /* ---- Cabecera ---- */
        if (x->prof == 2) {
            if (!strcmp(l, "ID")) {
                char ser[8], nm[12]; bool corr;
                if (sunat_serie_numero_parsear(v, c->tipo, ser, nm, &corr)) { str_copiar(c->serie, 8, ser); str_copiar(c->numero, 12, nm); set_meta(&c->m_serie); set_meta(&c->m_numero); }
                else { str_copiar(c->serie, 8, v); }
            } else if (!strcmp(l, "IssueDate")) { char f[11]; if (sunat_fecha_parsear(v, f)) { str_copiar(c->fecha_emision, 11, f); set_meta(&c->m_fecha); } }
            else if (!strcmp(l, "IssueTime")) { char h[9]; if (sunat_hora_parsear(v, h)) str_copiar(c->hora_emision, 9, h); }
            else if (!strcmp(l, "DueDate")) { char f[11]; if (sunat_fecha_parsear(v, f)) str_copiar(c->fecha_vencimiento, 11, f); }
            else if (!strcmp(l, "InvoiceTypeCode") || !strcmp(l, "CreditNoteTypeCode") || !strcmp(l, "DebitNoteTypeCode") || !strcmp(l, "DespatchAdviceTypeCode")) {
                ocrx_tipo_cpe t = sunat_tipo_desde_codigo(v);
                if (t != OCRX_CPE_DESCONOCIDO && c->tipo == OCRX_CPE_DESCONOCIDO) { c->tipo = t; set_meta(&c->m_tipo); }
                /* listID 0101 venta interna, 1001 detracción, 0200 exportación... */
            }
            else if (!strcmp(l, "DocumentCurrencyCode")) { c->moneda = sunat_moneda_desde_texto(v); set_meta(&c->m_moneda); }
            else if (!strcmp(l, "Note")) {
                if (!strcmp(x->attr_locale, "1000") || sunat_linea_es_monto_en_letras(v)) str_copiar(c->totales.monto_en_letras, OCRX_MAX_LEN, v);
                else if (!strcmp(x->attr_locale, "2006")) { /* operación sujeta a detracción */ }
                else if (!strcmp(x->attr_locale, "2000")) { /* comprobante de percepción */ }
                else if (!c->observaciones[0]) str_copiar(c->observaciones, OCRX_MAX_LEN, v);
            }
            else if (!strcmp(l, "UBLVersionID") || !strcmp(l, "CustomizationID")) {}
        }
        /* OrderReference (orden de compra) */
        if (!strcmp(l, "ID") && padre_es(x, 1, "OrderReference") && x->prof == 3) str_copiar(c->orden_compra, 32, v);
        /* Guía relacionada */
        if (!strcmp(l, "ID") && padre_es(x, 1, "DespatchDocumentReference") && x->prof == 3) str_copiar(c->guia_relacionada, 24, v);
        /* Tipo de cambio */
        if (!strcmp(l, "CalculationRate") && ruta_tiene(x, "PaymentExchangeRate")) c->tipo_cambio = num(v);

        /* ---- Partes ---- */
        ocrx_parte *p = NULL;
        if (x->en_supplier && !x->en_line) p = &c->emisor;
        else if (x->en_customer && !x->en_line) p = &c->cliente;
        if (p) {
            if (!strcmp(l, "ID") && (padre_es(x, 1, "PartyIdentification") || padre_es(x, 1, "CompanyID"))) {
                ocrx_tipo_doc_id td = x->attr_scheme[0] ? sunat_tipo_doc_desde_codigo(x->attr_scheme) : sunat_tipo_doc_por_forma(v);
                char d[32]; str_normalizar(v, d, sizeof d, true);
                p->tipo_doc = td; str_copiar(p->num_doc, 32, d);
                if (td == OCRX_DOC_RUC || sunat_ruc_valido(d)) { str_copiar(p->ruc, 12, d); p->tipo_doc = OCRX_DOC_RUC; set_meta(&p->m_ruc); }
            } else if (!strcmp(l, "CompanyID") && padre_es(x, 1, "PartyTaxScheme")) {
                if (!p->ruc[0] && sunat_ruc_valido(v)) { str_copiar(p->ruc, 12, v); p->tipo_doc = OCRX_DOC_RUC; str_copiar(p->num_doc, 32, v); set_meta(&p->m_ruc); }
            } else if (!strcmp(l, "CustomerAssignedAccountID") || (!strcmp(l, "AdditionalAccountID") && false)) {
                if (!p->ruc[0] && sunat_ruc_valido(v)) { str_copiar(p->ruc, 12, v); p->tipo_doc = OCRX_DOC_RUC; str_copiar(p->num_doc, 32, v); set_meta(&p->m_ruc); }
            } else if (!strcmp(l, "RegistrationName")) { if (!p->razon_social[0] || padre_es(x, 1, "PartyLegalEntity")) { str_copiar(p->razon_social, OCRX_MAX_LEN, v); set_meta(&p->m_razon_social); } }
            else if (!strcmp(l, "Name") && padre_es(x, 1, "PartyName")) str_copiar(p->nombre_comercial, OCRX_MAX_LEN, v);
            else if (!strcmp(l, "Name") && padre_es(x, 1, "Party") && !p->razon_social[0]) str_copiar(p->razon_social, OCRX_MAX_LEN, v);
            else if ((!strcmp(l, "Line") && padre_es(x, 1, "AddressLine")) || (!strcmp(l, "StreetName") && !p->direccion[0])) {
                if (padre_es(x, 1, "AddressLine") || !p->direccion[0]) { str_copiar(p->direccion, OCRX_MAX_LEN, v); set_meta(&p->m_direccion); }
            }
            else if (!strcmp(l, "ID") && (padre_es(x, 1, "RegistrationAddress") || padre_es(x, 1, "PostalAddress")) && strlen(v) == 6) str_copiar(p->ubigeo, 8, v);
            else if ((!strcmp(l, "CityName") || !strcmp(l, "CountrySubentity") || !strcmp(l, "District")) && p->direccion[0] && strlen(p->direccion) + strlen(v) + 3 < OCRX_MAX_LEN && !str_contiene(p->direccion, v)) {
                strcat(p->direccion, " - "); strcat(p->direccion, v);
            }
        }

        /* ---- Impuestos ---- */
        if (x->en_taxsubtotal) {
            if (!strcmp(l, "TaxableAmount")) x->sub_taxable = num(v);
            else if (!strcmp(l, "TaxAmount") && padre_es(x, 1, "TaxSubtotal")) x->sub_tax = num(v);
            else if (!strcmp(l, "ID") && padre_es(x, 1, "TaxScheme")) str_copiar(x->sub_scheme, 8, v);
            else if (!strcmp(l, "TaxExemptionReasonCode")) str_copiar(x->sub_exemption, 4, v);
            else if (!strcmp(l, "Percent")) x->sub_percent = num(v);
            else if (!strcmp(l, "TaxSubtotal")) { aplicar_subtotal(x); }
        } else if (!strcmp(l, "TaxAmount") && padre_es(x, 1, "TaxTotal") && !x->en_line && x->prof == 3) {
            x->doc_tax_amount += num(v);   /* respaldo: se usa al final si no hubo subtotales */
        }

        /* ---- Totales ---- */
        if (ruta_tiene(x, "LegalMonetaryTotal") || ruta_tiene(x, "RequestedMonetaryTotal")) {
            ocrx_totales *t = &c->totales;
            if (!strcmp(l, "LineExtensionAmount")) { /* suma de valores de venta (sin IGV) — referencia */ }
            else if (!strcmp(l, "TaxInclusiveAmount")) { if (t->total == 0) t->total = num(v); }
            else if (!strcmp(l, "PayableAmount")) { t->total = num(v); set_meta(&t->m_total); }
            else if (!strcmp(l, "AllowanceTotalAmount")) t->descuentos = num(v);
            else if (!strcmp(l, "ChargeTotalAmount")) t->otros_cargos = num(v);
            else if (!strcmp(l, "PrepaidAmount")) { /* anticipos */ }
            else if (!strcmp(l, "PayableRoundingAmount")) t->otros_cargos += num(v);
        }

        /* ---- Descuentos/cargos globales ---- */
        if (x->en_allowance && !x->en_line) {
            if (!strcmp(l, "ChargeIndicator")) str_copiar(x->allowance_indicator, 8, v);
            else if (!strcmp(l, "AllowanceChargeReasonCode")) str_copiar(x->allowance_reason, 4, v);
            else if (!strcmp(l, "Amount")) x->allowance_amount = num(v);
            else if (!strcmp(l, "AllowanceCharge")) {
                /* 02 descuento global afecta base; 03 no afecta; 62 percepción ... Los totales de LegalMonetaryTotal ya los traen; sólo registrar si faltan */
                (void)x->allowance_amount;
            }
        }

        /* ---- Detracción / forma de pago / retención ---- */
        if (x->en_payment_terms && !x->en_line) {
            if (!strcmp(l, "ID")) str_copiar(x->pt_id, 32, v);
            else if (!strcmp(l, "PaymentMeansID")) str_copiar(x->pt_means, 16, v);
            else if (!strcmp(l, "PaymentPercent")) x->pt_percent = num(v);
            else if (!strcmp(l, "Amount")) x->pt_amount = num(v);
            else if (!strcmp(l, "PaymentDueDate")) { char f[11]; if (sunat_fecha_parsear(v, f) && !c->fecha_vencimiento[0]) str_copiar(c->fecha_vencimiento, 11, f); }
            else if (!strcmp(l, "PaymentTerms")) {
                char id[32]; str_normalizar(x->pt_id, id, sizeof id, true);
                if (str_contiene(id, "DETRACCION")) { c->totales.detraccion_pct = x->pt_percent; c->totales.detraccion_monto = x->pt_amount; str_copiar(c->totales.detraccion_codigo, 4, x->pt_means); }
                else if (str_contiene(id, "FORMAPAGO") || str_contiene(id, "FORMA")) { if (!c->forma_pago[0] || str_contiene(x->pt_means, "CREDITO")) str_copiar(c->forma_pago, 32, x->pt_means); }
                else if (str_contiene(id, "RETENCION")) c->totales.retencion_monto = x->pt_amount;
                else if (str_contiene(id, "PERCEPCION")) c->totales.percepcion_monto = x->pt_amount;
            }
        }
        if (!strcmp(l, "ID") && padre_es(x, 1, "PayeeFinancialAccount")) str_copiar(c->totales.detraccion_cuenta, 24, v);

        /* ---- Referencia (NC/ND) ---- */
        if (x->en_billing_ref && !strcmp(l, "ID") && padre_es(x, 1, "InvoiceDocumentReference")) {
            char ser[8], nm[12]; bool corr;
            if (sunat_serie_numero_parsear(v, OCRX_CPE_DESCONOCIDO, ser, nm, &corr)) { str_copiar(c->referencia.serie, 8, ser); str_copiar(c->referencia.numero, 12, nm); }
        }
        if (x->en_billing_ref && !strcmp(l, "DocumentTypeCode")) c->referencia.tipo = sunat_tipo_desde_codigo(v);
        if (x->en_discrepancy) {
            if (!strcmp(l, "ResponseCode")) str_copiar(c->referencia.motivo_codigo, 4, v);
            else if (!strcmp(l, "Description")) str_copiar(c->referencia.motivo, OCRX_MAX_LEN, v);
            else if (!strcmp(l, "ReferenceID") && !c->referencia.serie[0]) {
                char ser[8], nm[12]; bool corr;
                if (sunat_serie_numero_parsear(v, OCRX_CPE_DESCONOCIDO, ser, nm, &corr)) { str_copiar(c->referencia.serie, 8, ser); str_copiar(c->referencia.numero, 12, nm); }
            }
        }

        /* ---- Ítems ---- */
        if (x->en_line && x->item) {
            ocrx_item *it = x->item;
            if (!strcmp(l, "InvoicedQuantity") || !strcmp(l, "CreditedQuantity") || !strcmp(l, "DebitedQuantity") || !strcmp(l, "DeliveredQuantity")) {
                it->cantidad = num(v); str_copiar(it->unidad, 8, sunat_unidad_normalizar(x->attr_unit));
            } else if (!strcmp(l, "LineExtensionAmount") && (padre_es(x, 1, "CreditNoteLine") || padre_es(x, 1, "DebitNoteLine") || padre_es(x, 1, "InvoiceLine"))) it->valor_venta = num(v);
            else if (!strcmp(l, "Description") && padre_es(x, 1, "Item")) {
                if (it->descripcion[0] && strlen(it->descripcion) + strlen(v) + 2 < OCRX_MAX_LEN) { strcat(it->descripcion, " "); strcat(it->descripcion, v); }
                else if (!it->descripcion[0]) str_copiar(it->descripcion, OCRX_MAX_LEN, v);
            }
            else if (!strcmp(l, "ID") && padre_es(x, 1, "SellersItemIdentification")) str_copiar(it->codigo, 32, v);
            else if (!strcmp(l, "PriceAmount") && padre_es(x, 1, "Price") && !x->en_pricing_ref) it->precio_unitario = num(v);
            else if (!strcmp(l, "PriceAmount") && x->en_pricing_ref) it->precio_unitario_con_igv = num(v);
            else if (!strcmp(l, "TaxAmount") && padre_es(x, 1, "TaxTotal")) { if (it->igv == 0) it->igv = num(v); }
        }
    }
    if (!strcmp(l, "DigestValue") && !c->hash_cpe[0]) str_copiar(c->hash_cpe, 64, v);

    /* salir de contextos */
    if (!strcmp(l, "AccountingSupplierParty") || !strcmp(l, "AgentParty") || !strcmp(l, "DespatchSupplierParty")) x->en_supplier--;
    else if (!strcmp(l, "AccountingCustomerParty") || !strcmp(l, "ReceiverParty") || !strcmp(l, "DeliveryCustomerParty")) x->en_customer--;
    else if (!strcmp(l, "SellerSupplierParty")) x->en_seller_supplier--;
    else if (!strcmp(l, "InvoiceLine") || !strcmp(l, "CreditNoteLine") || !strcmp(l, "DebitNoteLine") || !strcmp(l, "DespatchLine")) {
        if (x->item) {
            ocrx_item *it = x->item;
            if (it->importe == 0) it->importe = it->valor_venta + it->igv;
            if (it->precio_unitario_con_igv == 0 && it->cantidad > 0) it->precio_unitario_con_igv = it->importe / it->cantidad;
            if (!it->afectacion[0]) str_copiar(it->afectacion, 4, it->igv > 0 ? "10" : "20");
        }
        x->en_line--; x->item = NULL;
    }
    else if (!strcmp(l, "TaxSubtotal")) x->en_taxsubtotal--;
    else if (!strcmp(l, "PricingReference")) x->en_pricing_ref--;
    else if (!strcmp(l, "AllowanceCharge")) x->en_allowance--;
    else if (!strcmp(l, "PaymentTerms")) x->en_payment_terms--;
    else if (!strcmp(l, "BillingReference")) x->en_billing_ref--;
    else if (!strcmp(l, "DiscrepancyResponse")) x->en_discrepancy--;
    else if (!strcmp(l, "Delivery")) x->en_delivery--;
    else if (!strcmp(l, "Signature")) x->en_signature--;

    x->prof--;
    x->ntexto = 0; x->texto[0] = 0;
}

bool ubl_parece(const void *datos, size_t n) {
    const char *s = (const char *)datos;
    size_t lim = n < 4096 ? n : 4096;
    char cab[4097]; memcpy(cab, s, lim); cab[lim] = 0;
    /* BOM UTF-8 */
    const char *p = cab; if ((unsigned char)p[0] == 0xEF) p += 3;
    while (*p && (unsigned char)*p <= ' ') p++;
    if (*p != '<') return false;
    return str_contiene(cab, "Invoice") || str_contiene(cab, "CreditNote") || str_contiene(cab, "DebitNote") || str_contiene(cab, "DespatchAdvice") || str_contiene(cab, "Retention") || str_contiene(cab, "Perception") || str_contiene(cab, "urn:oasis:names:specification:ubl");
}

ocrx_error ubl_leer(const char *xml, size_t n, ocrx_comprobante *c, arena *a) {
    if (!xml || !n || !c) return OCRX_E_PARAM;
    XML_Parser p = XML_ParserCreateNS(NULL, '|');
    if (!p) return OCRX_E_MEMORIA;
    ublctx x; memset(&x, 0, sizeof x); x.c = c; x.a = a;
    XML_SetUserData(p, &x);
    XML_SetElementHandler(p, inicio, fin);
    XML_SetCharacterDataHandler(p, texto);
    /* saltar BOM */
    if (n >= 3 && (unsigned char)xml[0] == 0xEF && (unsigned char)xml[1] == 0xBB && (unsigned char)xml[2] == 0xBF) { xml += 3; n -= 3; }
    enum XML_Status st = XML_Parse(p, xml, (int)n, 1);
    XML_ParserFree(p);
    if (st == XML_STATUS_ERROR && !x.raiz_ok) return OCRX_E_XML;
    if (!x.raiz_ok) return OCRX_E_XML;
    /* completar */
    if (c->totales.igv == 0 && !c->totales.m_igv.presente && x.doc_tax_amount > 0) { c->totales.igv = x.doc_tax_amount; set_meta(&c->totales.m_igv); }
    if (c->tipo == OCRX_CPE_DESCONOCIDO) c->tipo = sunat_tipo_desde_serie(c->serie, OCRX_CPE_DESCONOCIDO);
    if (c->moneda == OCRX_MON_DESCONOCIDA) c->moneda = OCRX_MON_PEN;
    if (c->totales.total == 0 && c->n_items) { for (size_t i = 0; i < c->n_items; i++) c->totales.total += c->items[i].importe; }
    sunat_cuadrar(&c->totales);
    if (!c->totales.cuadra && (c->totales.gravada + c->totales.exonerada + c->totales.inafecta) > 0) {
        /* el XML manda; dejar constancia pero no marcar revisión por redondeo de hasta 0.05 */
        double calc = c->totales.gravada + c->totales.exonerada + c->totales.inafecta + c->totales.exportacion + c->totales.igv + c->totales.isc + c->totales.icbper + c->totales.otros_tributos + c->totales.otros_cargos - c->totales.descuentos;
        if (fabs(calc - c->totales.total) <= 0.05) c->totales.cuadra = true;
    }
    return OCRX_OK;
}

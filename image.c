#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "image.h"

/* ------------------------------------------------------------------
 * Форматы image.h:
 *   0 — RGB888   (по 32 бита на пиксель)
 *   1 — ARGB8888 (по 32 бита на пиксель)
 *   2 — RGB444   (по 12 бит на пиксель, вплотную)
 *   3 — ARGB4444 (по 16 бит на пиксель)
 * ------------------------------------------------------------------ */

static int data_size(unsigned char format, int n) {
    switch (format) {
        case 0: return n * 4;
        case 1: return n * 4;
        case 2: return (n * 12 + 7) / 8;
        case 3: return n * 2;
    }
    return 0;
}

/* Сырой (упакованный) пиксель i. */
static int raw_pixel(const Image *img, int i) {
    int total = (img->x * img->y * 12 + 7) / 8;
    switch (img->format) {
        case 0: case 1: return ((int*)img->data)[i];
        case 2: {
            const uint8_t *p = (const uint8_t*)img->data;
            int bit = i * 12;
            int byte = bit / 8;
            int shift = bit % 8;
            /* не читать за конец буфера на последнем пикселе */
            uint32_t v = p[byte];
            if (byte + 1 < total) v |= (uint32_t)p[byte + 1] << 8;
            if (byte + 2 < total) v |= (uint32_t)p[byte + 2] << 16;
            return (v >> shift) & 0x0FFF;
        }
        case 3: return ((unsigned short*)img->data)[i];
    }
    return 0;
}

static void unpack_rgb888(int px, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (px >> 16) & 0xFF; *g = (px >> 8) & 0xFF; *b = px & 0xFF;
}

static void unpack_argb888(int px, uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b) {
    *a = (px >> 24) & 0xFF; *r = (px >> 16) & 0xFF; *g = (px >> 8) & 0xFF; *b = px & 0xFF;
}

static void unpack_rgb444(int px, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = ((px >> 8) & 0x0F) * 17; *g = ((px >> 4) & 0x0F) * 17; *b = (px & 0x0F) * 17;
}

static void unpack_argb4444(int px, uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b) {
    *a = ((px >> 12) & 0x0F) * 17; *r = ((px >> 8) & 0x0F) * 17;
    *g = ((px >> 4) & 0x0F) * 17; *b = (px & 0x0F) * 17;
}

int image_load(const char *filename, Image *img) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    if (fread(&img->x, sizeof(short), 1, f) != 1) { fclose(f); return 0; }
    if (fread(&img->y, sizeof(short), 1, f) != 1) { fclose(f); return 0; }
    if (fread(&img->format, sizeof(unsigned char), 1, f) != 1) { fclose(f); return 0; }

    int n = img->x * img->y;
    int sz = data_size(img->format, n);
    img->data = malloc((size_t)sz);
    if (!img->data) { fclose(f); return 0; }

    if (fread(img->data, 1, (size_t)sz, f) != (size_t)sz) {
        free(img->data); img->data = NULL; fclose(f); return 0;
    }
    fclose(f);
    return 1;
}

void image_free(Image *img) {
    free(img->data);
    img->data = NULL;
}

void image_get_pixel(const Image *img, int i,
                     uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    int px = raw_pixel(img, i);
    *r = *g = *b = 0;
    *a = 255;
    switch (img->format) {
        case 0: unpack_rgb888(px, r, g, b); break;
        case 1: unpack_argb888(px, a, r, g, b); break;
        case 2: unpack_rgb444(px, r, g, b); break;
        case 3: unpack_argb4444(px, a, r, g, b); break;
        default: break;
    }
}

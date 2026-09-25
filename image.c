#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "image.h"

/* Размер упакованных пикселей в байтах; 0 — неизвестный формат. */
static size_t data_size(unsigned char format, size_t n) {
    switch (format) {
        case IMAGE_RGB888:
        case IMAGE_ARGB8888: return n * 4;
        case IMAGE_RGB444:   return (n * 12 + 7) / 8;
        case IMAGE_ARGB4444: return n * 2;
        default:             return 0;
    }
}

static size_t pixel_count(const Image *img) {
    return (size_t)img->x * (size_t)img->y;
}

/* Сырой (упакованный) пиксель i. */
static uint32_t raw_pixel(const Image *img, int i) {
    switch (img->format) {
        case IMAGE_RGB888:
        case IMAGE_ARGB8888:
            return ((const uint32_t *)img->data)[i];

        case IMAGE_RGB444: {
            const uint8_t *p = (const uint8_t *)img->data;
            const size_t total = data_size(IMAGE_RGB444, pixel_count(img));
            const size_t bit = (size_t)i * 12;
            const size_t byte = bit / 8;
            const unsigned shift = (unsigned)(bit % 8);

            /* не читать за конец буфера на последнем пикселе */
            uint32_t v = p[byte];
            if (byte + 1 < total) v |= (uint32_t)p[byte + 1] << 8;
            if (byte + 2 < total) v |= (uint32_t)p[byte + 2] << 16;
            return (v >> shift) & 0x0FFFu;
        }

        case IMAGE_ARGB4444:
            return ((const uint16_t *)img->data)[i];

        default:
            return 0;
    }
}

/* Канал шириной 8 бит, начиная с бита shift. */
static uint8_t channel8(uint32_t px, unsigned shift) {
    return (uint8_t)((px >> shift) & 0xFFu);
}

/* Канал шириной 4 бита, растянутый до 8 бит (0xF -> 0xFF). */
static uint8_t channel4(uint32_t px, unsigned shift) {
    return (uint8_t)(((px >> shift) & 0x0Fu) * 17u);
}

int image_load(const char *filename, Image *img) {
    img->data = NULL;

    FILE *f = fopen(filename, "rb");
    if (!f) return 0;

    int ok = fread(&img->x, sizeof(img->x), 1, f) == 1
          && fread(&img->y, sizeof(img->y), 1, f) == 1
          && fread(&img->format, sizeof(img->format), 1, f) == 1
          && img->x > 0 && img->y > 0;

    const size_t size = ok ? data_size(img->format, pixel_count(img)) : 0;
    ok = ok && size > 0;

    if (ok) {
        img->data = malloc(size);
        ok = img->data && fread(img->data, 1, size, f) == size;
    }
    fclose(f);

    if (!ok) image_free(img);
    return ok;
}

void image_free(Image *img) {
    free(img->data);
    img->data = NULL;
}

int image_has_alpha(const Image *img) {
    return img->format == IMAGE_ARGB8888 || img->format == IMAGE_ARGB4444;
}

void image_get_pixel(const Image *img, int i,
                     uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    const uint32_t px = raw_pixel(img, i);

    *r = *g = *b = 0;
    *a = 255;

    switch (img->format) {
        case IMAGE_ARGB8888:
            *a = channel8(px, 24);
            /* fallthrough */
        case IMAGE_RGB888:
            *r = channel8(px, 16); *g = channel8(px, 8); *b = channel8(px, 0);
            break;

        case IMAGE_ARGB4444:
            *a = channel4(px, 12);
            /* fallthrough */
        case IMAGE_RGB444:
            *r = channel4(px, 8); *g = channel4(px, 4); *b = channel4(px, 0);
            break;

        default:
            break;
    }
}

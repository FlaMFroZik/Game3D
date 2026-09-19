#ifndef IMAGE_H
#define IMAGE_H

/* ------------------------------------------------------------------
 * Raw-файлы изображений (.raw-текстуры).
 * Заголовок: short x, short y, unsigned char format; затем пиксели.
 * Форматы:
 *   0 — RGB888   (по 32 бита на пиксель)
 *   1 — ARGB8888 (по 32 бита на пиксель)
 *   2 — RGB444   (по 12 бит на пиксель, вплотную)
 *   3 — ARGB4444 (по 16 бит на пиксель)
 * ------------------------------------------------------------------ */

#include <stdint.h>

typedef struct {
    short x, y;
    unsigned char format;
    void *data; /* int* или unsigned short* */
} Image;

/* Загружает .raw-файл в img. 1 — успех, 0 — ошибка (файл или память). */
int image_load(const char *filename, Image *img);

/* Освобождает пиксели, загруженные image_load. */
void image_free(Image *img);

/* Пиксель i (строки слева-направо, сверху-вниз) в 8-битных каналах;
 * для форматов без альфа-канала *a ставится 255. */
void image_get_pixel(const Image *img, int i,
                     uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

#endif

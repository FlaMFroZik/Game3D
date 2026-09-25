#ifndef IMAGE_H
#define IMAGE_H

/* ------------------------------------------------------------------
 * Raw-файлы изображений (.raw-текстуры).
 * Заголовок: short x, short y, unsigned char format; затем пиксели.
 * Форматы — ImageFormat ниже.
 * ------------------------------------------------------------------ */

#include <stdint.h>

typedef enum {
    IMAGE_RGB888   = 0,  /* по 32 бита на пиксель, старший байт не используется */
    IMAGE_ARGB8888 = 1,  /* по 32 бита на пиксель */
    IMAGE_RGB444   = 2,  /* по 12 бит на пиксель, вплотную */
    IMAGE_ARGB4444 = 3   /* по 16 бит на пиксель */
} ImageFormat;

typedef struct {
    short x, y;
    unsigned char format; /* ImageFormat */
    void *data;           /* упакованные пиксели в формате format */
} Image;

/* Загружает .raw-файл в img. 1 — успех, 0 — ошибка (файл, формат,
 * размер или память). При ошибке img->data == NULL. */
int image_load(const char *filename, Image *img);

/* Освобождает пиксели, загруженные image_load. */
void image_free(Image *img);

/* 1, если у формата изображения есть альфа-канал. */
int image_has_alpha(const Image *img);

/* Пиксель i (строки слева-направо, сверху-вниз) в 8-битных каналах;
 * для форматов без альфа-канала *a ставится 255. */
void image_get_pixel(const Image *img, int i,
                     uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

#endif

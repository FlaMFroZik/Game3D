#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <GL/gl.h>

#include "image.h"
#include "render/prim.h"

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

static int get_pixel(const Image *img, int i) {
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

static int raw_load(const char *filename, Image *img) {
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

GLuint prim_load_texture(const char *filename) {
    Image img = {0};
    if (!raw_load(filename, &img)) {
        fprintf(stderr, "Error: cannot load texture file '%s'\n", filename);
        return 0;
    }
    printf("Loaded texture: %dx%d, format %d\n", img.x, img.y, img.format);

    int n = img.x * img.y;
    int has_alpha = (img.format == 1 || img.format == 3);

    unsigned char *pixels = malloc((size_t)n * (has_alpha ? 4 : 3));
    if (!pixels) {
        free(img.data);
        return 0;
    }

    for (int i = 0; i < n; i++) {
        int px = get_pixel(&img, i);
        uint8_t r = 0, g = 0, b = 0, a = 255;
        switch (img.format) {
            case 0: unpack_rgb888(px, &r, &g, &b); break;
            case 1: unpack_argb888(px, &a, &r, &g, &b); break;
            case 2: unpack_rgb444(px, &r, &g, &b); break;
            case 3: unpack_argb4444(px, &a, &r, &g, &b); break;
            default: break;
        }
        if (has_alpha) {
            pixels[i*4+0]=r; pixels[i*4+1]=g; pixels[i*4+2]=b; pixels[i*4+3]=a;
        } else {
            pixels[i*3+0]=r; pixels[i*3+1]=g; pixels[i*3+2]=b;
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (has_alpha) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.x, img.y, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, img.x, img.y, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, pixels);
    }

    free(pixels);
    free(img.data);
    return tex;
}

void prim_free_texture(GLuint tex) {
    if (tex != 0) {
        glDeleteTextures(1, &tex);
    }
}

void prim_draw_cell(const CellQuad *q) {
    glBegin(GL_TRIANGLES);

    glTexCoord2f(0, 0); glVertex3f(q->x0, q->y00, q->z0);
    glTexCoord2f(1, 0); glVertex3f(q->x1, q->y10, q->z0);
    glTexCoord2f(1, 1); glVertex3f(q->x1, q->y11, q->z1);

    glTexCoord2f(0, 0); glVertex3f(q->x0, q->y00, q->z0);
    glTexCoord2f(1, 1); glVertex3f(q->x1, q->y11, q->z1);
    glTexCoord2f(0, 1); glVertex3f(q->x0, q->y01, q->z1);

    glEnd();
}

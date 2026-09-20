#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <GL/gl.h>

#include "image.h"
#include "render/prim.h"

/* Число текселей, приходящееся на метр поверхности. Одно и то же по обеим
 * осям — в этом и смысл: тексель остаётся квадратным, а текстура занимает
 * в мире столько места, сколько позволяют её пропорции. */
static float texels_per_meter(const Texture *tex) {
    int big = (tex->width > tex->height) ? tex->width : tex->height;
    if (big <= 0) return 1.0f;
    return (float)big / PRIM_TEX_TILE_METERS;
}

float prim_tex_u(const Texture *tex, float meters) {
    if (tex->width <= 0) return 0.0f;
    return meters * texels_per_meter(tex) / (float)tex->width;
}

float prim_tex_v(const Texture *tex, float meters) {
    if (tex->height <= 0) return 0.0f;
    return meters * texels_per_meter(tex) / (float)tex->height;
}

Texture prim_load_texture(const char *filename) {
    Texture tex = {0, 0, 0};

    Image img = {0};
    if (!image_load(filename, &img)) {
        fprintf(stderr, "Error: cannot load texture file '%s'\n", filename);
        return tex;
    }

    const int w = img.x;
    const int h = img.y;
    if (w <= 0 || h <= 0) {
        fprintf(stderr, "Error: bad texture size %dx%d in '%s'\n", w, h, filename);
        image_free(&img);
        return tex;
    }
    printf("Loaded texture: %dx%d, format %d\n", w, h, img.format);

    const int n = w * h;
    const int has_alpha = (img.format == 1 || img.format == 3);

    unsigned char *pixels = malloc((size_t)n * (has_alpha ? 4 : 3));
    if (!pixels) {
        image_free(&img);
        return tex;
    }

    for (int i = 0; i < n; i++) {
        uint8_t r, g, b, a;
        image_get_pixel(&img, i, &r, &g, &b, &a);
        if (has_alpha) {
            pixels[i*4+0]=r; pixels[i*4+1]=g; pixels[i*4+2]=b; pixels[i*4+3]=a;
        } else {
            pixels[i*3+0]=r; pixels[i*3+1]=g; pixels[i*3+2]=b;
        }
    }
    image_free(&img);

    const GLint  internal = has_alpha ? GL_RGBA : GL_RGB;
    const GLenum format   = has_alpha ? GL_RGBA : GL_RGB;

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    /* Текстура повторяется по поверхности — это и есть «плитка» вместо
     * растягивания одной картинки на всю грань. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    /* RGB-строка не всегда кратна четырём байтам. Явно задаём выравнивание,
     * иначе драйверы Windows и Linux могут по-разному прочитать текстуру. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0,
                 format, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    free(pixels);

    tex.id = id;
    tex.width = w;
    tex.height = h;
    return tex;
}

void prim_free_texture(Texture *tex) {
    if (tex->id != 0) {
        glDeleteTextures(1, &tex->id);
    }
    tex->id = 0;
    tex->width = 0;
    tex->height = 0;
}

void prim_draw_cell(const Texture *tex, const CellQuad *q) {
    const float u0 = prim_tex_u(tex, q->x0);
    const float u1 = prim_tex_u(tex, q->x1);
    const float v0 = prim_tex_v(tex, q->z0);
    const float v1 = prim_tex_v(tex, q->z1);

    glBegin(GL_TRIANGLES);

    glTexCoord2f(u0, v0); glVertex3f(q->x0, q->y00, q->z0);
    glTexCoord2f(u1, v0); glVertex3f(q->x1, q->y10, q->z0);
    glTexCoord2f(u1, v1); glVertex3f(q->x1, q->y11, q->z1);

    glTexCoord2f(u0, v0); glVertex3f(q->x0, q->y00, q->z0);
    glTexCoord2f(u1, v1); glVertex3f(q->x1, q->y11, q->z1);
    glTexCoord2f(u0, v1); glVertex3f(q->x0, q->y01, q->z1);

    glEnd();
}

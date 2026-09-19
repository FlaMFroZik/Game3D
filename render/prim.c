#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <GL/gl.h>

#include "image.h"
#include "render/prim.h"

GLuint prim_load_texture(const char *filename) {
    Image img = {0};
    if (!image_load(filename, &img)) {
        fprintf(stderr, "Error: cannot load texture file '%s'\n", filename);
        return 0;
    }
    printf("Loaded texture: %dx%d, format %d\n", img.x, img.y, img.format);

    int n = img.x * img.y;
    int has_alpha = (img.format == 1 || img.format == 3);

    unsigned char *pixels = malloc((size_t)n * (has_alpha ? 4 : 3));
    if (!pixels) {
        image_free(&img);
        return 0;
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
    image_free(&img);
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

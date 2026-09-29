#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include <GL/gl.h>

#include "image.h"
#include "render/prim.h"

static float texels_per_meter(const Texture *tex, float tile_meters) {
    int big = (tex->width > tex->height) ? tex->width : tex->height;
    if (big <= 0) return 1.0f;
    if (!(tile_meters > 0.0f)) tile_meters = PRIM_TEX_TILE_METERS;
    return (float)big / tile_meters;
}

float prim_tex_u(const Texture *tex, float meters) {
    return prim_tex_u_tile(tex, meters, 0.0f);
}

float prim_tex_v(const Texture *tex, float meters) {
    return prim_tex_v_tile(tex, meters, 0.0f);
}

float prim_tex_u_tile(const Texture *tex, float meters, float tile_meters) {
    if (tex->width <= 0) return 0.0f;
    return meters * texels_per_meter(tex, tile_meters) / (float)tex->width;
}

float prim_tex_v_tile(const Texture *tex, float meters, float tile_meters) {
    if (tex->height <= 0) return 0.0f;
    return meters * texels_per_meter(tex, tile_meters) / (float)tex->height;
}

static unsigned char *unpack_pixels(const Image *img, int has_alpha) {
    const int n = img->x * img->y;
    const int channels = has_alpha ? 4 : 3;

    unsigned char *pixels = malloc((size_t)n * (size_t)channels);
    if (!pixels) return NULL;

    for (int i = 0; i < n; i++) {
        uint8_t rgba[4];
        image_get_pixel(img, i, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
        memcpy(&pixels[(size_t)i * (size_t)channels], rgba, (size_t)channels);
    }
    return pixels;
}

static Texture upload_rgb_pixels(const unsigned char *pixels, int w, int h) {
    Texture tex = {0, w, h};
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    tex.id = id;
    return tex;
}

Texture prim_load_texture(const char *filename) {
    Texture tex = {0, 0, 0};

    /* Проверяем, не является ли имя одной из встроенных текстур */
    const Texture *builtin = prim_find_builtin(filename);
    if (builtin) {
        return *builtin;
    }

    Image img = {0};
    if (!image_load(filename, &img)) {
        return tex;
    }

    const int w = img.x;
    const int h = img.y;
    const int has_alpha = image_has_alpha(&img);
    unsigned char *pixels = unpack_pixels(&img, has_alpha);
    image_free(&img);
    if (!pixels) {
        return tex;
    }

    const GLint  internal = has_alpha ? GL_RGBA : GL_RGB;
    const GLenum format   = has_alpha ? GL_RGBA : GL_RGB;

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

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

Texture prim_make_checker_texture(void) {
    enum { SIZE = 64, CELL = 8 };
    static unsigned char pixels[SIZE * SIZE * 3];

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            const int even = ((x / CELL) + (y / CELL)) % 2 == 0;
            unsigned char *p = &pixels[(y * SIZE + x) * 3];
            p[0] = even ? 140 : 105;
            p[1] = even ? 150 : 115;
            p[2] = even ? 140 : 105;
        }
    }
    return upload_rgb_pixels(pixels, SIZE, SIZE);
}

/* ---------- Процедурные текстуры Source / GoldSrc ---------- */

typedef struct {
    const char *name;
    Texture tex;
} BuiltinEntry;

static BuiltinEntry g_builtins[PRIM_BUILTIN_COUNT];
static int g_builtins_initialized = 0;

static void set_rgb(unsigned char *buf, int size, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= size || y < 0 || y >= size) return;
    int idx = (y * size + x) * 3;
    buf[idx + 0] = r;
    buf[idx + 1] = g;
    buf[idx + 2] = b;
}

/* 1. tools/toolsnodraw — легендарная текстура Hammer */
static Texture make_nodraw_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int border = (x == 0 || x == S - 1 || y == 0 || y == S - 1 || x == 1 || x == S - 2 || y == 1 || y == S - 2);
            int cross = (abs(x - y) <= 1 || abs(x - (S - 1 - y)) <= 1);
            if (border) {
                set_rgb(px, S, x, y, 70, 60, 10);
            } else if (cross) {
                set_rgb(px, S, x, y, 230, 190, 20);
            } else {
                set_rgb(px, S, x, y, 195, 160, 15);
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 2. tools/skybox — текстура неба */
static Texture make_skybox_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int border = (x == 0 || x == S - 1 || y == 0 || y == S - 1);
            int grid = (x % 16 == 0 || y % 16 == 0);
            if (border) {
                set_rgb(px, S, x, y, 30, 80, 150);
            } else if (grid) {
                set_rgb(px, S, x, y, 90, 160, 240);
            } else {
                set_rgb(px, S, x, y, 60, 130, 215);
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 3. dev/dev_measuregeneric01 — серая мерная сетка */
static Texture make_dev_gray_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int border = (x <= 1 || x >= S - 2 || y <= 1 || y >= S - 2);
            int cross = (x == S / 2 || y == S / 2);
            int subgrid = (x % 8 == 0 || y % 8 == 0);
            if (border || cross) {
                set_rgb(px, S, x, y, 160, 160, 160);
            } else if (subgrid) {
                set_rgb(px, S, x, y, 100, 100, 100);
            } else {
                set_rgb(px, S, x, y, 68, 68, 68);
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 4. dev/dev_measureorange — оранжевая мерная сетка */
static Texture make_dev_orange_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int border = (x <= 1 || x >= S - 2 || y <= 1 || y >= S - 2);
            int cross = (x == S / 2 || y == S / 2);
            int subgrid = (x % 8 == 0 || y % 8 == 0);
            if (border || cross) {
                set_rgb(px, S, x, y, 255, 200, 80);
            } else if (subgrid) {
                set_rgb(px, S, x, y, 225, 120, 20);
            } else {
                set_rgb(px, S, x, y, 195, 85, 10);
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 5. brick/brickwall001 — кирпичная кладка */
static Texture make_brick_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        int row = y / 8;
        int mortar_y = (y % 8 == 0);
        int shift = (row % 2 == 0) ? 0 : 8;
        for (int x = 0; x < S; x++) {
            int mortar_x = ((x + shift) % 16 == 0);
            if (mortar_y || mortar_x) {
                set_rgb(px, S, x, y, 175, 175, 165); /* раствор */
            } else {
                int noise = ((x * 13 + y * 29) % 25) - 12;
                int r = 160 + noise;
                int g = 65 + (noise / 2);
                int b = 45 + (noise / 2);
                set_rgb(px, S, x, y, (uint8_t)r, (uint8_t)g, (uint8_t)b);
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 6. concrete/concretefloor001 — гладкий бетон */
static Texture make_concrete_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int seam = (x == 0 || y == 0 || x == S - 1 || y == S - 1);
            int noise = ((x * 17 + y * 37 + (x ^ y) * 11) % 31) - 15;
            if (seam) {
                set_rgb(px, S, x, y, 80, 80, 80);
            } else {
                int c = 145 + noise;
                set_rgb(px, S, x, y, (uint8_t)c, (uint8_t)(c - 2), (uint8_t)(c - 5));
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 7. metal/metalfloor001 — рифленый металл */
static Texture make_metal_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int seam = (x % 32 == 0 || y % 32 == 0);
            int pattern = ((x / 4 + y / 4) % 2 == 0) && ((x % 4 == 1 || y % 4 == 1));
            int rivet = ((x % 32 == 4 || x % 32 == 28) && (y % 32 == 4 || y % 32 == 28));
            if (rivet) {
                set_rgb(px, S, x, y, 220, 225, 235);
            } else if (seam) {
                set_rgb(px, S, x, y, 50, 55, 60);
            } else if (pattern) {
                set_rgb(px, S, x, y, 140, 145, 155);
            } else {
                set_rgb(px, S, x, y, 95, 100, 110);
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 8. wood/woodcrate001 — деревянный ящик */
static Texture make_crate_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int frame = (x < 6 || x >= S - 6 || y < 6 || y >= S - 6);
            int diag = (abs(x - y) < 4 || abs(x - (S - 1 - y)) < 4);
            int corner_rivet = ((x == 3 || x == S - 4) && (y == 3 || y == S - 4));
            if (corner_rivet) {
                set_rgb(px, S, x, y, 200, 200, 200);
            } else if (frame) {
                set_rgb(px, S, x, y, 85, 55, 30);
            } else if (diag) {
                set_rgb(px, S, x, y, 110, 75, 45);
            } else {
                int grain = (y % 8 == 0) ? 90 : 135;
                int noise = ((x * 7 + y * 13) % 15);
                set_rgb(px, S, x, y, (uint8_t)(grain + noise), (uint8_t)(grain - 35 + noise), (uint8_t)(grain - 70 + noise));
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 9. tile/tilefloor001 — керамическая плитка */
static Texture make_tile_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int grout = (x % 16 == 0 || y % 16 == 0);
            int highlight = (x % 16 == 1 || y % 16 == 1);
            int dark_side = (x % 16 == 15 || y % 16 == 15);
            if (grout) {
                set_rgb(px, S, x, y, 50, 60, 70);
            } else if (highlight) {
                set_rgb(px, S, x, y, 220, 230, 240);
            } else if (dark_side) {
                set_rgb(px, S, x, y, 140, 155, 170);
            } else {
                set_rgb(px, S, x, y, 180, 195, 210);
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 10. hazard/hazard_stripe01 — предупредительные полосы */
static Texture make_hazard_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int stripe = ((x + y) / 8) % 2 == 0;
            if (stripe) {
                set_rgb(px, S, x, y, 240, 200, 10);  /* желтый */
            } else {
                set_rgb(px, S, x, y, 35, 35, 40);    /* черный */
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 11. stone/stonewall01 — каменная кладка */
static Texture make_stone_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int block_x = x / 16;
            int block_y = y / 16;
            int seam = (x % 16 == 0 || y % 16 == 0);
            int noise = ((x * 19 + y * 23 + block_x * 7) % 37) - 18;
            if (seam) {
                set_rgb(px, S, x, y, 60, 60, 60);
            } else {
                int base = 120 + ((block_x + block_y) % 2) * 20 + noise;
                set_rgb(px, S, x, y, (uint8_t)base, (uint8_t)base, (uint8_t)base);
            }
        }
    }
    return upload_rgb_pixels(px, S, S);
}

/* 12. ground/dirt01 — грунт / земля */
static Texture make_dirt_texture(void) {
    enum { S = 64 };
    unsigned char px[S * S * 3];
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            int noise = ((x * 31 + y * 47 + (x ^ y) * 19) % 43) - 21;
            int r = 90 + noise;
            int g = 65 + (noise * 3 / 4);
            int b = 45 + (noise / 2);
            set_rgb(px, S, x, y, (uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
    }
    return upload_rgb_pixels(px, S, S);
}

void prim_init_builtins(void) {
    if (g_builtins_initialized) return;

    g_builtins[0] = (BuiltinEntry){ "tools/toolsnodraw", make_nodraw_texture() };
    g_builtins[1] = (BuiltinEntry){ "dev/dev_measureorange", make_dev_orange_texture() };
    g_builtins[2] = (BuiltinEntry){ "dev/dev_measuregeneric01", make_dev_gray_texture() };
    g_builtins[3] = (BuiltinEntry){ "brick/brickwall001", make_brick_texture() };
    g_builtins[4] = (BuiltinEntry){ "concrete/concretefloor001", make_concrete_texture() };
    g_builtins[5] = (BuiltinEntry){ "metal/metalfloor001", make_metal_texture() };
    g_builtins[6] = (BuiltinEntry){ "wood/woodcrate001", make_crate_texture() };
    g_builtins[7] = (BuiltinEntry){ "hazard/hazard_stripe01", make_hazard_texture() };
    g_builtins[8] = (BuiltinEntry){ "tile/tilefloor001", make_tile_texture() };
    g_builtins[9] = (BuiltinEntry){ "stone/stonewall01", make_stone_texture() };
    g_builtins[10] = (BuiltinEntry){ "ground/dirt01", make_dirt_texture() };
    g_builtins[11] = (BuiltinEntry){ "tools/skybox", make_skybox_texture() };
    g_builtins[12] = (BuiltinEntry){ "checker", prim_make_checker_texture() };

    g_builtins_initialized = 1;
}

int prim_builtin_count(void) {
    return PRIM_BUILTIN_COUNT;
}

const char *prim_builtin_name(int index) {
    if (index < 0 || index >= PRIM_BUILTIN_COUNT) return "";
    return g_builtins[index].name;
}

const Texture *prim_builtin_texture(int index) {
    if (!g_builtins_initialized) prim_init_builtins();
    if (index < 0 || index >= PRIM_BUILTIN_COUNT) return NULL;
    return &g_builtins[index].tex;
}

const Texture *prim_find_builtin(const char *name) {
    if (!name || name[0] == '\0') return NULL;
    if (!g_builtins_initialized) prim_init_builtins();

    for (int i = 0; i < PRIM_BUILTIN_COUNT; i++) {
        if (strcmp(g_builtins[i].name, name) == 0) {
            return &g_builtins[i].tex;
        }
        /* Также проверяем совпадение по короткому имени (например "brick" для "brick/brickwall001") */
        const char *slash = strrchr(g_builtins[i].name, '/');
        const char *short_name = slash ? slash + 1 : g_builtins[i].name;
        if (strcmp(short_name, name) == 0) {
            return &g_builtins[i].tex;
        }
    }
    return NULL;
}

void prim_free_texture(Texture *tex) {
    if (tex && tex->id != 0) {
        glDeleteTextures(1, &tex->id);
        tex->id = 0;
        tex->width = 0;
        tex->height = 0;
    }
}

void prim_emit_quad(const PrimVertex v[4]) {
    static const int order[6] = {0, 1, 2, 0, 2, 3};

    for (int i = 0; i < 6; i++) {
        const PrimVertex *p = &v[order[i]];
        glTexCoord2f(p->u, p->v);
        glVertex3f(p->x, p->y, p->z);
    }
}

void prim_draw_cell(const Texture *tex, const CellQuad *q) {
    const float u0 = prim_tex_u(tex, q->x0);
    const float u1 = prim_tex_u(tex, q->x1);
    const float v0 = prim_tex_v(tex, q->z0);
    const float v1 = prim_tex_v(tex, q->z1);

    const PrimVertex quad[4] = {
        {u0, v0, q->x0, q->y00, q->z0},
        {u1, v0, q->x1, q->y10, q->z0},
        {u1, v1, q->x1, q->y11, q->z1},
        {u0, v1, q->x0, q->y01, q->z1},
    };

    glBegin(GL_TRIANGLES);
    prim_emit_quad(quad);
    glEnd();
}

void prim_draw_wire_box(float min_x, float min_y, float min_z,
                        float max_x, float max_y, float max_z) {
    glBegin(GL_LINES);
    /* низ */
    glVertex3f(min_x, min_y, min_z); glVertex3f(max_x, min_y, min_z);
    glVertex3f(max_x, min_y, min_z); glVertex3f(max_x, min_y, max_z);
    glVertex3f(max_x, min_y, max_z); glVertex3f(min_x, min_y, max_z);
    glVertex3f(min_x, min_y, max_z); glVertex3f(min_x, min_y, min_z);
    /* верх */
    glVertex3f(min_x, max_y, min_z); glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, max_y, min_z); glVertex3f(max_x, max_y, max_z);
    glVertex3f(max_x, max_y, max_z); glVertex3f(min_x, max_y, max_z);
    glVertex3f(min_x, max_y, max_z); glVertex3f(min_x, max_y, min_z);
    /* вертикали */
    glVertex3f(min_x, min_y, min_z); glVertex3f(min_x, max_y, min_z);
    glVertex3f(max_x, min_y, min_z); glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, min_y, max_z); glVertex3f(max_x, max_y, max_z);
    glVertex3f(min_x, min_y, max_z); glVertex3f(min_x, max_y, max_z);
    glEnd();
}

void prim_draw_solid_box(float min_x, float min_y, float min_z,
                         float max_x, float max_y, float max_z) {
    glBegin(GL_QUADS);
    /* Y- и Y+ */
    glVertex3f(min_x, min_y, min_z); glVertex3f(max_x, min_y, min_z);
    glVertex3f(max_x, min_y, max_z); glVertex3f(min_x, min_y, max_z);
    glVertex3f(min_x, max_y, min_z); glVertex3f(min_x, max_y, max_z);
    glVertex3f(max_x, max_y, max_z); glVertex3f(max_x, max_y, min_z);
    /* Z- и Z+ */
    glVertex3f(min_x, min_y, min_z); glVertex3f(min_x, max_y, min_z);
    glVertex3f(max_x, max_y, min_z); glVertex3f(max_x, min_y, min_z);
    glVertex3f(min_x, min_y, max_z); glVertex3f(max_x, min_y, max_z);
    glVertex3f(max_x, max_y, max_z); glVertex3f(min_x, max_y, max_z);
    /* X- и X+ */
    glVertex3f(min_x, min_y, min_z); glVertex3f(min_x, min_y, max_z);
    glVertex3f(min_x, max_y, max_z); glVertex3f(min_x, max_y, min_z);
    glVertex3f(max_x, min_y, min_z); glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, max_y, max_z); glVertex3f(max_x, min_y, max_z);
    glEnd();
}

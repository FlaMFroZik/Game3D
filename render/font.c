/* stb_truetype — готовая single-header библиотека растеризации TrueType
 * (public domain, https://github.com/nothings/stb). Реализация нужна ровно
 * в одном файле, поэтому она включается здесь, а не в заголовке. */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/font.h"

/* GL_CLAMP_TO_EDGE появился в OpenGL 1.2, а базовый <GL/gl.h> (и Windows,
 * и Linux) его не объявляет. Значение стандартное и поддерживается всюду,
 * где есть контекст 3.3, который просит render/window.h. */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

/* ---------- Что запекаем в атлас ---------- */
/* Непрерывные диапазоны: stb_truetype печёт их одним вызовом каждый. */
#define FONT_ASCII_FIRST 0x20
#define FONT_ASCII_LAST  0x7E
#define FONT_CYR_FIRST   0x410   /* А */
#define FONT_CYR_LAST    0x44F   /* я */
#define FONT_YO_UPPER    0x401   /* Ё — вне диапазона А-Я */
#define FONT_YO_LOWER    0x451   /* ё */

#define FONT_ASCII_COUNT (FONT_ASCII_LAST - FONT_ASCII_FIRST + 1)
#define FONT_CYR_COUNT   (FONT_CYR_LAST - FONT_CYR_FIRST + 1)
#define FONT_GLYPH_COUNT (FONT_ASCII_COUNT + FONT_CYR_COUNT + 2)

/* Атлас: с запасом под oversampling, чтобы все глифы влезли одним заходом. */
#define FONT_ATLAS_WIDTH  1024
#define FONT_ATLAS_HEIGHT 512
/* Последние строки атласа не отдаём упаковщику: там лежит сплошной белый
 * квадрат, которым рисуется символ-заглушка (его нет ни в одном шрифте). */
#define FONT_SOLID_ROWS   8
/* Сколько раз глиф растеризуется по каждой оси: 2x2 даёт сглаженные края
 * у текста любого размера, стОит вчетверо больше памяти атласа. */
#define FONT_OVERSAMPLE   2

struct Font {
    GLuint texture;      /* атлас: RGBA, в альфе — покрытие глифа */
    int atlas_width;
    int atlas_height;
    float pixel_size;
    float ascent;        /* от верха строки до базовой линии, пиксели */
    float descent;       /* ниже базовой линии, пиксели (отрицательное) */
    float line_height;
    /* Квадры и шаги глифов, посчитанные stb_truetype. */
    stbtt_packedchar glyphs[FONT_GLYPH_COUNT];
};

/* ---------- Чтение файла ---------- */

static unsigned char *font_read_file(const char *path, long *out_size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;

    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size <= 0) { fclose(file); return NULL; }
    rewind(file);

    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data) { fclose(file); return NULL; }
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);

    *out_size = size;
    return data;
}

/* ---------- Коды символов ---------- */

/* Индекс глифа в атласе по коду символа, -1 если символа в атласе нет. */
static int font_glyph_index(int codepoint) {
    if (codepoint >= FONT_ASCII_FIRST && codepoint <= FONT_ASCII_LAST) {
        return codepoint - FONT_ASCII_FIRST;
    }
    if (codepoint >= FONT_CYR_FIRST && codepoint <= FONT_CYR_LAST) {
        return FONT_ASCII_COUNT + (codepoint - FONT_CYR_FIRST);
    }
    if (codepoint == FONT_YO_UPPER) return FONT_ASCII_COUNT + FONT_CYR_COUNT;
    if (codepoint == FONT_YO_LOWER) return FONT_ASCII_COUNT + FONT_CYR_COUNT + 1;
    return -1;
}

/* Один символ UTF-8. Возвращает длину последовательности в байтах
 * (0 — обрыв строки, -1 — битая последовательность, её пропускаем). */
static int font_decode_utf8(const char *text, int *codepoint) {
    const unsigned char *s = (const unsigned char *)text;

    if (s[0] < 0x80) { *codepoint = s[0]; return s[0] ? 1 : 0; }

    int extra;
    int value;
    if ((s[0] & 0xE0) == 0xC0)      { value = s[0] & 0x1F; extra = 1; }
    else if ((s[0] & 0xF0) == 0xE0) { value = s[0] & 0x0F; extra = 2; }
    else if ((s[0] & 0xF8) == 0xF0) { value = s[0] & 0x07; extra = 3; }
    else return -1;

    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) return -1;
        value = (value << 6) | (s[i] & 0x3F);
    }
    *codepoint = value;
    return extra + 1;
}

/* ---------- Загрузка ---------- */

Font *font_create(const char *ttf_path, float pixel_size) {
    long size = 0;
    unsigned char *data = font_read_file(ttf_path, &size);
    if (!data) {
        fprintf(stderr, "Cannot read font file: %s\n", ttf_path);
        return NULL;
    }

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        fprintf(stderr, "Not a TrueType font: %s\n", ttf_path);
        free(data);
        return NULL;
    }

    Font *font = (Font *)calloc(1, sizeof(Font));
    if (!font) { free(data); return NULL; }

    font->pixel_size = pixel_size;
    font->atlas_width = FONT_ATLAS_WIDTH;
    font->atlas_height = FONT_ATLAS_HEIGHT;

    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, pixel_size);
    font->ascent = (float)ascent * scale;
    font->descent = (float)descent * scale;
    font->line_height = (float)(ascent - descent + line_gap) * scale;

    /* Атлас печатаем в одноканальный буфер, как это делает stb_truetype. */
    unsigned char *coverage = (unsigned char *)calloc(
        (size_t)font->atlas_width * (size_t)font->atlas_height, 1);
    if (!coverage) { free(font); free(data); return NULL; }

    stbtt_pack_context pack;
    int packed = 0;
    /* Упаковщику отдаём атлас без последних строк — там будет заглушка. */
    if (stbtt_PackBegin(&pack, coverage, font->atlas_width,
                        font->atlas_height - FONT_SOLID_ROWS, 0, 1, NULL)) {
        /* Глифа может не быть в шрифте — тогда берётся заглушка, а не
         * ошибка: интерфейс обязан работать с любым подходящим шрифтом. */
        pack.skip_missing = 1;
        stbtt_PackSetOversampling(&pack, FONT_OVERSAMPLE, FONT_OVERSAMPLE);

        packed = stbtt_PackFontRange(&pack, data, 0, pixel_size,
                                     FONT_ASCII_FIRST, FONT_ASCII_COUNT,
                                     font->glyphs);
        packed &= stbtt_PackFontRange(&pack, data, 0, pixel_size,
                                      FONT_CYR_FIRST, FONT_CYR_COUNT,
                                      font->glyphs + FONT_ASCII_COUNT);
        packed &= stbtt_PackFontRange(&pack, data, 0, pixel_size,
                                      FONT_YO_UPPER, 1,
                                      font->glyphs + FONT_ASCII_COUNT + FONT_CYR_COUNT);
        packed &= stbtt_PackFontRange(&pack, data, 0, pixel_size,
                                      FONT_YO_LOWER, 1,
                                      font->glyphs + FONT_ASCII_COUNT + FONT_CYR_COUNT + 1);
        stbtt_PackEnd(&pack);
    }

    if (!packed) {
        fprintf(stderr, "Cannot bake font atlas from: %s\n", ttf_path);
        free(coverage);
        free(font);
        free(data);
        return NULL;
    }

    /* Сплошной квадрат для символов-заглушек: в последние строки атласа,
     * которые упаковщику не отдавались. */
    for (int y = font->atlas_height - FONT_SOLID_ROWS; y < font->atlas_height; y++) {
        for (int x = 0; x < font->atlas_width; x++) {
            coverage[(size_t)y * (size_t)font->atlas_width + (size_t)x] = 255;
        }
    }

    /* Одноканальное покрытие раскладываем в RGBA: так атлас рисует текст
     * цветом glVertex в любом профиле OpenGL, без swizzle и luminance. */
    const size_t pixels = (size_t)font->atlas_width * (size_t)font->atlas_height;
    unsigned char *rgba = (unsigned char *)malloc(pixels * 4);
    if (!rgba) { free(coverage); free(font); free(data); return NULL; }

    for (size_t i = 0; i < pixels; i++) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage[i];
    }
    free(coverage);

    glGenTextures(1, &font->texture);
    glBindTexture(GL_TEXTURE_2D, font->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 font->atlas_width, font->atlas_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(rgba);

    /* Сам файл больше не нужен: глифы уже в атласе. */
    free(data);
    return font;
}

void font_destroy(Font *font) {
    if (!font) return;
    if (font->texture) {
        glDeleteTextures(1, &font->texture);
        font->texture = 0;
    }
    free(font);
}

float font_line_height(const Font *font, float scale) {
    return font->line_height * scale;
}

float font_text_width(const Font *font, const char *utf8, float scale) {
    float x = 0.0f;
    const char *p = utf8;

    while (*p) {
        int codepoint = 0;
        int length = font_decode_utf8(p, &codepoint);
        if (length <= 0) { p += 1; continue; }
        p += length;

        const int index = font_glyph_index(codepoint);
        /* Неизвестный символ занимает столько же, сколько «n», чтобы
         * ширина строки не зависела от покрытия шрифта. */
        x += (index >= 0)
            ? font->glyphs[index].xadvance
            : font->glyphs[font_glyph_index('n')].xadvance;
    }
    return x * scale;
}

/* ---------- Отрисовка ---------- */

/* Квад глифа в пикселях окна: один прямоугольник с координатами атласа. */
static void font_emit_quad(const stbtt_aligned_quad *q, float x, float y,
                           float scale) {
    const float x0 = x + q->x0 * scale;
    const float y0 = y + q->y0 * scale;
    const float x1 = x + q->x1 * scale;
    const float y1 = y + q->y1 * scale;

    glTexCoord2f(q->s0, q->t0); glVertex2f(x0, y0);
    glTexCoord2f(q->s1, q->t0); glVertex2f(x1, y0);
    glTexCoord2f(q->s1, q->t1); glVertex2f(x1, y1);
    glTexCoord2f(q->s0, q->t1); glVertex2f(x0, y1);
}

void font_draw(const Font *font, float x, float y, float scale,
               const float rgba[4], const char *utf8) {
    if (!font || !font->texture || !utf8) return;

    glBindTexture(GL_TEXTURE_2D, font->texture);
    glEnable(GL_TEXTURE_2D);
    glColor4fv(rgba);
    glBegin(GL_QUADS);

    /* Координаты глифов считаются от базовой линии, поэтому строку
     * поднимаем на ascent: y остаётся верхним краем строки. */
    float pen_x = 0.0f;
    float baseline = font->ascent;
    const char *p = utf8;

    while (*p) {
        int codepoint = 0;
        int length = font_decode_utf8(p, &codepoint);
        if (length <= 0) { p += 1; continue; }
        p += length;

        const int index = font_glyph_index(codepoint);
        if (index >= 0) {
            stbtt_aligned_quad quad;
            /* align_to_integer = 0: позиция дробная, иначе мелкий текст
             * «прыгает» по пикселям при изменении масштаба интерфейса. */
            stbtt_GetPackedQuad(font->glyphs, font->atlas_width,
                                font->atlas_height, index,
                                &pen_x, &baseline, &quad, 0);
            font_emit_quad(&quad, x, y, scale);
        } else {
            /* Символа нет в атласе: рисуем квадрат сплошным текселем атласа
             * (центр текселя — иначе фильтр смешает его с пустым соседом),
             * а шаг оставляем как у «n», чтобы ширина строки совпадала с
             * font_text_width. */
            const float advance = font->glyphs[font_glyph_index('n')].xadvance;
            stbtt_aligned_quad box;
            box.s0 = box.s1 = 4.5f / (float)font->atlas_width;
            box.t0 = box.t1 = ((float)font->atlas_height - 3.5f)
                              / (float)font->atlas_height;
            box.x0 = pen_x;
            box.x1 = pen_x + advance * 0.7f;
            /* Координаты считаются от базовой линии (ypos = ascent), поэтому
             * верх квадрата — высота прописных букв, низ — сама линия. */
            box.y0 = font->ascent * 0.28f;
            box.y1 = font->ascent;
            font_emit_quad(&box, x, y, scale);
            pen_x += advance;
        }
    }

    glEnd();
    glDisable(GL_TEXTURE_2D);
}

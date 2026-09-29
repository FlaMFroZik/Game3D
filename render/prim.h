#ifndef PRIM_H
#define PRIM_H

/* ------------------------------------------------------------------
 * Примитивы: текстуры и геометрия поверхностей.
 * Модуль ничего не знает про мир и камеру — только про OpenGL.
 *
 * Текстура ложится на поверхность БЕЗ растяжения: одна её копия занимает
 * в мире фиксированный размер (PRIM_TEX_TILE_METERS по большей стороне),
 * а меньшая сторона выходит из пропорций самой текстуры. Дальше рисунок
 * просто повторяется по поверхности — сколько копий влезло.
 * ------------------------------------------------------------------ */

#include <GL/gl.h>

/* Сколько метров мира занимает большая сторона одной копии текстуры
 * по умолчанию (карта может задать свой размер — см. map/map.h). */
#define PRIM_TEX_TILE_METERS 2.0f

/* Загруженная текстура: идентификатор OpenGL и её размер в текселях. */
typedef struct {
    GLuint id;      /* 0 — текстура не загружена */
    int width;      /* ширина в текселях */
    int height;     /* высота в текселях */
} Texture;

/* Загружает .raw-текстуру в видеопамять. */
Texture prim_load_texture(const char *filename);

/* Освобождает текстуру, созданную prim_load_texture (id обнуляется). */
void prim_free_texture(Texture *tex);

/* Встроенная «шахматка» 64x64 — текстура по умолчанию для редактора. */
Texture prim_make_checker_texture(void);

/* ---------- Встроенная библиотека текстур (стиль Source / GoldSrc) ---------- */

#define PRIM_BUILTIN_COUNT 13

/* Инициализирует и возвращает встроенные текстуры (nodraw, dev, brick, crate, etc.) */
void prim_init_builtins(void);
int prim_builtin_count(void);
const char *prim_builtin_name(int index);
const Texture *prim_builtin_texture(int index);
const Texture *prim_find_builtin(const char *name);

/* ---------- Раскладка текстуры ---------- */

float prim_tex_u(const Texture *tex, float meters);
float prim_tex_v(const Texture *tex, float meters);

float prim_tex_u_tile(const Texture *tex, float meters, float tile_meters);
float prim_tex_v_tile(const Texture *tex, float meters, float tile_meters);

/* Вершина с координатами текстуры. */
typedef struct {
    float u, v;     /* координаты текстуры */
    float x, y, z;  /* позиция в мире */
} PrimVertex;

/* Четырёхугольник v[0..3] как два треугольника (0,1,2) и (0,2,3). */
void prim_emit_quad(const PrimVertex v[4]);

/* Четыре угла клетки: (x0,z0)-(x1,z1), высоты y00/y10/y01/y11. */
typedef struct {
    float x0, z0;   /* дальний (по X и Z) угол клетки */
    float x1, z1;   /* ближний угол клетки */
    float y00, y10; /* высоты дальнего и ближнего угла по X */
    float y01, y11; /* высоты дальнего и ближнего угла по X + 1 по Z */
} CellQuad;

/* Рисует клетку как два треугольника. */
void prim_draw_cell(const Texture *tex, const CellQuad *q);

/* Рисование проволочного куба (AABB) */
void prim_draw_wire_box(float min_x, float min_y, float min_z,
                        float max_x, float max_y, float max_z);

/* Рисование сплошного одноцветного куба (AABB) */
void prim_draw_solid_box(float min_x, float min_y, float min_z,
                         float max_x, float max_y, float max_z);

#endif

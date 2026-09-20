#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "gen.h"

int GEN_SEED;

/* ---------- Шум ---------- */

static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + GEN_SEED;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

static float hash01(int x, int y) {
    /* старшие 24 бита хеша -> [0, 1) */
    return (float)(hash2(x, y) >> 8) * (1.0f / 16777216.0f);
}

static float smooth01(float t) {
    /* Квинтический smootherstep: C2 в узлах решётки, поэтому на стыках
     * октав нет заметных изломов — холмы получаются округлыми. */
    return t * t * t * (t * (6.0f * t - 15.0f) + 10.0f);
}

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* Плавный шум в узлах целочисленной решётки. */
static float value_noise(float x, float y) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    float tx = smooth01(x - (float)x0);
    float ty = smooth01(y - (float)y0);

    float n00 = hash01(x0,     y0);
    float n10 = hash01(x0 + 1, y0);
    float n01 = hash01(x0,     y0 + 1);
    float n11 = hash01(x0 + 1, y0 + 1);

    float nx0 = n00 + (n10 - n00) * tx;
    float nx1 = n01 + (n11 - n01) * tx;
    return nx0 + (nx1 - nx0) * ty;
}

/* ---------- Форма рельефа ---------- */

#define GEN_MAX_HEIGHT_Q   (GEN_MAX_HEIGHT * GEN_HEIGHT_SUBDIV)
#define GEN_TERRACE_Q      (GEN_TERRACE_STEP * GEN_HEIGHT_SUBDIV)
#define GEN_TERRACE_ROUGH_Q (GEN_TERRACE_ROUGH * GEN_HEIGHT_SUBDIV)

static float terrain_shape(int wx, int wz) {
    const float fx = (float)wx;
    const float fz = (float)wz;
    const float s = 1.0f / GEN_NOISE_SCALE;

    /* 1) пологий fBm */
    float n = value_noise(fx * s,          fz * s)
            + 0.45f * value_noise(fx * 2.0f * s, fz * 2.0f * s)
            + 0.20f * value_noise(fx * 4.0f * s, fz * 4.0f * s);
    n *= 1.0f / 1.65f;

    /* 2) равнина/холм: нижняя часть шума срезается, выход сглажен */
    float hill = smooth01(clamp01((n - GEN_PLAINS_CUTOFF) * GEN_PLAINS_GAIN));

    /* 3) поля холмов и пустое пространство между ними */
    float m = value_noise(fx * (0.5f * s) + 41.3f, fz * (0.5f * s) - 17.7f);
    float relief = GEN_RELIEF_FLOOR + (1.0f - GEN_RELIEF_FLOOR) *
                   smooth01(clamp01((m - 0.35f) * (1.0f / 0.5f)));

    /* раскачка равнины: шум самой большой длины волны, 0 .. GEN_PLAIN_ROUGH */
    float base = value_noise(fx * (0.8f * s) - 83.1f, fz * (0.8f * s) + 29.7f)
               + 0.5f * value_noise(fx * 1.6f * s + 13.0f, fz * 1.6f * s - 61.0f);
    base = clamp01(base * (1.0f / 1.5f));

    return (float)GEN_PLAIN_ROUGH * base
         + hill * relief * (float)(GEN_MAX_HEIGHT - GEN_PLAIN_ROUGH);
}

/* Тот же рельеф, но уже в квантах (то, что лежит в Chunk.height). */
static int raw_cell_height(int wx, int wz) {
    int q = (int)(terrain_shape(wx, wz) * (float)GEN_HEIGHT_SUBDIV + 0.5f);

    if (GEN_TERRACE_Q > 1) {
        q -= q % GEN_TERRACE_Q;  /* плоские площадки-террасы */

        /* Неровность внутри террасы: ±GEN_TERRACE_ROUGH клеток,
         * то есть меньше шага игрока — на проходимость не влияет. */
        q += (int)(hash01(wx, wz) * (float)(2 * GEN_TERRACE_ROUGH_Q + 1))
           - GEN_TERRACE_ROUGH_Q;
    }

    if (q < 0) q = 0;
    if (q > GEN_MAX_HEIGHT_Q) q = GEN_MAX_HEIGHT_Q;
    return q;
}

/* ---------- Пул чанков ---------- */

/* Потолок проверяется в квантах: height[] в Chunk — один байт на узел. */
_Static_assert(GEN_MAX_HEIGHT_Q <= 255, "высота не влезает в GenHeight (см. gen.h)");

static Chunk chunk_pool[GEN_MAX_CHUNKS];
static int   chunk_count = 0;

void gen_init(void) {
    chunk_count = 0;

    /* time_t имеет разный размер на разных компиляторах; явно сворачиваем
     * значение в 32-битный seed без предупреждений MSVC/GCC. */
    uint64_t now = (uint64_t)time(NULL);
    GEN_SEED = (int)(now ^ (now >> 32));
}

int gen_chunk_count(void) {
    return chunk_count;
}

const Chunk *gen_chunk_at(int index) {
    if (index < 0 || index >= chunk_count) return NULL;
    return &chunk_pool[index];
}

Chunk *gen_find_chunk(int cx, int cz) {
    for (int i = 0; i < chunk_count; i++) {
        if (chunk_pool[i].cx == cx && chunk_pool[i].cz == cz) return &chunk_pool[i];
    }
    return NULL;
}

/* Загружает чанк целиком и только потом увеличивает счётчик: наружу
 * никогда не видно чанка с недосчитанными высотами. NULL — пул полон. */
static Chunk *load_chunk(int cx, int cz) {
    if (chunk_count >= GEN_MAX_CHUNKS) return NULL;

    Chunk *c = &chunk_pool[chunk_count];
    c->cx = cx;
    c->cz = cz;

    int base_wx = cx * GEN_CHUNK_SIZE;
    int base_wz = cz * GEN_CHUNK_SIZE;

    for (int x = 0; x < GEN_CHUNK_VERTS; x++) {
        for (int z = 0; z < GEN_CHUNK_VERTS; z++) {
            c->height[x][z] = (GenHeight)raw_cell_height(base_wx + x, base_wz + z);
        }
    }

    chunk_count++;
    return c;
}

/* Выгружает чанки, ушедшие далеко за радиус видимости: на их месте
 * будут загружены новые. Дыр в рельефе не бывает — высоты берутся
 * из шума, даже если чанк не загружен. */
static void unload_far_chunks(int pcx, int pcz) {
    const int keep = GEN_VIEW_RADIUS + 1;

    for (int i = 0; i < chunk_count; ) {
        Chunk *c = &chunk_pool[i];
        int dx = c->cx - pcx;
        int dz = c->cz - pcz;
        if (dx < 0) dx = -dx;
        if (dz < 0) dz = -dz;

        if (dx > keep || dz > keep) {
            chunk_pool[i] = chunk_pool[chunk_count - 1];  /* порядок чанков не важен */
            chunk_count--;
        } else {
            i++;
        }
    }
}

void gen_update_chunks(float cam_x, float cam_z) {
    int pcx = gen_chunk_coord(gen_world_to_cell(cam_x));
    int pcz = gen_chunk_coord(gen_world_to_cell(cam_z));

    unload_far_chunks(pcx, pcz);

    int generated = 0;
    for (int dx = -GEN_VIEW_RADIUS; dx <= GEN_VIEW_RADIUS && generated < GEN_MAX_CHUNKS_PER_FRAME; dx++) {
        for (int dz = -GEN_VIEW_RADIUS; dz <= GEN_VIEW_RADIUS && generated < GEN_MAX_CHUNKS_PER_FRAME; dz++) {
            int cx = pcx + dx;
            int cz = pcz + dz;

            if (gen_find_chunk(cx, cz)) continue;
            if (!load_chunk(cx, cz)) return;  /* пул полон — догрузим в следующих кадрах */

            generated++;
        }
    }
}

/* ---------- Высоты ---------- */

int gen_cell_height(int wx, int wz) {
    int cx = gen_chunk_coord(wx);
    int cz = gen_chunk_coord(wz);

    const Chunk *c = gen_find_chunk(cx, cz);
    if (c) {
        return c->height[wx - cx * GEN_CHUNK_SIZE][wz - cz * GEN_CHUNK_SIZE];
    }
    return raw_cell_height(wx, wz);  /* значения совпадают с данными чанка */
}

static float cell_y(int wx, int wz) {
    return (float)gen_cell_height(wx, wz) * GEN_HEIGHT_STEP + GEN_TERRAIN_OFFSET;
}

float gen_sample_height(float x, float z) {
    float fx = x / GEN_CELL_SIZE;
    float fz = z / GEN_CELL_SIZE;

    int wx0 = (int)floorf(fx);
    int wz0 = (int)floorf(fz);
    float tx = fx - (float)wx0;
    float tz = fz - (float)wz0;

    float hx0 = cell_y(wx0,     wz0)     * (1.0f - tx) + cell_y(wx0 + 1, wz0)     * tx;
    float hx1 = cell_y(wx0,     wz0 + 1) * (1.0f - tx) + cell_y(wx0 + 1, wz0 + 1) * tx;
    return hx0 * (1.0f - tz) + hx1 * tz;
}

float gen_chunk_y(const Chunk *c, int lx, int lz) {
    if (lx < 0) lx = 0;
    if (lx > GEN_CHUNK_SIZE) lx = GEN_CHUNK_SIZE;
    if (lz < 0) lz = 0;
    if (lz > GEN_CHUNK_SIZE) lz = GEN_CHUNK_SIZE;

    return (float)c->height[lx][lz] * GEN_HEIGHT_STEP + GEN_TERRAIN_OFFSET;
}

/* ---------- Координаты ---------- */

int gen_floor_div(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

int gen_chunk_coord(int cell) {
    return gen_floor_div(cell, GEN_CHUNK_SIZE);
}

int gen_world_to_cell(float w) {
    return (int)floorf(w / GEN_CELL_SIZE);
}

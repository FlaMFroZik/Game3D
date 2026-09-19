#ifndef GEN_H
#define GEN_H

/* ------------------------------------------------------------------
 * Генерация мира.
 *
 * Мир разбит на чанки — квадраты GEN_CHUNK_SIZE x GEN_CHUNK_SIZE клеток.
 * Высота клетки считается процедурно (детерминированный шум), поэтому
 * рельеф бесконечный и одинаковый независимо от того, загружен чанк
 * или нет. В загруженном чанке высоты лежат в массиве: рендер берёт их
 * оттуда, чтобы не считать шум на каждую вершину каждый кадр.
 * ------------------------------------------------------------------ */

#define GEN_CELL_SIZE            0.25f  /* размер одной клетки в мировых единицах */
#define GEN_CHUNK_SIZE           32     /* клеток по стороне чанка */
#define GEN_CHUNK_VERTS          (GEN_CHUNK_SIZE + 1) /* высоты хранятся с общим краем */
#define GEN_VIEW_RADIUS          2      /* радиус отрисовки/генерации в чанках */
#define GEN_MAX_CHUNKS           256    /* размер пула чанков */
#define GEN_MAX_CHUNKS_PER_FRAME 8      /* сколько чанков максимум генерируем за кадр */
#define GEN_TERRAIN_OFFSET       1.0f   /* подъём рельефа над y = 0 */
#define GEN_MAX_HEIGHT           48     /* максимальная высота рельефа в клетках */
#define GEN_TERRACE_STEP         3      /* высота террасы в клетках */
#define GEN_TERRACE_ROUGH        0      /* разброс высоты внутри террасы, в клетках */

typedef struct {
    int cx, cz;                                              /* координаты чанка */
    unsigned char height[GEN_CHUNK_VERTS][GEN_CHUNK_VERTS];  /* высота клетки в клетках */
} Chunk;

/* ---------- Пул чанков ---------- */

void          gen_init(void);
int           gen_chunk_count(void);           /* число загруженных чанков */
const Chunk  *gen_chunk_at(int index);         /* NULL, если индекса нет */
Chunk        *gen_find_chunk(int cx, int cz);  /* NULL, если чанк не загружен */

/* Догружает чанки вокруг точки мира и выгружает ушедшие далеко. */
void gen_update_chunks(float cam_x, float cam_z);

/* ---------- Высоты ---------- */

int   gen_cell_height(int wx, int wz);         /* высота клетки в клетках */
float gen_sample_height(float x, float z);     /* высота рельефа в мире (билинейно) */

/* Высота угла внутри чанка, lx/lz в [0 .. GEN_CHUNK_SIZE]. */
float gen_chunk_y(const Chunk *c, int lx, int lz);

/* ---------- Координаты ---------- */

int gen_floor_div(int a, int b);               /* деление с округлением вниз */
int gen_chunk_coord(int cell);                 /* клетка -> номер чанка */
int gen_world_to_cell(float w);                /* мир -> номер клетки */

#endif

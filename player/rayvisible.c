#include "player/rayvisible.h"

/* Куб слегка сжимается перед проверкой, чтобы луч, скользящий по
 * поверхности (например, два игрока, стоящие на одном полу), не
 * считался перекрытым из-за округления float. */
#define RAY_BOX_SHRINK 0.02f

/* Пересекает ли отрезок from + t*(to-from), t в [0, 1], параллелепипед.
 * Метод slab: для каждой оси отрезок входит в куб в t_enter и выходит
 * в t_exit; отрезок пересекает куб, если эти интервалы накладываются. */
static int segment_hits_box(float from_x, float from_y, float from_z,
                            float dx, float dy, float dz,
                            const SrvBox *box) {
    const float origin[3] = {from_x, from_y, from_z};
    const float direction[3] = {dx, dy, dz};
    const float lo[3] = {box->min_x + RAY_BOX_SHRINK,
                         box->min_y + RAY_BOX_SHRINK,
                         box->min_z + RAY_BOX_SHRINK};
    const float hi[3] = {box->max_x - RAY_BOX_SHRINK,
                         box->max_y - RAY_BOX_SHRINK,
                         box->max_z - RAY_BOX_SHRINK};
    float t_enter = 0.0f;
    float t_exit = 1.0f;
    int axis;

    for (axis = 0; axis < 3; axis++) {
        if (direction[axis] == 0.0f) {
            /* Луч параллелен граням по этой оси: промах, если идёт сбоку. */
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) return 0;
        } else {
            float t1 = (lo[axis] - origin[axis]) / direction[axis];
            float t2 = (hi[axis] - origin[axis]) / direction[axis];
            float swap;

            if (t1 > t2) {
                swap = t1; t1 = t2; t2 = swap;
            }
            if (t1 > t_enter) t_enter = t1;
            if (t2 < t_exit) t_exit = t2;
            /* Касание поверхности (t_enter == t_exit) попаданием не
             * считается — строгое неравенство. */
            if (t_enter >= t_exit) return 0;
        }
    }

    /* Интервал пересечения должен пересекаться с самим отрезком [0, 1]. */
    return t_enter < t_exit && t_exit > 0.0f && t_enter < 1.0f;
}

int ray_player_visible(const SrvMap *map,
                       float from_x, float from_y, float from_z,
                       float to_x, float to_y, float to_z) {
    size_t i;

    if (map == NULL || map->count == 0) return 1;

    for (i = 0; i < map->count; i++) {
        if (segment_hits_box(from_x, from_y, from_z,
                             to_x - from_x, to_y - from_y, to_z - from_z,
                             &map->boxes[i])) {
            return 0;
        }
    }
    return 1;
}

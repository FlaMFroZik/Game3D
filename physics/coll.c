#include <math.h>

#include "physics/coll.h"
#include "map/gen.h"
#include "map/map.h"

/* ------------------------------------------------------------------
 * Реализация коллизий игрока с рельефом (процедурная генерация) и
 * объектами карты (кубы из map/map.c).
 *
 * Мир бывает двух видов (см. main.c / render/render.c):
 *   - процедурный рельеф: высоту даёт gen_sample_height(), кубов нет;
 *   - пользовательская карта: рельефа нет, есть только кубы, пол на y = 0.
 * Здесь оба случая сведены к единому набору запросов высоты и стен, а
 * какой мир активен — сообщает map_is_custom().
 * ------------------------------------------------------------------ */

/* Пол пользовательской карты, когда под ногами нет ни одного куба. */
#define COLL_MAP_FLOOR 0.0f

/* M_PI не входит в стандарт C11 (и его нет в MSVC по умолчанию). */
#define COLL_TWO_PI 6.28318530717958647692f

/* Сколько точек по окружности коллайдера пробуем для рельефа: центр плюс
 * равномерно распределённые точки на радиусе. Больше точек — аккуратнее
 * прилегание к склонам и стенам, но дороже. */
#define COLL_RING_SAMPLES 8

/* Текущий уровень ног игрока. Точечные запросы высоты по кубам зависят от
 * того, где сейчас ноги: куб считается «землёй», только если его верх не
 * выше ног больше чем на шаг, а «потолком» — если его низ выше ног. */
static float g_feet_y = 0.0f;

void coll_set_feet_y(float feet_y) {
    g_feet_y = feet_y;
}

/* Высота рельефа под точкой. На пользовательской карте рельефа нет — пол. */
static float terrain_height(float x, float z) {
    if (map_is_custom()) {
        return COLL_MAP_FLOOR;
    }
    return gen_sample_height(x, z);
}

/* Максимальная высота рельефа в «следе» коллайдера (центр + кольцо точек).
 * Берём максимум, чтобы игрок стоял на самой высокой точке под собой и не
 * проваливался в склон или стену. */
static float terrain_height_around(float cx, float cz, float radius) {
    if (map_is_custom()) {
        return COLL_MAP_FLOOR;
    }

    float highest = gen_sample_height(cx, cz);
    for (int i = 0; i < COLL_RING_SAMPLES; i++) {
        const float a = (float)i / (float)COLL_RING_SAMPLES * COLL_TWO_PI;
        const float px = cx + cosf(a) * radius;
        const float pz = cz + sinf(a) * radius;
        const float h = gen_sample_height(px, pz);
        if (h > highest) {
            highest = h;
        }
    }
    return highest;
}

/* Рельеф в точке — стена для ног на высоте bottom_y, если земля там выше
 * ног больше чем на COLL_STEP_HEIGHT (подняться на такую ступень нельзя). */
static int terrain_point_blocked(float px, float pz, float bottom_y) {
    if (map_is_custom()) {
        return 0;
    }
    const float g = gen_sample_height(px, pz);
    return g > bottom_y + COLL_STEP_HEIGHT + COLL_EPSILON;
}

/* Упирается ли коллайдер в рельеф: проверяем центр и кольцо точек. */
static int terrain_capsule_blocked(float cx, float cz, float feet_y) {
    if (map_is_custom()) {
        return 0;
    }

    if (terrain_point_blocked(cx, cz, feet_y)) {
        return 1;
    }
    for (int i = 0; i < COLL_RING_SAMPLES; i++) {
        const float a = (float)i / (float)COLL_RING_SAMPLES * COLL_TWO_PI;
        const float px = cx + cosf(a) * COLL_RADIUS;
        const float pz = cz + sinf(a) * COLL_RADIUS;
        if (terrain_point_blocked(px, pz, feet_y)) {
            return 1;
        }
    }
    return 0;
}

float coll_ground_height(float x, float z) {
    const float base = terrain_height(x, z);
    /* Куб под точкой перекрывает рельеф, если стоит на нём (верх не выше
     * ног + шаг); иначе остаётся высота рельефа. */
    return map_ground_height(x, z, g_feet_y, base);
}

float coll_player_ground_height(float cx, float cz) {
    const float base = terrain_height_around(cx, cz, COLL_RADIUS);
    return map_cylinder_ground_height(cx, cz, COLL_RADIUS, g_feet_y, base);
}

float coll_ceiling_height(float x, float z) {
    /* У рельефа потолка нет — только нижние грани кубов над ногами. */
    return map_ceiling_height(x, z, g_feet_y);
}

float coll_player_ceiling_height(float cx, float cz) {
    return map_cylinder_ceiling_height(cx, cz, COLL_RADIUS, g_feet_y);
}

int coll_point_blocked(float px, float pz, float bottom_y) {
    if (terrain_point_blocked(px, pz, bottom_y)) {
        return 1;
    }
    return map_point_blocked(px, pz, bottom_y);
}

int coll_capsule_blocked(float cx, float cz, float feet_y) {
    if (terrain_capsule_blocked(cx, cz, feet_y)) {
        return 1;
    }
    return map_capsule_blocked(cx, cz, feet_y);
}

void coll_move(float *x, float *z, float dx, float dz, float feet_y) {
    /* Сначала пробуем полный шаг. */
    const float nx = *x + dx;
    const float nz = *z + dz;
    if (!coll_capsule_blocked(nx, nz, feet_y)) {
        *x = nx;
        *z = nz;
        return;
    }

    /* Уперлись — пробуем оси по отдельности, чтобы скользить вдоль стены.
     * Ось X отдельно... */
    if (dx != 0.0f && !coll_capsule_blocked(*x + dx, *z, feet_y)) {
        *x += dx;
    }
    /* ...затем ось Z (уже с учётом сдвига по X). */
    if (dz != 0.0f && !coll_capsule_blocked(*x, *z + dz, feet_y)) {
        *z += dz;
    }
}

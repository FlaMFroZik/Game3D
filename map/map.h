#ifndef MAP_H
#define MAP_H

#include <stddef.h>

/* Объект куба (осепараллельный параллелепипед / AABB)
 * Позиция (x, y, z) и размеры (sx, sy, sz)
 */
typedef struct {
    float x, y, z;
    float sx, sy, sz;
} MapCube;

typedef struct {
    MapCube *cubes;
    size_t count;
    size_t capacity;
    int is_loaded; /* 1 если карта была успешно загружена из файла, 0 иначе */
} Map;

/* Глобальный экземпляр карты мира */
extern Map g_map;

/* Загрузка карты из текстового файла.
 * Формат строки: x y z sx sy sz
 * Пропускаются пустые строки и строки, начинающиеся с '#'
 * Возвращает 1 при успехе, 0 при ошибке.
 */
int map_load(const char *filename);

/* Инициализация карты в пустом состоянии */
void map_init(void);

/* Освобождение ресурсов карты */
void map_free(void);

/* Проверка, загружена ли пользовательская карта */
int map_is_custom(void);

/* Отрисовка всех объектов карты */
void map_render(void);

/* Коллизия: проверка попадания точки внутрь кубов карты (с учетом высоты)
 * Возвращает 1, если точка заблокирована кубом, 0 иначе.
 */
int map_point_blocked(float px, float pz, float bottom_y);

/* Высота верхней грани самого высокого куба под точкой (px, pz),
 * находящегося не выше max_y (или вообще под ногами игрока).
 * Если под точкой нет кубов, возвращает default_y.
 */
float map_ground_height(float px, float pz, float current_feet_y, float default_y);

#endif /* MAP_H */

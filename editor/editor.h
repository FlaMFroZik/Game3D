#ifndef EDITOR_H
#define EDITOR_H

/* ------------------------------------------------------------------
 * Редактор карт: свободная noclip-камера (без коллизий с картой),
 * прицеливание лучом из центра экрана, установка и удаление кубов,
 * сохранение в .tfm.
 *
 * Модуль читает ввод через render/window.h и правит глобальную карту
 * map/map.h. Меню и HUD рисует main.c — редактору хватает мира.
 * ------------------------------------------------------------------ */

#include <stddef.h>

#include "map/list.h"     /* MAP_LIST_PATH_MAX */
#include "map/map.h"
#include "render/render.h"

#define EDITOR_REACH      48.0f  /* дальность луча прицела, метров */
#define EDITOR_BRUSH_MIN   1.0f
#define EDITOR_BRUSH_MAX  32.0f
#define EDITOR_GRID_HALF  48     /* половина стороны видимой сетки, клеток */

typedef struct {
    Camera cam;

    /* Кисть: размеры нового куба (сечение w x w, высота h). */
    float brush_w;
    float brush_h;

    /* Материал новых кубов. NULL — текстура по умолчанию. */
    const Texture *material;
    float mat_tile;
    MapUvMode mat_uv;

    int grid_on;   /* сетка на полу y = 0 */
    int dirty;     /* есть несохранённые изменения */

    char file[MAP_LIST_PATH_MAX];   /* куда сохранять; "" — имя ещё не выбрано */

    /* Результат прицеливания текущего кадра (editor_update). */
    int   has_hit;     /* луч попал в куб или в пол */
    int   hit_cube;    /* индекс куба или -1, если попали в пол y = 0 */
    float place_x, place_y, place_z;   /* позиция нового куба (мин. угол) */
} Editor;

/* Новая сессия: пустая карта, камера над началом координат. */
void editor_init(Editor *ed);

/* Сессия поверх уже загруженной map_load карты. */
void editor_init_loaded(Editor *ed, const char *file);

/* Кадр редактора: noclip-движение и обзор камеры (кубы не блокируют
 * движение), колесо — размер кисти, G — сетка, прицеливание луча.
 * Клики не обрабатывает — их решает main. */
void editor_update(Editor *ed, double dt);

/* Ставит куб кисти в прицел. 1 — куб добавлен. */
int editor_place(Editor *ed);

/* Удаляет куб под прицелом. 1 — куб удалён. */
int editor_remove(Editor *ed);

/* «Пипетка»: копирует материал куба под прицелом в кисть. */
int editor_pick_material(Editor *ed);

/* Сетка, подсветка куба под прицелом и «призрак» нового куба.
 * Вызывать после render_world, пока включены глубина и туман. */
void editor_draw_gizmos(const Editor *ed);

/* Сохраняет карту в ed->file; пустое имя — придумывает свободное
 * mapNN.tfm рядом с редактором. 1 — успех (ed->dirty сброшен). */
int editor_save(Editor *ed);

/* Имя материала кисти для HUD: имя файла текстуры или "ПО УМОЛЧАНИЮ". */
const char *editor_material_name(const Editor *ed);

#endif /* EDITOR_H */

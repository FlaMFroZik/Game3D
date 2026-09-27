#ifndef EDITOR_H
#define EDITOR_H

/* ------------------------------------------------------------------
 * Редактор карт в стиле Valve Hammer Editor (Worldcraft) / Source / GoldSrc.
 *
 * 4 видовых экрана (2x2): Top (x/z), Front (x/y), Side (z/y), 3D Camera.
 * Инструменты: Selection (Shift+S), Block Tool (Shift+B), Camera (Shift+C),
 * Face Edit (Shift+A), Apply Texture (Shift+T), Zoom (Shift+G), Clip (Shift+X),
 * Hollow Brush. Сетка с шагом от 0.125 до 64 м, привязка, диалоги F9/Alt+P.
 * ------------------------------------------------------------------ */

#include <stddef.h>

#include "map/list.h"
#include "map/map.h"
#include "render/prim.h"
#include "render/render.h"
#include "render/ui.h"

#define EDITOR_MAX_UNDO 32
#define EDITOR_GRID_SIZES_COUNT 10

/* Инструменты редактора Hammer */
typedef enum {
    TOOL_SELECT = 0,   /* Выбор и трансформация (Shift+S) */
    TOOL_ZOOM,         /* Масштабирование вида (Shift+G) */
    TOOL_CAMERA,       /* Позиция и цель 3D-камеры (Shift+C) */
    TOOL_ENTITY,       /* Размещение сущностей (Shift+E) */
    TOOL_BLOCK,        /* Создание брашей/кубов (Shift+B) */
    TOOL_TEXTURE,      /* Настройка текстур и граней (Shift+A) */
    TOOL_APPLY_TEX,    /* Быстрое наложение текстуры (Shift+T) */
    TOOL_CLIP,         /* Отсечение/срез браша (Shift+X) */
    TOOL_HOLLOW,       /* Полая комната */
    TOOL_COUNT
} EditorTool;

/* Примитивы создания брашей */
typedef enum {
    PRIM_BLOCK = 0,    /* Обычный куб/параллелепипед */
    PRIM_WEDGE,        /* Клин / наклонная рампа */
    PRIM_CYLINDER,     /* Цилиндр (8-гранная колонна) */
    PRIM_ARCH,         /* Арка */
    PRIM_ROOM          /* Полая комната */
} BrushPrimitive;

/* Видовые экраны */
typedef enum {
    VIEWPORT_TOP = 0,  /* Top (x/z) — 2D вид сверху */
    VIEWPORT_3D,       /* 3D View — 3D перспектива */
    VIEWPORT_FRONT,    /* Front (x/y) — 2D вид спереди */
    VIEWPORT_SIDE,     /* Side (z/y) — 2D вид сбоку */
    VIEWPORT_COUNT
} ViewportIndex;

/* Режимы 3D-рендера */
typedef enum {
    RENDER_3D_TEXTURED = 0,
    RENDER_3D_FLAT,
    RENDER_3D_WIREFRAME
} Render3DMode;

/* Диалоговые окна / модали */
typedef enum {
    MODAL_NONE = 0,
    MODAL_TEXTURE_BROWSER,
    MODAL_RUN_MAP,          /* Компиляция карты (F9) */
    MODAL_CHECK_PROBLEMS,   /* Проверка карты (Alt+P) */
    MODAL_MAP_INFO,         /* Информация о карте */
    MODAL_ABOUT_HAMMER,     /* О программе */
    MODAL_OPEN_MAP,         /* Открыть карту */
    MODAL_SAVE_AS,          /* Сохранить как */
    MODAL_HOLLOW_SETTINGS   /* Настройки полой комнаты */
} EditorModal;

/* Меню верхнего бара */
typedef enum {
    MENU_NONE = 0,
    MENU_FILE,
    MENU_EDIT,
    MENU_MAP,
    MENU_VIEW,
    MENU_TOOLS,
    MENU_HELP
} ActiveMenu;

/* Камера 2D-вида */
typedef struct {
    float pan_x, pan_y;  /* центр вида в мировых координатах */
    float zoom;          /* пикселей на метр */
} View2D;

/* Состояние редактора */
typedef struct {
    Camera cam;                  /* 3D-камера noclip */
    int freelook_3d;             /* режим свободного обзора 3D (клавиша Z) */

    View2D views2d[4];           /* параметры камер для видовых экранов */
    int active_viewport;         /* активный видовой экран (0..3) */
    int maximized_viewport;      /* -1 (2x2 сетка) или 0..3 (развернут на весь экран) */

    EditorTool tool;             /* текущий инструмент */
    BrushPrimitive primitive;    /* выбранный тип примитива */
    Render3DMode render_3d_mode; /* режим отображения 3D */

    /* Создание браша (Block Tool) */
    int   preview_active;
    float preview_min[3];
    float preview_max[3];
    int   preview_drag_view;     /* в каком 2D-виде начали тянуть */

    /* Выделение */
    int selected_cube;           /* индекс выделенного куба (-1 если нет) */
    int hovered_cube;            /* куб под курсором в 3D */
    int is_dragging;             /* перетаскивание выделения или ручки */
    int drag_handle;             /* 0..7 — ручка ресайза, 8 — перемещение всего браша */
    float drag_start_mouse[2];
    float drag_orig_min[3];
    float drag_orig_max[3];

    /* Сетка */
    float grid_size;             /* размер шага сетки в метрах */
    int   grid_index;            /* индекс в таблице размеров сетки */
    int   grid_snap;             /* привязка к сетке (1/0) */
    int   grid_visible;          /* показывать сетку (1/0) */

    /* Активный материал */
    const Texture *material;
    char  material_name[128];
    float mat_tile;
    MapUvMode mat_uv;

    /* Файл и статус */
    int dirty;
    char file[MAP_LIST_PATH_MAX];
    char status_msg[256];
    char coord_str[128];
    char size_str[128];

    /* Меню и модали */
    ActiveMenu active_menu;
    EditorModal modal;
    char modal_input[256];
    char tex_filter[64];
    int  tex_scroll;
    int  map_scroll;

    /* Компилятор F9 */
    int compile_stage;           /* 0 - готов, 1 - vbsp, 2 - vvis, 3 - vrad, 4 - готово */
    double compile_timer;
    char compile_log[2048];

    /* Проверка карты Alt+P */
    int problem_count;
    char problem_list[8][128];

    /* Параметры полой комнаты */
    float hollow_thickness;

    /* История Undo / Redo */
    struct {
        MapCube *cubes;
        size_t count;
    } undo_stack[EDITOR_MAX_UNDO];
    int undo_count;
    int undo_index;

    /* Карты рядом с редактором */
    MapList map_list;
} Editor;

/* Инициализация редактора */
void editor_init(Editor *ed);
void editor_init_loaded(Editor *ed, const char *file);
void editor_shutdown(Editor *ed);

/* Кадр обновления (ввод, камера, навигация) */
void editor_update(Editor *ed, double dt, int win_w, int win_h);

/* Отрисовка всех видовых экранов и мира */
void editor_render_viewports(Editor *ed, const Renderer *r, int win_w, int win_h);

/* Отрисовка интерфейса Hammer (меню, тулбары, доки, статус-бар, модали) */
void editor_render_ui(Editor *ed, Ui *ui);

/* Операции с картой */
int  editor_save(Editor *ed);
int  editor_save_as(Editor *ed, const char *path);
int  editor_open(Editor *ed, const char *path);
void editor_new_map(Editor *ed);

void editor_push_undo(Editor *ed);
void editor_undo(Editor *ed);
void editor_redo(Editor *ed);

void editor_create_brush(Editor *ed);
void editor_delete_selected(Editor *ed);
void editor_duplicate_selected(Editor *ed);
void editor_hollow_selected(Editor *ed);
void editor_apply_material_to_selected(Editor *ed);
void editor_pick_material_from_selected(Editor *ed);
void editor_select_all(Editor *ed);
void editor_clear_selection(Editor *ed);

void editor_set_tool(Editor *ed, EditorTool tool);
void editor_grid_smaller(Editor *ed);
void editor_grid_larger(Editor *ed);
void editor_toggle_snap(Editor *ed);
void editor_toggle_maximize(Editor *ed);
void editor_toggle_3d_freelook(Editor *ed, void *window);

void editor_run_compile(Editor *ed);
void editor_check_problems(Editor *ed);

const char *editor_material_name(const Editor *ed);

#endif /* EDITOR_H */

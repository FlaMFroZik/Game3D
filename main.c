#include <stdio.h>
#include <string.h>

#include "editor/editor.h"
#include "map/list.h"
#include "map/map.h"
#include "render/font.h"
#include "render/render.h"
#include "render/ui.h"
#include "render/window.h"

/* ------------------------------------------------------------------
 * Game3D — редактор карт.
 *
 * Свободная камера, кубы ставятся и удаляются мышью, карта
 * сохраняется в текстовый формат .tfm (см. README.md). Сама игра
 * этот файл потом просто загружает.
 * ------------------------------------------------------------------ */

/* ---------- Параметры ---------- */

#define WINDOW_WIDTH  1024
#define WINDOW_HEIGHT 640
#define MAX_FRAME_DELTA 0.1   /* защита от «скачка» после зависания */

/* Шрифт интерфейса ищем рядом с редактором, а затем в текущем каталоге —
 * так меню работает и из собранного каталога, и из дерева исходников. */
#define FONT_DIR  "assets/fonts"
#define FONT_FILE "DejaVuSans.ttf"

/* ---------- Экраны ---------- */

typedef enum {
    SCREEN_MAIN_MENU = 0,   /* «Новая карта», «Открыть карту», «Выйти» */
    SCREEN_MAP_LIST,        /* список карт рядом с редактором */
    SCREEN_EDITING,         /* мир, курсор захвачен */
    SCREEN_EDIT_MENU,       /* Esc в редакторе: продолжить/сохранить/выйти */
    SCREEN_QUIT             /* не рисуется: сигнал выйти из цикла */
} Screen;

/* ---------- Размеры меню ----------
 * Заданы для окна высотой 600 px и умножаются на масштаб интерфейса,
 * поэтому на большом окне меню просто крупнее. */

#define MENU_PANEL_W        360.0f
#define MENU_PANEL_PAD       24.0f
#define MENU_BUTTON_H        44.0f
#define MENU_BUTTON_GAP      12.0f
#define MENU_MAIN_PANEL_H   300.0f
#define MENU_EDIT_PANEL_H   380.0f
#define MENU_TITLE_SCALE      1.6f
#define MENU_HEAD_SCALE       1.3f
#define MENU_EDGE(s)          (((s) > 1.5f) ? 2.0f : 1.0f)

#define MAP_PANEL_W         460.0f
#define MAP_PANEL_MAX_H     460.0f
#define MAP_PANEL_MARGIN     40.0f   /* сколько экрана оставить сверху и снизу */
#define MAP_HEAD_H           70.0f   /* отступ под заголовок и кнопку «X» */
#define MAP_ROW_H            36.0f
#define MAP_CLOSE_SIZE       28.0f
#define MAP_LIST_SCROLL_HINT "КРУТИТЕ КОЛЕСО МЫШИ"

/* Цвета HUD поверх мира. */
static const float HUD_TEXT[4]  = { 1.0f, 1.0f, 1.0f, 0.95f };
static const float HUD_DIM[4]   = { 1.0f, 1.0f, 1.0f, 0.60f };
static const float HUD_CROSS[4] = { 1.0f, 1.0f, 1.0f, 0.85f };
static const float HUD_SHADE[4] = { 0.0f, 0.0f, 0.0f, 0.35f };

/* Сообщение в меню редактора: результат последнего сохранения. */
static char save_status[192];

/* ---------- Вспомогательное ---------- */

/* Собирает кадр интерфейса: размер окна, курсор, клик и колесо мыши. */
static void menu_frame_begin(Ui *ui, const WinWindow *window, const Font *font) {
    int width, height;
    win_size(window, &width, &height);

    ui_frame_begin(ui, font, width, height);
    win_pointer_pixels(window, &ui->pointer_x, &ui->pointer_y);
    ui->clicked = win_mouse_clicked(0);
    ui->wheel = (float)win_scroll_delta();
}

/* Кадр мира без обмена буферами. */
static void draw_world(Renderer *renderer, const WinWindow *window,
                       const Editor *ed, int with_gizmos) {
    int width, height;
    win_size(window, &width, &height);

    render_clear(renderer);
    render_camera(renderer, &ed->cam, width, height);
    render_world(renderer);
    if (with_gizmos) {
        editor_draw_gizmos(ed);
    }
}

/* Фон экранов без мира: просто небо, чтобы меню не висело на чёрном. */
static void draw_sky(Renderer *renderer, const WinWindow *window) {
    int width, height;
    win_size(window, &width, &height);
    if (width <= 0 || height <= 0) { width = 1; height = 1; }

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    render_clear(renderer);
}

static float smaller(float a, float b) {
    return (a < b) ? a : b;
}

/* Имя файла без каталога — для заголовков и HUD. */
static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    return slash ? slash + 1 : path;
}

/* ---------- HUD редактора ---------- */

static void draw_hud(Ui *ui, const Editor *ed) {
    const float s = ui->scale;
    const float cx = (float)ui->width * 0.5f;
    const float cy = (float)ui->height * 0.5f;

    /* Прицел: тонкий крестик по центру. */
    ui_fill(ui_rect(cx - 9.0f * s, cy - 1.0f * s, 18.0f * s, 2.0f * s), HUD_CROSS);
    ui_fill(ui_rect(cx - 1.0f * s, cy - 9.0f * s, 2.0f * s, 18.0f * s), HUD_CROSS);

    /* Подсказка управления — сверху, мелко. */
    ui_label(ui, 10.0f * s, 8.0f * s, 0.72f, HUD_DIM,
             "ЛКМ ПОСТАВИТЬ   ПКМ УДАЛИТЬ   СКМ ПИПЕТКА   КОЛЕСО ВЫСОТА КИСТИ   "
             "SHIFT+КОЛЕСО СЕЧЕНИЕ   G СЕТКА   ESC МЕНЮ");

    /* Состояние — снизу, на лёгкой подложке, чтобы читалось на любом фоне. */
    const float line_h = ui_text_height(ui, 0.85f);
    const float bar_h = line_h * 2.0f + 18.0f * s;
    ui_fill(ui_rect(0.0f, (float)ui->height - bar_h, (float)ui->width, bar_h),
            HUD_SHADE);

    char line[256];
    const float x = 10.0f * s;
    float y = (float)ui->height - bar_h + 6.0f * s;

    snprintf(line, sizeof(line), "КУБОВ: %d   КИСТЬ: %gX%gX%g   МАТЕРИАЛ: %s",
             (int)g_map.count,
             (double)ed->brush_w, (double)ed->brush_h, (double)ed->brush_w,
             editor_material_name(ed));
    ui_label(ui, x, y, 0.85f, HUD_TEXT, line);

    y += line_h + 4.0f * s;
    snprintf(line, sizeof(line), "ФАЙЛ: %s%s",
             ed->file[0] ? base_name(ed->file) : "НОВАЯ КАРТА",
             ed->dirty ? " *" : "");
    ui_label(ui, x, y, 0.85f, ed->dirty ? HUD_TEXT : HUD_DIM, line);
}

/* ---------- Экраны меню ---------- */

static Screen draw_main_menu(Ui *ui) {
    const float s = ui->scale;
    const UiRect panel = ui_centered(ui, MENU_PANEL_W * s, MENU_MAIN_PANEL_H * s);

    ui_fill(panel, UI_COLOR_PANEL);
    ui_border(panel, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);

    ui_label_in(ui,
                ui_rect(panel.x, panel.y + MENU_PANEL_PAD * s, panel.w, 40.0f * s),
                MENU_TITLE_SCALE, UI_COLOR_TEXT, "РЕДАКТОР КАРТ");
    ui_label_in(ui,
                ui_rect(panel.x, panel.y + (MENU_PANEL_PAD + 42.0f) * s,
                        panel.w, 20.0f * s),
                0.8f, UI_COLOR_TEXT_DIM, "GAME3D");

    const float button_w = panel.w - 2.0f * MENU_PANEL_PAD * s;
    const float button_x = panel.x + MENU_PANEL_PAD * s;
    float y = panel.y + 118.0f * s;

    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "НОВАЯ КАРТА", UI_BUTTON_DEFAULT)) {
        return SCREEN_EDITING;
    }

    y += (MENU_BUTTON_H + MENU_BUTTON_GAP) * s;
    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "ОТКРЫТЬ КАРТУ", UI_BUTTON_DEFAULT)) {
        return SCREEN_MAP_LIST;
    }

    y += (MENU_BUTTON_H + MENU_BUTTON_GAP) * s;
    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "ВЫЙТИ", UI_BUTTON_DANGER)) {
        return SCREEN_QUIT;
    }

    return SCREEN_MAIN_MENU;
}

/* Состояние списка карт: сами файлы и прокрутка списка. */
typedef struct {
    MapList maps;
    int scroll;      /* индекс первой видимой строки */
} MapMenu;

/* Возвращает SCREEN_EDITING и пишет путь к выбранной карте в out;
 * иначе — следующий экран меню. */
static Screen draw_map_list(Ui *ui, MapMenu *menu, char *out, size_t out_size) {
    const float s = ui->scale;
    const float pad = MENU_PANEL_PAD * s;

    const float panel_h = smaller(MAP_PANEL_MAX_H * s,
                                  (float)ui->height - MAP_PANEL_MARGIN * s);
    const UiRect panel = ui_centered(ui, MAP_PANEL_W * s, panel_h);

    ui_fill(panel, UI_COLOR_PANEL);
    ui_border(panel, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);

    ui_label(ui, panel.x + pad, panel.y + pad, MENU_HEAD_SCALE,
             UI_COLOR_TEXT, "ОТКРЫТЬ КАРТУ");

    /* «X» закрывает список и возвращает в главное меню. */
    const float close_size = MAP_CLOSE_SIZE * s;
    const UiRect close = ui_rect(panel.x + panel.w - pad - close_size,
                                 panel.y + pad, close_size, close_size);
    if (ui_button(ui, close, "X", UI_BUTTON_DEFAULT)) {
        return SCREEN_MAIN_MENU;
    }

    const int rows = menu->maps.count;
    const float row_h = MAP_ROW_H * s;
    const float list_top = panel.y + MAP_HEAD_H * s;
    const float list_bottom = panel.y + panel_h - pad;

    int visible = (int)((list_bottom - list_top) / row_h);
    if (visible < 1) visible = 1;
    if (visible > rows) visible = rows;

    if (menu->scroll < 0) menu->scroll = 0;
    if (menu->scroll > rows - visible) menu->scroll = rows - visible;
    menu->scroll -= (int)ui->wheel;
    if (menu->scroll < 0) menu->scroll = 0;
    if (menu->scroll > rows - visible) menu->scroll = rows - visible;

    for (int i = 0; i < visible; i++) {
        const int row = menu->scroll + i;
        const UiRect item = ui_rect(panel.x + pad, list_top + (float)i * row_h,
                                    panel.w - 2.0f * pad, row_h - 4.0f * s);

        if (ui_button(ui, item, menu->maps.names[row], UI_BUTTON_DEFAULT)) {
            map_list_path(&menu->maps, row, out, out_size);
            return SCREEN_EDITING;
        }
    }

    if (rows == 0) {
        const UiRect hint = ui_rect(panel.x + pad, list_top + 8.0f * s,
                                    panel.w - 2.0f * pad, 40.0f * s);
        ui_label(ui, hint.x, hint.y, 0.9f, UI_COLOR_TEXT_DIM,
                 "РЯДОМ С РЕДАКТОРОМ НЕТ ФАЙЛОВ .TFM");
    } else if (rows > visible) {
        ui_label(ui, panel.x + pad, list_bottom + 2.0f * s, 0.8f,
                 UI_COLOR_TEXT_DIM, MAP_LIST_SCROLL_HINT);
    }

    return SCREEN_MAP_LIST;
}

/* Меню редактора (Esc): продолжить, сохранить, в главное меню, выйти. */
static Screen draw_edit_menu(Ui *ui, Editor *ed) {
    const float s = ui->scale;
    const UiRect panel = ui_centered(ui, MENU_PANEL_W * s, MENU_EDIT_PANEL_H * s);

    ui_fill(panel, UI_COLOR_PANEL);
    ui_border(panel, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);

    ui_label_in(ui,
                ui_rect(panel.x, panel.y + MENU_PANEL_PAD * s, panel.w, 32.0f * s),
                MENU_HEAD_SCALE, UI_COLOR_TEXT, "РЕДАКТОР");

    /* Строка состояния: файл и несохранённые изменения. */
    char status[224];
    if (save_status[0] != '\0') {
        snprintf(status, sizeof(status), "%s", save_status);
    } else if (ed->dirty) {
        snprintf(status, sizeof(status), "ЕСТЬ НЕСОХРАНЁННЫЕ ИЗМЕНЕНИЯ");
    } else if (ed->file[0] != '\0') {
        snprintf(status, sizeof(status), "ФАЙЛ: %s", base_name(ed->file));
    } else {
        snprintf(status, sizeof(status), "НОВАЯ КАРТА");
    }
    ui_label_in(ui,
                ui_rect(panel.x, panel.y + (MENU_PANEL_PAD + 40.0f) * s,
                        panel.w, 20.0f * s),
                0.8f, UI_COLOR_TEXT_DIM, status);

    const float button_w = panel.w - 2.0f * MENU_PANEL_PAD * s;
    const float button_x = panel.x + MENU_PANEL_PAD * s;
    float y = panel.y + 110.0f * s;

    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "ПРОДОЛЖИТЬ", UI_BUTTON_DEFAULT)) {
        return SCREEN_EDITING;
    }

    y += (MENU_BUTTON_H + MENU_BUTTON_GAP) * s;
    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "СОХРАНИТЬ", UI_BUTTON_DEFAULT)) {
        if (editor_save(ed)) {
            snprintf(save_status, sizeof(save_status), "СОХРАНЕНО: %s",
                     base_name(ed->file));
        } else {
            snprintf(save_status, sizeof(save_status), "ОШИБКА СОХРАНЕНИЯ");
        }
    }

    y += (MENU_BUTTON_H + MENU_BUTTON_GAP) * s;
    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "В ГЛАВНОЕ МЕНЮ", UI_BUTTON_DEFAULT)) {
        return SCREEN_MAIN_MENU;
    }

    y += (MENU_BUTTON_H + MENU_BUTTON_GAP) * s;
    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "ВЫЙТИ", UI_BUTTON_DANGER)) {
        return SCREEN_QUIT;
    }

    return SCREEN_EDIT_MENU;
}

/* ---------- Поиск шрифта ---------- */

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

/* Путь к шрифту: каталог редактора, затем текущий каталог. Возвращает NULL,
 * если файла нет ни там, ни там (текст тогда не нарисуешь). */
static const char *find_font(char *out, size_t out_size) {
    const char *roots[2];
    roots[0] = map_list_game_dir();
    roots[1] = ".";

    for (int i = 0; i < 2; i++) {
        snprintf(out, out_size, "%s/%s/%s", roots[i], FONT_DIR, FONT_FILE);
        if (file_exists(out)) return out;

        snprintf(out, out_size, "%s/%s", roots[i], FONT_FILE);
        if (file_exists(out)) return out;
    }
    out[0] = '\0';
    return NULL;
}

/* ---------- Редактор ---------- */

int main(int argc, char **argv) {
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [texture-file] [map-file]\n", argv[0]);
        return 1;
    }

    /* Всё опционально: без текстуры кубы рисуются встроенной «шахматкой»,
     * без карты редактор начинает с меню. */
    const char *texture_file = (argc >= 2 && argv[1][0] != '\0') ? argv[1] : NULL;
    const char *map_file = (argc >= 3 && argv[2][0] != '\0') ? argv[2] : NULL;

    WinWindow window;
    if (!win_init(&window, WINDOW_WIDTH, WINDOW_HEIGHT, "Game3D — редактор карт")) {
        return 1;
    }

    Renderer renderer;
    if (!render_init(&renderer, texture_file)) {
        fprintf(stderr, "Failed to create the default texture.\n");
        render_shutdown(&renderer);
        win_shutdown(&window);
        return 1;
    }
    render_setup_gl();

    char font_path[MAP_LIST_PATH_MAX];
    if (!find_font(font_path, sizeof font_path)) {
        fprintf(stderr, "Cannot find font %s/%s next to the editor.\n",
                FONT_DIR, FONT_FILE);
        render_shutdown(&renderer);
        win_shutdown(&window);
        return 1;
    }

    Font *font = font_create(font_path, FONT_PIXEL_SIZE);
    if (!font) {
        render_shutdown(&renderer);
        win_shutdown(&window);
        return 1;
    }

    Screen screen = SCREEN_MAIN_MENU;
    MapMenu map_menu;
    memset(&map_menu, 0, sizeof map_menu);

    Editor ed;
    editor_init(&ed);

    double last_time = win_time_seconds();

    /* Карта в командной строке открывается сразу, минуя меню. */
    if (map_file) {
        if (map_load(map_file)) {
            editor_init_loaded(&ed, map_file);
        } else {
            editor_init(&ed);   /* файла нет — начинаем пустую карту */
        }
        screen = SCREEN_EDITING;
        win_set_mouse_captured(&window, 1);
        win_reset_input();
    }

    while (screen != SCREEN_QUIT && !win_poll(&window)) {
        if (screen == SCREEN_EDITING) {
            double now = win_time_seconds();
            double delta = now - last_time;
            last_time = now;

            if (delta > MAX_FRAME_DELTA) delta = MAX_FRAME_DELTA;
            if (delta < 0.0) delta = 0.0;

            editor_update(&ed, delta);

            /* Мышь: поставить, удалить, «пипетка». */
            if (win_mouse_clicked(0)) editor_place(&ed);
            if (win_mouse_clicked(1)) editor_remove(&ed);
            if (win_mouse_clicked(2)) editor_pick_material(&ed);

            draw_world(&renderer, &window, &ed, 1);

            int width, height;
            win_size(&window, &width, &height);
            Ui ui;
            ui_frame_begin(&ui, font, width, height);
            draw_hud(&ui, &ed);
            ui_frame_end();

            /* Esc открывает меню редактора и возвращает курсор. */
            if (win_escape_pressed()) {
                screen = SCREEN_EDIT_MENU;
                save_status[0] = '\0';
                win_set_mouse_captured(&window, 0);
                win_reset_input();
            }
            win_swap(&window);
            continue;
        }

        /* Дальше — экраны меню: мир либо не создан, либо ждёт за меню. */
        if (screen == SCREEN_EDIT_MENU) {
            draw_world(&renderer, &window, &ed, 0);
        } else {
            draw_sky(&renderer, &window);
        }

        Ui ui;
        menu_frame_begin(&ui, &window, font);

        Screen next = screen;
        char chosen_map[MAP_LIST_PATH_MAX];
        chosen_map[0] = '\0';

        switch (screen) {
            case SCREEN_MAIN_MENU:
                next = draw_main_menu(&ui);
                if (next == SCREEN_MAP_LIST) {
                    /* Каталог читаем заново: карты могли добавить,
                     * пока меню было открыто. */
                    map_list_scan(&map_menu.maps, map_list_game_dir());
                    map_menu.scroll = 0;
                }
                break;
            case SCREEN_MAP_LIST:
                next = draw_map_list(&ui, &map_menu, chosen_map, sizeof chosen_map);
                break;
            case SCREEN_EDIT_MENU:
                next = draw_edit_menu(&ui, &ed);
                break;
            default:
                break;
        }

        ui_frame_end();
        win_swap(&window);

        if (screen == SCREEN_MAIN_MENU && next == SCREEN_EDITING) {
            /* Новая пустая карта. */
            editor_init(&ed);
            save_status[0] = '\0';
        } else if (screen == SCREEN_MAP_LIST && next == SCREEN_EDITING) {
            /* Карта из файла; не читается — остаёмся в списке. */
            if (map_load(chosen_map)) {
                editor_init_loaded(&ed, chosen_map);
                save_status[0] = '\0';
            } else {
                next = SCREEN_MAP_LIST;
            }
        } else if (screen == SCREEN_EDIT_MENU && next == SCREEN_MAIN_MENU) {
            /* Выход из сессии: мир освобождаем, пока контекст жив. */
            map_free();
        }

        /* Esc в меню работает как «назад», а в главном меню — как выход. */
        if (win_escape_pressed()) {
            switch (next) {
                case SCREEN_MAP_LIST:  next = SCREEN_MAIN_MENU; break;
                case SCREEN_EDIT_MENU: next = SCREEN_EDITING;   break;
                case SCREEN_MAIN_MENU: next = SCREEN_QUIT;      break;
                default: break;
            }
        }

        if (next != screen) {
            win_set_mouse_captured(&window, next == SCREEN_EDITING);
            if (next == SCREEN_EDITING) {
                last_time = win_time_seconds();
            }
            win_reset_input();
        }

        screen = next;
    }

    map_free();
    font_destroy(font);
    render_shutdown(&renderer);
    win_shutdown(&window);
    return 0;
}

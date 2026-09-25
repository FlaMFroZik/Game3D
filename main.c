#include <stdio.h>
#include <string.h>

#include "map/gen.h"
#include "map/list.h"
#include "map/map.h"
#include "physics/physics.h"
#include "render/font.h"
#include "render/render.h"
#include "render/ui.h"
#include "render/window.h"

/* ---------- Параметры ---------- */

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define PHYSICS_HZ    60.0
#define PHYSICS_STEP  (1.0 / PHYSICS_HZ)
#define MAX_FRAME_DELTA 0.2   /* защита от «скачка» после зависания */

/* Шрифт интерфейса ищем рядом с игрой, а затем в текущем каталоге —
 * так меню работает и из собранного каталога, и из дерева исходников. */
#define FONT_DIR  "assets/fonts"
#define FONT_FILE "DejaVuSans.ttf"

/* ---------- Экраны ---------- */

typedef enum {
    SCREEN_MAIN_MENU = 0,   /* «Начать» и «Выйти» */
    SCREEN_MAP_LIST,        /* список карт рядом с игрой */
    SCREEN_PLAYING,
    SCREEN_PAUSE,           /* Esc в игре: «Закрыть меню» и «Выйти» */
    SCREEN_QUIT             /* не рисуется: сигнал выйти из цикла */
} Screen;

/* ---------- Размеры меню ----------
 * Заданы для окна высотой 600 px и умножаются на масштаб интерфейса,
 * поэтому на большом окне меню просто крупнее. */

#define MENU_PANEL_W        340.0f
#define MENU_PANEL_PAD       24.0f
#define MENU_BUTTON_H        44.0f
#define MENU_BUTTON_GAP      12.0f
#define MENU_MAIN_PANEL_H   250.0f
#define MENU_PAUSE_PANEL_H  230.0f
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

/* ---------- Ввод ---------- */

/* Состояние клавиш превращаем в намерения игрока — physics не знает про GLFW. */
static void read_input(PlayerInput *in) {
    in->forward = (float)(win_key_down(WIN_KEY_W) - win_key_down(WIN_KEY_S));
    in->strafe  = (float)(win_key_down(WIN_KEY_D) - win_key_down(WIN_KEY_A));
    in->look_x  = (float)(win_key_down(WIN_KEY_RIGHT) - win_key_down(WIN_KEY_LEFT));
    in->look_y  = (float)(win_key_down(WIN_KEY_UP) - win_key_down(WIN_KEY_DOWN));
    in->jump    = win_key_down(WIN_KEY_SPACE);
    in->run     = win_key_down(WIN_KEY_SHIFT);
}

/* Собирает кадр интерфейса: размер окна, курсор, клик и колесо мыши. */
static void menu_frame_begin(Ui *ui, const WinWindow *window, const Font *font) {
    int width, height;
    win_size(window, &width, &height);

    ui_frame_begin(ui, font, width, height);
    win_pointer_pixels(window, &ui->pointer_x, &ui->pointer_y);
    ui->clicked = win_mouse_clicked(0);
    ui->wheel = (float)win_scroll_delta();
}

/* ---------- Мир ---------- */

/* Карта из файла; если её нет или она не читается — процедурный мир.
 * Пустой map_file сразу означает процедурную генерацию. */
static void load_world(const char *map_file) {
    map_init();
    if (map_file && map_file[0] != '\0' && !map_load(map_file)) {
        printf("Fallback to procedural terrain generation.\n");
    }
    gen_init();
}

/* Кадр мира без обмена буферами. */
static void draw_world(Renderer *renderer, const WinWindow *window, const Player *player) {
    int width, height;
    win_size(window, &width, &height);

    /* Генерация — fallback на случай, если карта не загружена;
     * с кастомной картой чанки не нужны ни рендеру, ни физике.
     * Радиус мира берётся от глубины тумана: за ней пиксели всё равно
     * залиты цветом неба, а внутри неё рельеф должен быть целиком —
     * иначе на краю кадра виден обрыв загруженных чанков. */
    if (!map_is_custom()) {
        gen_set_view_radius(render_view_radius(player));
        gen_update_chunks(player->x, player->z);
    }

    render_clear(renderer);
    render_camera(renderer, player, width, height);
    render_world(renderer, player);
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

/* ---------- Экраны меню ---------- */

static Screen draw_main_menu(Ui *ui) {
    const float s = ui->scale;
    const UiRect panel = ui_centered(ui, MENU_PANEL_W * s, MENU_MAIN_PANEL_H * s);

    ui_fill(panel, UI_COLOR_PANEL);
    ui_border(panel, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);

    ui_label_in(ui,
                ui_rect(panel.x, panel.y + MENU_PANEL_PAD * s, panel.w, 40.0f * s),
                MENU_TITLE_SCALE, UI_COLOR_TEXT, "GAME3D");

    const float button_w = panel.w - 2.0f * MENU_PANEL_PAD * s;
    const float button_x = panel.x + MENU_PANEL_PAD * s;
    float y = panel.y + 100.0f * s;

    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "НАЧАТЬ", UI_BUTTON_DEFAULT)) {
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

/* Возвращает SCREEN_PLAYING и пишет путь к карте в out (пустая строка —
 * игра без карты, процедурный мир); иначе — следующий экран меню. */
static Screen draw_map_list(Ui *ui, MapMenu *menu, char *out, size_t out_size) {
    const float s = ui->scale;
    const float pad = MENU_PANEL_PAD * s;

    const float panel_h = smaller(MAP_PANEL_MAX_H * s,
                                  (float)ui->height - MAP_PANEL_MARGIN * s);
    const UiRect panel = ui_centered(ui, MAP_PANEL_W * s, panel_h);

    ui_fill(panel, UI_COLOR_PANEL);
    ui_border(panel, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);

    ui_label(ui, panel.x + pad, panel.y + pad, MENU_HEAD_SCALE,
             UI_COLOR_TEXT, "ВЫБОР КАРТЫ");

    /* «X» закрывает выбор карты и возвращает в главное меню. */
    const float close_size = MAP_CLOSE_SIZE * s;
    const UiRect close = ui_rect(panel.x + panel.w - pad - close_size,
                                 panel.y + pad, close_size, close_size);
    if (ui_button(ui, close, "X", UI_BUTTON_DEFAULT)) {
        return SCREEN_MAIN_MENU;
    }

    /* Строки: сначала игра без карты, затем найденные файлы. */
    const int rows = menu->maps.count + 1;
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
        const char *label = (row == 0) ? "БЕЗ КАРТЫ" : menu->maps.names[row - 1];

        if (ui_button(ui, item, label, UI_BUTTON_DEFAULT)) {
            if (row == 0) {
                out[0] = '\0';
            } else {
                map_list_path(&menu->maps, row - 1, out, out_size);
            }
            return SCREEN_PLAYING;
        }
    }

    if (menu->maps.count == 0) {
        const UiRect hint = ui_rect(panel.x + pad, list_top + row_h + 8.0f * s,
                                    panel.w - 2.0f * pad, 40.0f * s);
        ui_label(ui, hint.x, hint.y, 0.9f, UI_COLOR_TEXT_DIM,
                 "РЯДОМ С ИГРОЙ НЕТ ФАЙЛОВ .MAP И .TXT");
    } else if (rows > visible) {
        ui_label(ui, panel.x + pad, list_bottom + 2.0f * s, 0.8f,
                 UI_COLOR_TEXT_DIM, MAP_LIST_SCROLL_HINT);
    }

    return SCREEN_MAP_LIST;
}

/* Пауза: «Закрыть меню» сверху, «Выйти» внизу. */
static Screen draw_pause(Ui *ui) {
    const float s = ui->scale;
    const UiRect panel = ui_centered(ui, MENU_PANEL_W * s, MENU_PAUSE_PANEL_H * s);

    ui_fill(panel, UI_COLOR_PANEL);
    ui_border(panel, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);

    ui_label_in(ui,
                ui_rect(panel.x, panel.y + MENU_PANEL_PAD * s, panel.w, 32.0f * s),
                MENU_HEAD_SCALE, UI_COLOR_TEXT, "ПАУЗА");

    const float button_w = panel.w - 2.0f * MENU_PANEL_PAD * s;
    const float button_x = panel.x + MENU_PANEL_PAD * s;
    const float top_y = panel.y + 90.0f * s;
    const float bottom_y = panel.y + panel.h - MENU_PANEL_PAD * s - MENU_BUTTON_H * s;

    if (ui_button(ui, ui_rect(button_x, top_y, button_w, MENU_BUTTON_H * s),
                  "ЗАКРЫТЬ МЕНЮ", UI_BUTTON_DEFAULT)) {
        return SCREEN_PLAYING;
    }
    if (ui_button(ui, ui_rect(button_x, bottom_y, button_w, MENU_BUTTON_H * s),
                  "ВЫЙТИ", UI_BUTTON_DANGER)) {
        return SCREEN_MAIN_MENU;
    }

    return SCREEN_PAUSE;
}

/* ---------- Поиск шрифта ---------- */

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

/* Путь к шрифту: каталог игры, затем текущий каталог. Возвращает NULL,
 * если файла нет ни там, ни там (текст в меню тогда не нарисуешь). */
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

/* ---------- Игра ---------- */

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <texture-file> [map-file]\n", argv[0]);
        return 1;
    }

    const char *texture_file = argv[1];
    const char *map_file = (argc >= 3 && argv[2][0] != '\0') ? argv[2] : NULL;

    WinWindow window;
    if (!win_init(&window, WINDOW_WIDTH, WINDOW_HEIGHT, "Game3D")) {
        return 1;
    }

    Renderer renderer;
    if (!render_init(&renderer, texture_file)) {
        fprintf(stderr, "Failed to load texture: %s\n", texture_file);
        render_shutdown(&renderer);
        win_shutdown(&window);
        return 1;
    }
    render_setup_gl();

    char font_path[MAP_LIST_PATH_MAX];
    if (!find_font(font_path, sizeof font_path)) {
        fprintf(stderr, "Cannot find font %s/%s next to the game.\n",
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

    Player player;
    phys_init(&player);

    double last_time = win_time_seconds();
    double accumulator = 0.0;

    /* Карта в командной строке запускает игру сразу, минуя меню. */
    if (map_file) {
        load_world(map_file);
        screen = SCREEN_PLAYING;
    }

    while (screen != SCREEN_QUIT && !win_poll(&window)) {
        if (screen == SCREEN_PLAYING) {
            double now = win_time_seconds();
            double delta = now - last_time;
            last_time = now;

            if (delta > MAX_FRAME_DELTA) delta = MAX_FRAME_DELTA;
            if (delta < 0.0) delta = 0.0;
            accumulator += delta;

            /* Фиксированный шаг физики: поведение не зависит от FPS. */
            while (accumulator >= PHYSICS_STEP) {
                PlayerInput in;
                read_input(&in);
                phys_update(&player, &in, PHYSICS_STEP);
                accumulator -= PHYSICS_STEP;
            }

            draw_world(&renderer, &window, &player);

            /* Esc больше не закрывает игру, а открывает паузу. */
            if (win_escape_pressed()) {
                screen = SCREEN_PAUSE;
                win_reset_input();
            }
            win_swap(&window);
            continue;
        }

        /* Дальше — экраны меню: мир либо не загружен, либо стоит на паузе. */
        if (screen == SCREEN_PAUSE) {
            draw_world(&renderer, &window, &player);
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
                    /* Каталог игры читаем заново: карты могли добавить,
                     * пока меню было открыто. */
                    map_list_scan(&map_menu.maps, map_list_game_dir());
                    map_menu.scroll = 0;
                }
                break;
            case SCREEN_MAP_LIST:
                next = draw_map_list(&ui, &map_menu, chosen_map, sizeof chosen_map);
                break;
            case SCREEN_PAUSE:
                next = draw_pause(&ui);
                break;
            default:
                break;
        }

        ui_frame_end();
        win_swap(&window);

        if (screen == SCREEN_MAP_LIST && next == SCREEN_PLAYING) {
            /* Новая партия: мир из выбранного файла или процедурный. */
            load_world(chosen_map);
            phys_init(&player);
            accumulator = 0.0;
            last_time = win_time_seconds();
            win_reset_input();
        } else if (screen == SCREEN_PAUSE && next == SCREEN_PLAYING) {
            /* Продолжаем ту же партию: мир и игрок остаются на месте. */
            accumulator = 0.0;
            last_time = win_time_seconds();
            win_reset_input();
        } else if (screen == SCREEN_PAUSE && next == SCREEN_MAIN_MENU) {
            /* Выход из партии: мир освобождаем, пока контекст жив. */
            map_free();
            win_reset_input();
        } else if (next != screen) {
            win_reset_input();
        }

        /* Esc в меню работает как кнопка назад, а в главном меню — как выход. */
        if (win_escape_pressed()) {
            switch (next) {
                case SCREEN_MAP_LIST: next = SCREEN_MAIN_MENU; break;
                case SCREEN_PAUSE:    next = SCREEN_PLAYING;   break;
                case SCREEN_MAIN_MENU: next = SCREEN_QUIT;     break;
                default: break;
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

#include <stdio.h>
#include <string.h>

#include "map/gen.h"
#include "map/list.h"
#include "map/map.h"
#include "network/multiplayer.h"
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

/* FPS усредняется на коротком интервале: число остаётся читаемым и не
 * скачет при каждом отдельном кадре. */
#define FPS_UPDATE_INTERVAL 0.5
#define FPS_PANEL_MARGIN     10.0f
#define FPS_PANEL_PAD_X       8.0f
#define FPS_PANEL_PAD_Y       4.0f
#define FPS_TEXT_SCALE        0.85f

/* Шрифт интерфейса ищем рядом с игрой, а затем в текущем каталоге —
 * так меню работает и из собранного каталога, и из дерева исходников. */
#define FONT_DIR  "assets/fonts"
#define FONT_FILE "DejaVuSans.ttf"

/* Список удалённых серверов читается рядом с игрой из этого файла. */
#define SERVER_LIST_FILE "servers.txt"

/* ---------- Экраны ---------- */

typedef enum {
    SCREEN_MAIN_MENU = 0,   /* выбор одиночной или многопользовательской игры */
    SCREEN_MAP_LIST,        /* список карт рядом с игрой */
    SCREEN_MULTIPLAYER,     /* подключение UDP-клиента к внешнему серверу */
    SCREEN_PLAYING,
    SCREEN_PAUSE,           /* Esc в игре: «Закрыть меню» и «Выйти» */
    SCREEN_QUIT             /* не рисуется: сигнал выйти из цикла */
} Screen;

/* ---------- Размеры меню ----------
 * Заданы для окна высотой 600 px и умножаются на масштаб интерфейса,
 * поэтому на большом окне меню просто крупнее. */

#define MENU_PANEL_W        540.0f
#define MENU_PANEL_PAD       24.0f
#define MENU_BUTTON_H        44.0f
#define MENU_BUTTON_GAP      12.0f
#define MENU_MAIN_PANEL_H   300.0f
#define MENU_MULTI_PANEL_H  520.0f
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

#define DIRECT_PANEL_W      460.0f
#define DIRECT_PANEL_H      230.0f
#define DIRECT_INPUT_H       44.0f

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

/* ---------- Счётчик кадров ---------- */

typedef struct {
    double sample_start;
    unsigned int frames;
    int value;
} FpsCounter;

static void fps_init(FpsCounter *counter, double now) {
    counter->sample_start = now;
    counter->frames = 0;
    counter->value = 0;
}

static void fps_update(FpsCounter *counter, double now) {
    counter->frames++;

    const double elapsed = now - counter->sample_start;
    if (elapsed >= FPS_UPDATE_INTERVAL) {
        counter->value = (int)((double)counter->frames / elapsed + 0.5);
        counter->sample_start = now;
        counter->frames = 0;
    }
}

/* Рисуется последним поверх мира и меню, в правом верхнем углу. */
static void draw_fps(const Ui *ui, const FpsCounter *counter) {
    char text[32];
    snprintf(text, sizeof text, "FPS: %d", counter->value);

    const float s = ui->scale;
    const float pad_x = FPS_PANEL_PAD_X * s;
    const float pad_y = FPS_PANEL_PAD_Y * s;
    const float text_w = ui_text_width(ui, text, FPS_TEXT_SCALE);
    const float text_h = ui_text_height(ui, FPS_TEXT_SCALE);
    const float panel_w = text_w + 2.0f * pad_x;
    const float panel_h = text_h + 2.0f * pad_y;
    const float margin = FPS_PANEL_MARGIN * s;
    const UiRect panel = ui_rect((float)ui->width - margin - panel_w,
                                 margin, panel_w, panel_h);

    ui_fill(panel, UI_COLOR_PANEL);
    ui_border(panel, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);
    ui_label(ui, panel.x + pad_x, panel.y + pad_y, FPS_TEXT_SCALE,
             UI_COLOR_TEXT, text);
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
    float y = panel.y + 86.0f * s;

    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "Однопользовательская игра", UI_BUTTON_DEFAULT)) {
        return SCREEN_MAP_LIST;
    }

    y += (MENU_BUTTON_H + MENU_BUTTON_GAP) * s;
    if (ui_button(ui, ui_rect(button_x, y, button_w, MENU_BUTTON_H * s),
                  "Многопользовательская игра", UI_BUTTON_DEFAULT)) {
        return SCREEN_MULTIPLAYER;
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

/* Прокрутка списка серверов LAN. Адреса появляются только после строгой
 * проверки ответа [1] в network/multiplayer.c. */
typedef struct {
    int scroll;
    int direct_open;
    char direct_endpoint[MULTIPLAYER_ENDPOINT_MAX + 1];
} ServerMenu;

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

/* Адрес сервера состоит только из ASCII: IPv4/IPv6, имя хоста и порт.
 * Пробелы и прочие знаки не вводим, чтобы поле сразу содержало endpoint,
 * который можно безопасно передать getaddrinfo. */
static int direct_endpoint_character(int codepoint) {
    return (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= '0' && codepoint <= '9') ||
           codepoint == '.' || codepoint == ':' || codepoint == '-' ||
           codepoint == '_' || codepoint == '[' || codepoint == ']' ||
           codepoint == '%';
}

/* Забирает накопленные события клавиатуры. Enter работает так же, как кнопка
 * «ЗАЙТИ»; Backspace поддерживает удержание благодаря GLFW_REPEAT. */
static int update_direct_endpoint(ServerMenu *menu) {
    int submit = 0;
    int event;

    while ((event = win_text_event()) != 0) {
        size_t length = strlen(menu->direct_endpoint);
        if (event == WIN_TEXT_BACKSPACE) {
            if (length > 0) menu->direct_endpoint[length - 1] = '\0';
        } else if (event == WIN_TEXT_ENTER) {
            submit = 1;
        } else if (direct_endpoint_character(event) &&
                   length < MULTIPLAYER_ENDPOINT_MAX) {
            menu->direct_endpoint[length] = (char)event;
            menu->direct_endpoint[length + 1] = '\0';
        }
    }
    return submit;
}

/* Длинный адрес показываем с конца: порт и последние группы IPv6 при вводе
 * важнее начала, а текст не должен вылезать за рамку поля. */
static const char *direct_endpoint_visible(const Ui *ui, const char *endpoint,
                                           float max_width, char *out,
                                           size_t out_size) {
    const char *start = endpoint;
    if (endpoint[0] == '\0') return "IP:PORT";

    while (start[0] != '\0') {
        snprintf(out, out_size, "%s|", start);
        if (ui_text_width(ui, out, 1.0f) <= max_width) return out;
        start++;
    }
    snprintf(out, out_size, "|");
    return out;
}

/* Сервер не запускается и не поставляется с клиентом. Браузер сначала
 * опрашивает LAN broadcast, потом читает servers.txt рядом с игрой. */
static Screen draw_multiplayer_menu(Ui *ui, MultiplayerClient *client,
                                    MultiplayerServerBrowser *browser,
                                    ServerMenu *menu, const char *server_file,
                                    double now) {
    const float s = ui->scale;
    const float pad = MENU_PANEL_PAD * s;
    const UiRect panel = ui_centered(ui, MENU_PANEL_W * s, MENU_MULTI_PANEL_H * s);
    int server_count = 0;
    const MultiplayerServerEntry *servers =
        multiplayer_server_browser_entries(browser, &server_count);
    const int rows_visible = 6;
    const float row_h = 34.0f * s;
    const float list_top = panel.y + 148.0f * s;
    const int modal_was_open = menu->direct_open;
    const int saved_clicked = ui->clicked;
    char text[160];

    /* Пока открыто прямое подключение, фон остаётся виден, но его кнопки
     * не получают клик сквозь модальное окно. */
    if (modal_was_open) ui->clicked = 0;

    ui_fill(panel, UI_COLOR_PANEL);
    ui_border(panel, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);

    ui_label_in(ui,
                ui_rect(panel.x, panel.y + pad, panel.w, 34.0f * s),
                MENU_HEAD_SCALE, UI_COLOR_TEXT, "МНОГОПОЛЬЗОВАТЕЛЬСКАЯ ИГРА");

    ui_label(ui, panel.x + pad, panel.y + 78.0f * s, 0.8f,
             UI_COLOR_TEXT_DIM, "ПОИСК UDP-СЕРВЕРОВ В ЛОКАЛЬНОЙ СЕТИ");
    ui_label(ui, panel.x + pad, panel.y + 100.0f * s, 0.88f,
             UI_COLOR_TEXT,
             (client->state == MULTIPLAYER_IDLE)
                 ? multiplayer_server_browser_status(browser)
                 : multiplayer_status(client));

    snprintf(text, sizeof text, "ДОСТУПНЫЕ СЕРВЕРЫ: %d", server_count);
    ui_label(ui, panel.x + pad, panel.y + 124.0f * s, 0.82f,
             UI_COLOR_TEXT_DIM, text);

    if (menu->scroll < 0) menu->scroll = 0;
    if (menu->scroll > server_count - rows_visible) {
        menu->scroll = server_count - rows_visible;
    }
    if (menu->scroll < 0) menu->scroll = 0;
    menu->scroll -= (int)ui->wheel;
    if (menu->scroll < 0) menu->scroll = 0;
    if (menu->scroll > server_count - rows_visible) {
        menu->scroll = server_count - rows_visible;
    }

    if (server_count == 0) {
        ui_label(ui, panel.x + pad, list_top + 10.0f * s, 0.88f,
                 UI_COLOR_TEXT_DIM, "НЕТ КОРРЕКТНЫХ ОТВЕТОВ [1]");
    } else {
        const int visible = (server_count - menu->scroll < rows_visible)
            ? server_count - menu->scroll : rows_visible;
        for (int i = 0; i < visible; i++) {
            const MultiplayerServerEntry *entry = &servers[menu->scroll + i];
            const UiRect row = ui_rect(panel.x + pad,
                                       list_top + (float)i * row_h,
                                       panel.w - 2.0f * pad, row_h - 4.0f * s);

            /* Не даём неподконтрольному имени сервера выйти за края кнопки. */
            snprintf(text, sizeof text, "%.18s  %.9s  %u/%u  %.10s",
                     entry->endpoint, entry->info.name,
                     (unsigned int)entry->info.players,
                     (unsigned int)entry->info.max_players, entry->info.map);
            if (ui_button(ui, row, text, UI_BUTTON_DEFAULT)) {
                multiplayer_server_browser_stop(browser);
                (void)multiplayer_connect(client, entry->endpoint, now);
            }
        }
        if (server_count > rows_visible) {
            ui_label(ui, panel.x + pad, list_top + rows_visible * row_h + 2.0f * s,
                     0.72f, UI_COLOR_TEXT_DIM, MAP_LIST_SCROLL_HINT);
        }
    }

    ui_label(ui, panel.x + pad, panel.y + 382.0f * s, 0.76f,
             UI_COLOR_TEXT_DIM, "ПОСЛЕ LAN ПРОВЕРЯЕТСЯ ФАЙЛ SERVERS.TXT");
    const float button_w = panel.w - 2.0f * pad;
    const float button_x = panel.x + pad;

    if (ui_button(ui, ui_rect(button_x, panel.y + 400.0f * s,
                              button_w, MENU_BUTTON_H * s),
                  "ПОДКЛЮЧИТЬСЯ НАПРЯМУЮ", UI_BUTTON_DEFAULT)) {
        menu->direct_open = 1;
        menu->direct_endpoint[0] = '\0';
        /* Не переносим в поле символы, набранные до его открытия. */
        win_reset_input();
        /* Открывающий клик не должен сразу нажать элемент модального окна. */
        ui->clicked = 0;
    }

    const float footer_y = panel.y + panel.h - pad - MENU_BUTTON_H * s;
    const float footer_gap = 10.0f * s;
    const float footer_w = (button_w - footer_gap) * 0.5f;
    if (ui_button(ui, ui_rect(button_x, footer_y, footer_w, MENU_BUTTON_H * s),
                  "ОБНОВИТЬ", UI_BUTTON_DEFAULT)) {
        multiplayer_disconnect(client);
        menu->scroll = 0;
        (void)multiplayer_server_browser_start(browser, server_file, now);
    }
    if (ui_button(ui, ui_rect(button_x + footer_w + footer_gap, footer_y,
                              footer_w, MENU_BUTTON_H * s),
                  "НАЗАД", UI_BUTTON_DANGER)) {
        return SCREEN_MAIN_MENU;
    }

    if (menu->direct_open) {
        static const float shade[4] = {0.0f, 0.0f, 0.0f, 0.55f};
        char input_text[MULTIPLAYER_ENDPOINT_MAX + 2];
        const UiRect direct = ui_centered(ui, DIRECT_PANEL_W * s,
                                         DIRECT_PANEL_H * s);
        const float direct_pad = MENU_PANEL_PAD * s;
        const float close_size = MAP_CLOSE_SIZE * s;
        const UiRect close = ui_rect(direct.x + direct.w - direct_pad - close_size,
                                     direct.y + direct_pad, close_size, close_size);
        const UiRect input = ui_rect(direct.x + direct_pad,
                                     direct.y + 88.0f * s,
                                     direct.w - 2.0f * direct_pad,
                                     DIRECT_INPUT_H * s);
        const UiRect join = ui_rect(direct.x + direct_pad,
                                    direct.y + direct.h - direct_pad -
                                        MENU_BUTTON_H * s,
                                    direct.w - 2.0f * direct_pad,
                                    MENU_BUTTON_H * s);
        int submit;

        if (modal_was_open) ui->clicked = saved_clicked;
        submit = update_direct_endpoint(menu);

        ui_fill(ui_rect(0.0f, 0.0f, (float)ui->width, (float)ui->height), shade);
        ui_fill(direct, UI_COLOR_PANEL);
        ui_border(direct, MENU_EDGE(s), UI_COLOR_PANEL_EDGE);
        ui_label(ui, direct.x + direct_pad, direct.y + direct_pad,
                 MENU_HEAD_SCALE, UI_COLOR_TEXT, "ПРЯМОЕ ПОДКЛЮЧЕНИЕ");

        if (ui_button(ui, close, "X", UI_BUTTON_DEFAULT) ||
            win_escape_pressed()) {
            menu->direct_open = 0;
            win_reset_input();
            return SCREEN_MULTIPLAYER;
        }

        ui_fill(input, UI_COLOR_BUTTON);
        ui_border(input, MENU_EDGE(s), UI_COLOR_BUTTON_EDGE);
        const char *visible = direct_endpoint_visible(
            ui, menu->direct_endpoint, input.w - 20.0f * s,
            input_text, sizeof input_text);
        ui_label(ui, input.x + 10.0f * s,
                 input.y + (input.h - ui_text_height(ui, 1.0f)) * 0.5f,
                 1.0f,
                 menu->direct_endpoint[0] ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM,
                 visible);

        if (ui_button(ui, join, "ЗАЙТИ", UI_BUTTON_DEFAULT)) submit = 1;
        if (submit && menu->direct_endpoint[0] != '\0') {
            multiplayer_server_browser_stop(browser);
            (void)multiplayer_connect(client, menu->direct_endpoint, now);
            menu->direct_open = 0;
            win_reset_input();
        }
    }

    return SCREEN_MULTIPLAYER;
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

/* Пользователь не передаёт адреса и ресурсы в командной строке: текстура
 * встроена, а servers.txt всегда лежит рядом с исполняемым файлом. */
static void server_list_path(char *out, size_t out_size) {
    snprintf(out, out_size, "%s/%s", map_list_game_dir(), SERVER_LIST_FILE);
    if (file_exists(out)) return;
    /* Удобно и для запуска из дерева исходников без сборки. */
    snprintf(out, out_size, "%s", SERVER_LIST_FILE);
}

int main(void) {
    WinWindow window;
    if (!win_init(&window, WINDOW_WIDTH, WINDOW_HEIGHT, "Game3D")) {
        return 1;
    }

    Renderer renderer;
    if (!render_init(&renderer, NULL)) {
        fprintf(stderr, "Failed to create the built-in texture.\n");
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

    char server_file[MULTIPLAYER_SERVER_FILE_MAX + 1];
    server_list_path(server_file, sizeof server_file);

    Screen screen = SCREEN_MAIN_MENU;
    MapMenu map_menu;
    memset(&map_menu, 0, sizeof map_menu);

    MultiplayerClient multiplayer;
    multiplayer_init(&multiplayer);
    MultiplayerServerBrowser server_browser;
    multiplayer_server_browser_init(&server_browser);
    ServerMenu server_menu;
    memset(&server_menu, 0, sizeof server_menu);
    int multiplayer_game = 0;

    Player player;
    phys_init(&player);

    double last_time = win_time_seconds();
    double accumulator = 0.0;
    FpsCounter fps;
    fps_init(&fps, last_time);

    while (screen != SCREEN_QUIT && !win_poll(&window)) {
        const double frame_time = win_time_seconds();
        fps_update(&fps, frame_time);

        if (screen == SCREEN_PLAYING) {
            double delta = frame_time - last_time;
            last_time = frame_time;

            if (delta > MAX_FRAME_DELTA) delta = MAX_FRAME_DELTA;
            if (delta < 0.0) delta = 0.0;
            accumulator += delta;

            /* Фиксированный шаг физики: поведение не зависит от FPS. */
            while (accumulator >= PHYSICS_STEP) {
                PlayerInput in;
                read_input(&in);
                phys_update(&player, &in, PHYSICS_STEP);
                accumulator -= PHYSICS_STEP;

                if (multiplayer_game) {
                    float look_x, look_y, look_z;
                    phys_view_dir(&player, &look_x, &look_y, &look_z);
                    multiplayer_send_transform(&multiplayer,
                                               player.x, player.y, player.z,
                                               look_x, look_y, look_z);
                }
            }

            if (multiplayer_game) {
                multiplayer_update(&multiplayer, frame_time);
                multiplayer_request_visible_players(&multiplayer, frame_time);
            }

            draw_world(&renderer, &window, &player);
            if (multiplayer_game) {
                int remote_count = 0;
                const MultiplayerRemotePlayer *remote =
                    multiplayer_remote_players(&multiplayer, &remote_count);
                render_remote_players(remote, remote_count);
            }

            /* Esc больше не закрывает игру, а открывает паузу. */
            if (win_escape_pressed()) {
                screen = SCREEN_PAUSE;
                win_reset_input();
            }

            int width, height;
            win_size(&window, &width, &height);
            Ui hud;
            ui_frame_begin(&hud, font, width, height);
            draw_fps(&hud, &fps);
            ui_frame_end();

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
                } else if (next == SCREEN_MULTIPLAYER) {
                    server_menu.scroll = 0;
                    (void)multiplayer_server_browser_start(&server_browser, server_file, frame_time);
                }
                break;
            case SCREEN_MAP_LIST:
                next = draw_map_list(&ui, &map_menu, chosen_map, sizeof chosen_map);
                break;
            case SCREEN_MULTIPLAYER:
                multiplayer_server_browser_update(&server_browser, frame_time);
                multiplayer_update(&multiplayer, frame_time);
                next = draw_multiplayer_menu(&ui, &multiplayer, &server_browser,
                                             &server_menu, server_file, frame_time);
                if (multiplayer_is_joined(&multiplayer)) {
                    const MultiplayerServerInfo *info = multiplayer_server_info(&multiplayer);
                    if (info->map[0] != '\0') {
                        snprintf(chosen_map, sizeof chosen_map, "%s", info->map);
                    }
                    multiplayer_server_browser_stop(&server_browser);
                    next = SCREEN_PLAYING;
                }
                break;
            case SCREEN_PAUSE:
                next = draw_pause(&ui);
                break;
            default:
                break;
        }

        draw_fps(&ui, &fps);
        ui_frame_end();
        win_swap(&window);

        if (screen == SCREEN_MAP_LIST && next == SCREEN_PLAYING) {
            /* Новая одиночная партия: карта или процедурный мир. */
            multiplayer_game = 0;
            load_world(chosen_map);
            phys_init(&player);
            accumulator = 0.0;
            last_time = win_time_seconds();
            win_reset_input();
        } else if (screen == SCREEN_MULTIPLAYER && next == SCREEN_PLAYING) {
            /* Карта в [1] — имя уже установленного у клиента файла. Если её
             * нет, load_world оставляет процедурный мир; по UDP карта не
             * скачивается. */
            multiplayer_game = 1;
            load_world(chosen_map);
            phys_init(&player);
            accumulator = 0.0;
            last_time = win_time_seconds();
            win_reset_input();
        } else if (screen == SCREEN_MULTIPLAYER && next == SCREEN_MAIN_MENU) {
            multiplayer_server_browser_stop(&server_browser);
            multiplayer_disconnect(&multiplayer);
            win_reset_input();
        } else if (screen == SCREEN_PAUSE && next == SCREEN_PLAYING) {
            /* Продолжаем ту же партию: мир и игрок остаются на месте. */
            accumulator = 0.0;
            last_time = win_time_seconds();
            win_reset_input();
        } else if (screen == SCREEN_PAUSE && next == SCREEN_MAIN_MENU) {
            /* Выход из партии: мир освобождаем, пока контекст жив. */
            if (multiplayer_game) multiplayer_disconnect(&multiplayer);
            multiplayer_game = 0;
            map_free();
            win_reset_input();
        } else if (next != screen) {
            win_reset_input();
        }

        /* Esc в меню работает как кнопка назад, а в главном меню — как выход. */
        if (win_escape_pressed()) {
            switch (next) {
                case SCREEN_MAP_LIST:
                    next = SCREEN_MAIN_MENU;
                    break;
                case SCREEN_MULTIPLAYER:
                    multiplayer_server_browser_stop(&server_browser);
                    multiplayer_disconnect(&multiplayer);
                    next = SCREEN_MAIN_MENU;
                    break;
                case SCREEN_PAUSE:
                    next = SCREEN_PLAYING;
                    break;
                case SCREEN_MAIN_MENU:
                    next = SCREEN_QUIT;
                    break;
                default:
                    break;
            }
            win_reset_input();
        }

        screen = next;
    }

    multiplayer_server_browser_stop(&server_browser);
    multiplayer_disconnect(&multiplayer);
    map_free();
    font_destroy(font);
    render_shutdown(&renderer);
    win_shutdown(&window);
    return 0;
}

#ifndef UI_H
#define UI_H

/* ------------------------------------------------------------------
 * Интерфейс в стиле Valve Hammer Editor (Worldcraft) / Source / GoldSrc.
 *
 * Растровый шрифт, рельефные 3D-панели (bevel), панели инструментов,
 * заголовки видовых экранов, меню, модальные окна и статус-бар.
 * ------------------------------------------------------------------ */

#include "render/font.h"
#include "render/prim.h"

typedef struct {
    float x, y, w, h;
} UiRect;

typedef struct {
    int width, height;      /* размер окна в пикселях */
    int pointer_x, pointer_y;
    int clicked;            /* левая кнопка нажата в этом кадре */
    int right_clicked;      /* правая кнопка нажата */
    int mouse_down;         /* левая кнопка удерживается */
    int mouse_released;     /* левая кнопка отпущена */
    float wheel;            /* прокрутка колеса */
    float scale;            /* масштаб интерфейса */
    const Font *font;
} Ui;

/* Цвета интерфейса Valve Hammer */
extern const float UI_CLR_BG[4];
extern const float UI_CLR_PANEL[4];
extern const float UI_CLR_PANEL_LIGHT[4];
extern const float UI_CLR_PANEL_DARK[4];
extern const float UI_CLR_SUNKEN[4];
extern const float UI_CLR_SUNKEN_BORDER[4];
extern const float UI_CLR_BTN[4];
extern const float UI_CLR_BTN_HOT[4];
extern const float UI_CLR_BTN_ACTIVE[4];
extern const float UI_CLR_BTN_BORDER_HI[4];
extern const float UI_CLR_BTN_BORDER_LO[4];
extern const float UI_CLR_ACCENT[4];
extern const float UI_CLR_ACCENT_HOVER[4];
extern const float UI_CLR_TEXT[4];
extern const float UI_CLR_TEXT_DIM[4];
extern const float UI_CLR_TEXT_ACCENT[4];
extern const float UI_CLR_TEXT_DISABLED[4];
extern const float UI_CLR_SELECTION[4];
extern const float UI_CLR_VIEWPORT_BORDER[4];
extern const float UI_CLR_VIEWPORT_ACTIVE[4];
extern const float UI_CLR_GRID_GREEN[4];
extern const float UI_CLR_GRID_MAJOR[4];
extern const float UI_CLR_AXIS_X[4];
extern const float UI_CLR_AXIS_Y[4];
extern const float UI_CLR_AXIS_Z[4];

/* Готовит кадр интерфейса: ортогональная проекция в пикселях окна */
void ui_frame_begin(Ui *ui, const Font *font, int width, int height);
void ui_frame_end(void);

/* ---------- Геометрия ---------- */
UiRect ui_rect(float x, float y, float w, float h);
UiRect ui_centered(const Ui *ui, float w, float h);
UiRect ui_inset(UiRect parent, float inset);
int    ui_contains(const Ui *ui, UiRect r);

/* ---------- Примитивы рисования ---------- */
void ui_fill(UiRect r, const float rgba[4]);
void ui_border(UiRect r, float thickness, const float rgba[4]);
void ui_line(float x0, float y0, float x1, float y1, float thickness, const float rgba[4]);
void ui_rect_outline(UiRect r, float thickness, const float rgba[4]);
void ui_draw_handle(float cx, float cy, float size, const float fill[4], const float border[4]);

/* Текст */
void ui_label(const Ui *ui, float x, float y, float scale,
              const float rgba[4], const char *text);
void ui_label_in(const Ui *ui, UiRect r, float scale,
                 const float rgba[4], const char *text);
void ui_label_left(const Ui *ui, UiRect r, float offset_x, float scale,
                   const float rgba[4], const char *text);
void ui_label_right(const Ui *ui, UiRect r, float offset_x, float scale,
                    const float rgba[4], const char *text);

float ui_text_width(const Ui *ui, const char *text, float scale);
float ui_text_height(const Ui *ui, float scale);

/* ---------- Hammer 3D Bevel панели ---------- */
void ui_panel_raised(UiRect r);
void ui_panel_sunken(UiRect r, const float *bg_rgba);
void ui_panel_flat(UiRect r, const float *bg_rgba, const float *border_rgba);
void ui_panel_header(const Ui *ui, UiRect r, const char *title, int active);

/* Заголовок видового окна (например, "[ top (x/z) ]") */
int ui_viewport_tab(Ui *ui, UiRect r, const char *title, int is_active, int is_maximized);

/* Кнопка на панели инструментов (стиль Hammer) */
int ui_button_tool(Ui *ui, UiRect r, const char *icon, const char *shortcut, int active);

/* Стандартная кнопка Hammer */
int ui_button_hammer(Ui *ui, UiRect r, const char *label, int active);
int ui_button_hammer_small(Ui *ui, UiRect r, const char *label);

/* Пункт меню */
int ui_menu_item(Ui *ui, UiRect r, const char *label, const char *shortcut, int enabled);

/* Ячейка статус-бара */
void ui_status_field(const Ui *ui, UiRect r, const char *text, const float *text_rgba);

/* Предпросмотр текстуры */
int ui_texture_preview(Ui *ui, UiRect r, const Texture *tex, const char *name, int selected);

/* Модальное окно (стиль Hammer / Win32) */
void ui_dialog_frame(const Ui *ui, UiRect r, const char *title, int *close_clicked);

/* Числовой степпер (значение с кнопками [-] и [+]) */
int ui_stepper_float(Ui *ui, UiRect r, const char *label, float *value, float step, float min_v, float max_v, const char *fmt);

#endif /* UI_H */

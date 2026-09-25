#ifndef UI_H
#define UI_H

/* ------------------------------------------------------------------
 * Минималистичный интерфейс меню: прямоугольники, рамки и текст.
 *
 * Всё рисуется в пикселях области отрисовки, ось Y направлена вниз —
 * как у координат курсора мыши. Модуль не знает про GLFW: положение
 * курсора и нажатия кладёт в Ui вызывающий код (см. main.c).
 * ------------------------------------------------------------------ */

#include "render/font.h"

typedef struct {
    float x, y, w, h;
} UiRect;

typedef struct {
    int width, height;      /* размер области отрисовки в пикселях */
    int pointer_x, pointer_y;
    int clicked;            /* левая кнопка мыши нажата в этом кадре */
    float wheel;            /* прокрутка колеса за кадр, в «строках» */
    float scale;            /* масштаб интерфейса: 1 при высоте 600 px */
    const Font *font;
} Ui;

/* Палитра меню — одна на все экраны, чтобы кнопки выглядели одинаково. */
extern const float UI_COLOR_PANEL[4];
extern const float UI_COLOR_PANEL_EDGE[4];
extern const float UI_COLOR_BUTTON[4];
extern const float UI_COLOR_BUTTON_HOT[4];
extern const float UI_COLOR_BUTTON_EDGE[4];
extern const float UI_COLOR_DANGER[4];
extern const float UI_COLOR_DANGER_HOT[4];
extern const float UI_COLOR_TEXT[4];
extern const float UI_COLOR_TEXT_DIM[4];

/* Готовит кадр интерфейса: запоминает размер экрана и курсор, включает
 * ортогональную проекцию в пикселях, смешивание и выключает то, что
 * нужно миру (глубина, туман). */
void ui_frame_begin(Ui *ui, const Font *font, int width, int height);

/* Возвращает состояние OpenGL, которое включил render_setup_gl. */
void ui_frame_end(void);

/* ---------- Геометрия ---------- */

UiRect ui_rect(float x, float y, float w, float h);

/* Прямоугольник по центру экрана. */
UiRect ui_centered(const Ui *ui, float w, float h);

/* Прямоугольник, вписанный в parent с отступом inset со всех сторон. */
UiRect ui_inset(UiRect parent, float inset);

int ui_contains(const Ui *ui, UiRect r);

/* ---------- Рисование ---------- */

void ui_fill(UiRect r, const float rgba[4]);
void ui_border(UiRect r, float thickness, const float rgba[4]);

/* Текст: x, y — левый верхний угол строки. scale — размер относительно
 * обычного текста меню (1.0), а не размер в пикселях. */
void ui_label(const Ui *ui, float x, float y, float scale,
              const float rgba[4], const char *text);

/* Текст по центру прямоугольника (по обеим осям). */
void ui_label_in(const Ui *ui, UiRect r, float scale,
                 const float rgba[4], const char *text);

/* Ширина и высота текста в пикселях экрана при масштабе scale. */
float ui_text_width(const Ui *ui, const char *text, float scale);
float ui_text_height(const Ui *ui, float scale);

/* ---------- Кнопки ---------- */

typedef enum {
    UI_BUTTON_DEFAULT = 0,
    UI_BUTTON_DANGER      /* кнопка, которая что-то закрывает или завершает */
} UiButtonStyle;

/* Кнопка с подписью по центру: подсвечивается под курсором и возвращает 1
 * в том кадре, когда по ней кликнули. */
int ui_button(Ui *ui, UiRect r, const char *label, UiButtonStyle style);

#endif /* UI_H */

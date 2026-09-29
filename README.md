# Game3D — Valve Hammer Editor v4.1 (Source / GoldSrc Style)

Редактор уровней и карт для 3D-движка Game3D, полностью переработанный в аутентичном стиле **Valve Hammer Editor (Worldcraft)** из игр на движках **Source / GoldSrc** (Half-Life, Counter-Strike 1.6, Half-Life 2, CS:Source, Portal, TF2).

---

## Особенности стиля Hammer (Source / GoldSrc)

- **4 синхронных видовых экрана (2x2 Quad-View Layout)**:
  - **Top (x/z)** — 2D ортографический вид сверху (оси X и Z, сетка Hammer Green / Major grid);
  - **3D Textured / Flat / Wire** — 3D перспективный вид с текстурами, освещением и noclip-камерой;
  - **Front (x/y)** — 2D ортографический вид спереди (оси X и Y);
  - **Side (z/y)** — 2D ортографический вид сбоку (оси Z и Y).
  - Быстрое переключение и разворачивание любого экрана на весь рабочий стол (`Shift+Z` или кнопка `[+]`/`[-]`).
- **3D Noclip Freelook (клавиша `Z`)**:
  - Нажатие **`Z`** мгновенно захватывает мышь для свободного полета по 3D-сцене (WASD + Space/Ctrl + Shift). Повторное нажатие **`Z`** или **`Esc`** возвращает курсор в режим интерфейса.
- **Инструменты картостроения (Tool Palette)**:
  - **Selection Tool (`Shift+S`)** — выбор брашей, перемещение и масштабирование с помощью 8 угловых и боковых ручек (Resize Handles);
  - **Block Tool (`Shift+B`)** — создание брашей протяжкой рамки в 2D-видах с отображением размеров, подтверждение клавишей **`Enter`**;
  - **Camera Tool (`Shift+C`)** — позиционирование 3D-камеры и лучей обзора в 2D-видах;
  - **Texture Application Tool (`Shift+A`)** — инспектор и настройка материалов, масштаба текстур (`Tile`), режимов `Repeat`/`Stretch`;
  - **Apply Current Texture (`Shift+T`)** — быстрое наложение выбранного материала на выделенный браш;
  - **Magnify / Zoom Tool (`Shift+G`)** — приближение и отдаление 2D-видов;
  - **Clip Tool (`Shift+X`)** — срез брашей;
  - **Hollow Tool** — создание полой комнаты с настраиваемой толщиной стен.
- **Встроенная библиотека текстур Source / GoldSrc**:
  - `tools/toolsnodraw`, `tools/skybox`, `dev/dev_measureorange`, `dev/dev_measuregeneric01`, `brick/brickwall001`, `concrete/concretefloor001`, `metal/metalfloor001`, `wood/woodcrate001`, `hazard/hazard_stripe01`, `tile/tilefloor001`, `stone/stonewall01`, `ground/dirt01`, `checker`.
  - Встроенный **Texture Browser** с визуальной сеткой миниатюр.
- **Сетка и привязка (Grid & Snapping)**:
  - Шаг сетки от 0.125м до 64м (клавиши `[` и `]`);
  - Переключение привязки к сетке (**Snap**);
  - Подсветка координатных осей (красная X, зеленая Y, синяя Z) и центра мира (0,0,0);
  - Отображение конуса обзора 3D-камеры на 2D-экранах.
- **Диалоговые окна и утилиты Hammer**:
  - **Run Map / Compile (клавиша `F9`)** — аутентичное окно компиляции карты (`vbsp`, `vvis`, `vrad`);
  - **Check for Problems (клавиши `Alt+P`)** — проверка карты на ошибки и микро-браши;
  - **Map Information** — статистика солидов, полигонов и вершин;
  - **Full Undo / Redo (`Ctrl+Z` / `Ctrl+Y`)** с глубиной истории до 32 состояний.

---

## Горячие клавиши (Hotkeys)

| Клавиша | Действие |
| --- | --- |
| **Shift + S** | Selection Tool (Выбор и трансформация) |
| **Shift + B** | Block / Brush Tool (Создание браша) |
| **Shift + C** | Camera Tool (Управление 3D-камерой) |
| **Shift + A** | Texture Application / Face Edit (Настройка материалов) |
| **Shift + T** | Apply Current Texture (Наложить текущую текстуру) |
| **Shift + G** | Magnify / Zoom Tool (Масштабирование вида) |
| **Shift + X** | Clip Tool (Срез браша) |
| **Shift + Z** | Развернуть активный видовой экран / 2x2 Quad View |
| **Z** | Переключить режим 3D Freelook (WASD + обзор мышью) |
| **[ / ]** | Уменьшить / увеличить шаг сетки |
| **Enter** | Создать браш из текущего превью Block Tool |
| **Delete / Backspace** | Удалить выделенный браш |
| **Ctrl + D** | Дублировать выделенный браш |
| **Ctrl + Z / Ctrl + Y** | Undo / Redo |
| **Ctrl + S** | Сохранить карту |
| **Ctrl + O** | Открыть карту |
| **Ctrl + N** | Новая карта |
| **Ctrl + A** | Выделить все |
| **F9** | Run Map (Окно компиляции) |
| **Alt + P** | Check for Problems (Проверка ошибок) |
| **ПКМ / СКМ + Drag в 2D** | Панорамирование 2D-вида |
| **Колесо мыши в 2D** | Масштабирование (Zoom) под курсором |
| **Esc** | Снять выделение / закрыть модальное окно / выйти из 3D-обзора |

---

## Сборка

### Linux

```sh
cmake -S . -B build
cmake --build build --config Release
```

### Windows (Visual Studio / Ninja / MinGW)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

---

## Запуск

```sh
./game3d                      # запуск Hammer Editor с новой картой
./game3d map01.tfm            # открыть конкретную карту
./game3d brick.raw map01.tfm  # открыть карту с текстурой по умолчанию
```

---

## Формат карты (.tfm)

Карта сохраняется в текстовом формате `.tfm`, полностью совместимом с игрой и движком:

```text
x y z sx sy sz [tex=<файл|текстура>] [tile=<метры>] [repeat|stretch]
```

Пример:
```text
# Game3D map (.tfm)
0 0 0 16 0.5 16 tex=concrete/concretefloor001 tile=2
0 0.5 0 0.5 4 16 tex=brick/brickwall001 tile=2
15.5 0.5 0 0.5 4 16 tex=brick/brickwall001 tile=2
0.5 0.5 0 15 4 0.5 tex=brick/brickwall001 tile=2
```

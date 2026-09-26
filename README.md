# Game3D

простая 3D стрелялка
версия 2.1

я работаю над быстрой 

## Сборка

Нужно

CMake 3.20 или новее;
компилятор C11 — GCC/Clang на Linux или Visual Studio любой версии наверно начиная с 2022 на Windows;
OpenGL (на Linux — пакет разработки и драйвер видеокарты);
доступ к github.com, если GLFW и stb не установлены в системе (CMake скачает их сам).

Текст в меню рисуется шрифтом `assets/fonts/DejaVuSans.ttf` из репозитория —
CMake копирует его к собранной игре, ничего ставить не нужно.

### Linux

```sh
cmake -S . -B build
cmake --build build --config Release
```

Пакеты разработки, если их ещё нет:

```sh
# Debian/Ubuntu (метапакеты тянут всё нужное)
sudo apt install xorg-dev libglu1-mesa-dev
# Fedora
sudo dnf install libX11-devel libXrandr-devel libXinerama-devel \
                 libXcursor-devel libXi-devel libXext-devel mesa-libGL-devel
# Arch
sudo pacman -S --needed libx11 libxrandr libxinerama libxcursor libxi libxext mesa
```

CMake определяет дистрибутив по `/etc/os-release` и при нехватке заголовков
печатает готовую команду установки — её можно скопировать в терминал как есть.
Для незнакомого дистрибутива вместо команды будет список нужных библиотек.

Пока конфигурация не прошла до конца, `cmake --build build` работать не будет —
Makefile создаётся только на успешной конфигурации.

#### Fedora Atomic: Silverblue, Kinoite, Bazzite

Там `/usr` смонтирован только для чтения и `sudo dnf install` не работает.
Есть два способа поставить пакеты разработки:

```sh
# 1. Слоями в образ системы — нужна перезагрузка:
sudo rpm-ostree install libX11-devel libXrandr-devel libXinerama-devel \
     libXcursor-devel libXi-devel libXext-devel mesa-libGL-devel
systemctl reboot

# 2. Или в контейнере toolbox — система не меняется:
toolbox create -c game3d && toolbox enter -c game3d
sudo dnf install -y cmake gcc libX11-devel libXrandr-devel libXinerama-devel \
     libXcursor-devel libXi-devel libXext-devel mesa-libGL-devel
cmake -S . -B build && cmake --build build --config Release
```

Внутри toolbox CMake видит обычную Fedora и подсказывает обычный
`sudo dnf install`.

#### Бэкенд окна: X11 или Wayland

Если glfw3 не установлен в системе, CMake сам скачает GLFW 3.4 (нужен доступ к
github.com). GLFW 3.4 собирается с двумя бэкендами сразу — X11 и Wayland, — и
каждый требует своих заголовков, поэтому CMake проверяет их заранее и оставляет
в сборке то, что есть в системе:

* есть заголовки X11 (Xlib, Xrandr, Xinerama, Xkb, Xcursor, XInput2, Xshape) —
  собирается X11; на Wayland-сессии игра при этом идёт через XWayland;
* есть только Wayland (`wayland-scanner`, `wayland-client`, `wayland-cursor`,
  `wayland-egl`, `xkbcommon`) — собирается Wayland;
* есть и то и другое — собираются оба, работающий выбирает GLFW во время
  запуска.

Пакеты для Wayland: `wayland-devel libxkbcommon-devel` на Fedora (в Atomic —
тем же способом, что выше) или `libwayland-dev libxkbcommon-dev` на
Debian/Ubuntu; `libwayland-dev` тянет за собой `libwayland-bin` с
`wayland-scanner`. Отдельный `wayland-protocols` не нужен: GLFW 3.4 берёт
протоколы из своего каталога `deps/wayland`.

Перебить выбор можно вручную: `-DGAME3D_GLFW_WAYLAND=ON` (требовать Wayland)
или `-DGAME3D_GLFW_WAYLAND=OFF` (только X11). По умолчанию `AUTO` — по составу
установленных пакетов.

Пакеты разработки нужны только для сборки: собранной игре хватает библиотек,
которые уже есть в системе.

### Windows

Откройте **Developer PowerShell for VS** (с годом версии в конце) и выполните

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Запуск

посмотрите аргументы при запуске

## Форматы и управление

Формат карты пишется в расширении .tfm


```text
x y z sx sy sz [tex=<файл>] [tile=<метры>] [repeat|stretch]
```

Пустые строки и строки, начинающиеся с `#`, игнорируются. Комментарий можно
дописать и в конце строки.

```text
texture stone.bif
tile 2

0 0 0 8 1 8                       # камень, как задано директивой
8 0 0 4 3 4 tex=brick.bif tile=1  # кирпич, одна копия = 1 метр
12 0 0 4 3 4 brick.bif            # то же самое: путь можно писать без ключа
16 0 0 4 3 2 tex=sign.bif stretch # вывеска: одна копия на всю грань
```

| Запись | Что делает |
| --- | --- |
| `texture <файл>` | текстура по умолчанию для всех кубов ниже |
| `tile <метры>` | размер одной копии текстуры по умолчанию, в метрах (0 — 2 м) |
| `repeat` / `stretch` | раскладка по умолчанию для кубов ниже |
| `tex=<файл>` | своя текстура куба (то же — просто путь после размеров) |
| `tile=<метры>` | размер одной копии текстуры на этом кубе |
| `repeat` | рисунок повторяется по миру — по умолчанию |
| `stretch` | одна копия растянута на каждую грань целиком |

Директивы действуют на все кубы ниже: одной строкой `texture` задаётся вся
карта, а `stretch` можно включить для её части. Куб без своей текстуры
рисуется текстурой по умолчанию, а если её в карте нет — той, что передана
игре в командной строке (она же текстура рельефа).

Режим `repeat` продолжает рисунок на соседних кубах: одинаковые кубы встык
выглядят как одна поверхность без швов и стыков. Режим `stretch` нужен там,
где рисунок должен быть виден целиком (табличка, экран, панель).

Путь к текстуре можно писать в кавычках, если в нём есть пробелы

### Управление

- **WASD** — движение;
- **стрелки** — обзор;
- **Shift** — бег;
- **Space** — прыжок;
- **Esc** — меню паузы (в меню — «назад», в главном меню — выход);
- **мышь** — кнопки меню и прокрутка списка карт.

GLFW сообщает клавиши по физическому положению на клавиатуре, поэтому WASD
работает независимо от текущей раскладки.

## Структура

```text
main.c            игровой цикл, экраны меню, ввод -> PlayerInput
map/
    map.c/h       загрузка карты, отрисовка кубов, коллизии
    gen.c/h       процедурная генерация мира и чанков (радиус — по глубине тумана)
    list.c/h      каталог игры и список карт рядом с ней
image.c/h         чтение .raw-текстур
assets/fonts/     шрифт меню (DejaVuSans.ttf) и его лицензия

render/
    window.c/h    окно, контекст OpenGL, ввод и монотонный таймер (GLFW)
    render.c/h    камера, проекция под размер окна, туман и кадр
    prim.c/h      текстуры и геометрия
    texpool.c/h   пул текстур карты (один файл — одна текстура)
    font.c/h      атлас глифов из TrueType (stb_truetype) и вывод текста
    ui.c/h        панели, кнопки и текст меню поверх кадра

physics/
    physics.c/h   движение игрока, гравитация, прыжок и обзор
    coll.c/h      коллизии игрока с рельефом и картой
```

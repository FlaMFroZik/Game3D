# Game3D

Простая 3D-стрелялка на C11. Проект собирается на **Windows и Linux** из одного
исходного дерева: окно, ввод и таймеры работают через GLFW, а OpenGL используется
для отрисовки.

## Сборка без лишних файлов проекта

Нужны:

- CMake 3.20 или новее;
- компилятор C11 — GCC/Clang на Linux или Visual Studio 2022/MinGW на Windows;
- OpenGL (на Linux — пакет разработки и драйвер видеокарты).

GLFW автоматически загружается CMake при первой конфигурации, поэтому вручную
настраивать `.sln`, `.vcxproj` или отдельные Linux-флаги не нужно.

### Linux

Debian/Ubuntu:

```sh
sudo apt install build-essential cmake libgl1-mesa-dev
cmake -S . -B build
cmake --build build --config Release
```

### Windows

Откройте **Developer PowerShell for VS 2022** (или используйте MinGW) и выполните:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Для офлайн-сборки можно установить GLFW системным способом и отключить загрузку:

```sh
cmake -S . -B build -DGAME3D_FETCH_GLFW=OFF
```

`Makefile` оставлен как короткая обёртка над теми же командами CMake для GNU Make.

## Запуск

Игра принимает файл текстуры и необязательный файл карты:

```sh
# Linux или MinGW
./build/game3d texture.raw map.txt

# Visual Studio на Windows
.\build\Release\game3d.exe .\texture.raw .\map.txt
```

Если карта не указана или не найдена, используется процедурная генерация рельефа.
Для запуска без карты достаточно передать только текстуру.

```sh
make run TEXTURE=texture.raw MAP=map.txt
make asan
make clean
```

## Форматы и управление

Формат `.raw` описан в [`image.h`](image.h). Карта — текстовый файл, одна строка
на куб:

```text
x y z sx sy sz
```

Пустые строки и строки, начинающиеся с `#`, игнорируются.

- **WASD** — движение;
- **стрелки** — обзор;
- **Shift** — бег;
- **Space** — прыжок;
- **Esc** — выход.

GLFW сообщает клавиши по физическому положению на клавиатуре, поэтому WASD
работает независимо от текущей раскладки.

## Структура

```text
main.c            игровой цикл, ввод -> PlayerInput
map/
    map.c/h       загрузка карты, отрисовка кубов, коллизии
    gen.c/h       процедурная генерация мира и чанков
image.c/h         чтение .raw-текстур

render/
    window.c/h    окно, контекст OpenGL, ввод и монотонный таймер (GLFW)
    render.c/h    камера, проекция, туман и кадр
    prim.c/h      текстуры и геометрия

physics/
    physics.c/h   движение игрока, гравитация, прыжок и обзор
    coll.c/h      коллизии игрока с рельефом и картой
```

GLU не используется: проекция камеры и загрузка текстуры реализованы через
базовый OpenGL. Это убирает ещё одну платформозависимую зависимость и упрощает
сборку на Windows/Linux.

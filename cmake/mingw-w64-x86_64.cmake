# Toolchain-файл для кросс-компиляции игры под Windows (x86-64) из Linux
# с MinGW-w64. Использование:
#
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
#   cmake --build build-win
#
# Рядом с game3d.exe (в build-win) лежат assets/fonts/DejaVuSans.ttf и
# servers.txt — на Windows копируется весь этот набор.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Кросс-компилятор MinGW-w64 из пакетов дистрибутива:
# Debian/Ubuntu — gcc-mingw-w64-x86-64, Fedora — mingw64-gcc, Arch — mingw-w64-gcc.
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Заголовки и библиотеки ищутся только в окружении MinGW, а не хоста —
# иначе find_library мог бы подхватить линуксовую .so вместо виндовой
# импортной библиотеки. /usr/x86_64-w64-mingw32 — раскладка
# Debian/Ubuntu/Arch, «sys-root/mingw» под ней — Fedora.
set(CMAKE_FIND_ROOT_PATH
    /usr/x86_64-w64-mingw32
    /usr/x86_64-w64-mingw32/sys-root/mingw)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)  # программы (git и пр.) — с хоста
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

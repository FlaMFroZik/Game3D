cmake_minimum_required(VERSION 3.20)

# CMake 3.31+ нормализует пути в install(DESTINATION ...). Включаем новое
# поведение явно: иначе на свежих CMake установка ругается на политику CMP0177,
# а на старых эта строка просто ничего не делает.
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(Game3D VERSION 1.3 LANGUAGES C)

# Тип сборки. У Makefile и Ninja он один на весь каталог и выбирается здесь, на
# конфигурации: `cmake --build build --config Release` для них ничего не значит,
# и без CMAKE_BUILD_TYPE компилятор работает вообще без оптимизаций. Visual
# Studio и другие многоконфигурационные генераторы выбирают конфигурацию сами,
# их не трогаем.
get_property(GAME3D_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT GAME3D_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE не задан — собираем Release")
endif()
if(NOT GAME3D_MULTI_CONFIG)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

option(GAME3D_FETCH_GLFW
       "Download GLFW when a system package is not available"
       ON)
option(GAME3D_FETCH_STB
       "Download stb (single-header libraries) for the menu font"
       ON)
option(GAME3D_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
       OFF)
set(GAME3D_GLFW_WAYLAND "AUTO" CACHE STRING
    "Build GLFW's Wayland backend when GLFW is fetched: AUTO, ON or OFF")
set_property(CACHE GAME3D_GLFW_WAYLAND PROPERTY STRINGS AUTO ON OFF)

set(GAME3D_SOURCES
    main.c
    image.c
    editor/editor.c
    map/list.c
    map/map.c
    render/font.c
    render/prim.c
    render/render.c
    render/texpool.c
    render/ui.c
    render/window.c
)

# Заголовки на сборку не влияют, но, будучи перечисленными в цели, попадают в
# дерево файлов IDE (Visual Studio, CLion, Qt Creator) — иначе там виден только
# список .c.
set(GAME3D_HEADERS
    image.h
    editor/editor.h
    map/list.h
    map/map.h
    physics/coll.h
    render/font.h
    render/prim.h
    render/render.h
    render/texpool.h
    render/ui.h
    render/window.h
)

# Шрифт меню: лежит в репозитории и копируется к собранной игре, чтобы
# exe/ELF находил его рядом с собой (см. find_font в main.c).
set(GAME3D_FONT_FILE DejaVuSans.ttf)
set(GAME3D_FONT_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/${GAME3D_FONT_FILE}")

# ---------------------------------------------------------------------------
# Подсказки про пакеты разработки.
#
# Одни и те же библиотеки называются по-разному: в Fedora это libXrandr-devel, в
# Debian — libxrandr-dev, в Arch — libxrandr, поэтому команда установки
# собирается под ту систему, где идёт сборка, и её можно скопировать из
# сообщения об ошибке в терминал как есть. Отдельный случай — системы на ostree
# (Fedora Atomic: Silverblue, Kinoite, Bazzite и т. п.): /usr там смонтирован
# только для чтения, sudo dnf install не работает, и пакеты ставятся либо в
# контейнер (toolbox, distrobox), либо слоями через rpm-ostree с перезагрузкой.
# Внутри контейнера toolbox всё как в обычной Fedora — это видно по
# /run/.containerenv.
# ---------------------------------------------------------------------------

set(GAME3D_DISTRO "unknown")
set(GAME3D_OSTREE FALSE)     # /usr только для чтения (Fedora Atomic и подобные)
set(GAME3D_CONTAINER FALSE)  # сборка идёт внутри toolbox/distrobox/podman/docker

if(EXISTS "/run/ostree-booted")
    set(GAME3D_OSTREE TRUE)
endif()
if(EXISTS "/run/.containerenv" OR EXISTS "/.dockerenv")
    set(GAME3D_CONTAINER TRUE)
endif()

if(EXISTS "/etc/os-release")
    file(STRINGS "/etc/os-release" _os_release REGEX "^(ID|ID_LIKE)=")
    set(_distro_ids "")
    foreach(_line IN LISTS _os_release)
        string(REGEX REPLACE "^[^=]*=" "" _value "${_line}")
        string(REPLACE "\"" "" _value "${_value}")
        string(TOLOWER "${_value}" _value)
        separate_arguments(_value UNIX_COMMAND "${_value}")
        list(APPEND _distro_ids ${_value})
    endforeach()

    if("fedora" IN_LIST _distro_ids OR "rhel" IN_LIST _distro_ids OR
       "centos" IN_LIST _distro_ids)
        set(GAME3D_DISTRO "fedora")
    elseif("debian" IN_LIST _distro_ids OR "ubuntu" IN_LIST _distro_ids)
        set(GAME3D_DISTRO "debian")
    elseif("arch" IN_LIST _distro_ids OR "archlinux" IN_LIST _distro_ids)
        set(GAME3D_DISTRO "arch")
    elseif("opensuse" IN_LIST _distro_ids OR "suse" IN_LIST _distro_ids)
        set(GAME3D_DISTRO "suse")
    endif()

    unset(_distro_ids)
    unset(_os_release)
endif()

# Имена пакетов для ролей X11/Wayland/OpenGL в текущем дистрибутиве:
# x11, xkb, xrandr, xinerama, xcursor, xi, xext, gl, glfw, wayland, xkbcommon.
# Пустой результат означает, что дистрибутив не опознан.
function(game3d_packages OUT_VAR)
    if(GAME3D_DISTRO STREQUAL "fedora")
        set(_known
            "x11=libX11-devel" "xkb=libX11-devel" "xrandr=libXrandr-devel"
            "xinerama=libXinerama-devel" "xcursor=libXcursor-devel"
            "xi=libXi-devel" "xext=libXext-devel" "gl=mesa-libGL-devel"
            "glfw=glfw-devel" "wayland=wayland-devel"
            "xkbcommon=libxkbcommon-devel")
    elseif(GAME3D_DISTRO STREQUAL "debian")
        set(_known
            "x11=libx11-dev" "xkb=libx11-dev" "xrandr=libxrandr-dev"
            "xinerama=libxinerama-dev" "xcursor=libxcursor-dev"
            "xi=libxi-dev" "xext=libxext-dev" "gl=libgl1-mesa-dev"
            "glfw=libglfw3-dev" "wayland=libwayland-dev"
            "xkbcommon=libxkbcommon-dev")
    elseif(GAME3D_DISTRO STREQUAL "arch")
        set(_known
            "x11=libx11" "xkb=libx11" "xrandr=libxrandr"
            "xinerama=libxinerama" "xcursor=libxcursor" "xi=libxi"
            "xext=libxext" "gl=mesa" "glfw=glfw" "wayland=wayland"
            "xkbcommon=libxkbcommon")
    elseif(GAME3D_DISTRO STREQUAL "suse")
        set(_known
            "x11=libX11-devel" "xkb=libX11-devel" "xrandr=libXrandr-devel"
            "xinerama=libXinerama-devel" "xcursor=libXcursor-devel"
            "xi=libXi-devel" "xext=libXext-devel" "gl=Mesa-libGL-devel"
            "glfw=glfw-devel" "wayland=wayland-devel"
            "xkbcommon=libxkbcommon-devel")
    else()
        set(_known "")
    endif()

    set(_packages "")
    foreach(_role IN LISTS ARGN)
        foreach(_entry IN LISTS _known)
            if(_entry MATCHES "^${_role}=(.*)$")
                list(APPEND _packages "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endforeach()
    # libX11-devel (x11 и xkb) и wayland-devel (wayland и xkbcommon) повторяются.
    list(REMOVE_DUPLICATES _packages)
    set(${OUT_VAR} "${_packages}" PARENT_SCOPE)
endfunction()

# Имя библиотеки, которую закрывает роль, — годится и для списка недостающих
# заголовков, и для подсказки на незнакомом дистрибутиве (там имена пакетов
# ни о чём не говорят).
function(game3d_library_label ROLE OUT_VAR)
    if(ROLE STREQUAL "x11")
        set(_label "libX11")
    elseif(ROLE STREQUAL "xkb")
        set(_label "libX11 (Xkb)")
    elseif(ROLE STREQUAL "xrandr")
        set(_label "libXrandr")
    elseif(ROLE STREQUAL "xinerama")
        set(_label "libXinerama")
    elseif(ROLE STREQUAL "xcursor")
        set(_label "libXcursor")
    elseif(ROLE STREQUAL "xi")
        set(_label "libXi (XInput2)")
    elseif(ROLE STREQUAL "xext")
        set(_label "libXext (Xshape)")
    elseif(ROLE STREQUAL "gl")
        set(_label "OpenGL (Mesa)")
    elseif(ROLE STREQUAL "glfw")
        set(_label "GLFW")
    elseif(ROLE STREQUAL "wayland")
        set(_label "wayland-client, wayland-cursor, wayland-egl, wayland-scanner")
    elseif(ROLE STREQUAL "xkbcommon")
        set(_label "xkbcommon")
    else()
        set(_label "${ROLE}")
    endif()
    set(${OUT_VAR} "${_label}" PARENT_SCOPE)
endfunction()

# Готовая команда установки для перечисленных ролей (с отступом в 4 пробела,
# чтобы её можно было вставить прямо в текст ошибки).
function(game3d_install_hint OUT_VAR)
    game3d_packages(_packages ${ARGN})

    if(NOT _packages)
        set(_labels "")
        foreach(_role IN LISTS ARGN)
            game3d_library_label(${_role} _label)
            list(APPEND _labels "${_label}")
        endforeach()
        list(JOIN _labels ", " _list)
        set(${OUT_VAR} "    нужны пакеты разработки: ${_list}\n" PARENT_SCOPE)
        return()
    endif()

    string(REPLACE ";" " " _list "${_packages}")

    if(GAME3D_DISTRO STREQUAL "fedora")
        if(GAME3D_OSTREE AND NOT GAME3D_CONTAINER)
            # Fedora Atomic: пакеты нельзя поставить в работающую систему.
            string(CONCAT _text
                "    sudo rpm-ostree install ${_list}\n"
                "    systemctl reboot   # rpm-ostree собирает новый образ системы\n"
                "    # либо, не трогая систему, собрать в контейнере toolbox:\n"
                "    toolbox create -c game3d && toolbox enter -c game3d\n"
                "    sudo dnf install -y cmake gcc ${_list}\n")
        else()
            set(_text "    sudo dnf install -y ${_list}\n")
        endif()
    elseif(GAME3D_DISTRO STREQUAL "debian")
        set(_text "    sudo apt install -y ${_list}\n")
    elseif(GAME3D_DISTRO STREQUAL "arch")
        set(_text "    sudo pacman -S --needed ${_list}\n")
    elseif(GAME3D_DISTRO STREQUAL "suse")
        set(_text "    sudo zypper install -y ${_list}\n")
    else()
        set(_text "")
    endif()

    set(${OUT_VAR} "${_text}" PARENT_SCOPE)
endfunction()

# GLFW is the only window/input dependency. Prefer a package already installed
# by the user; FetchContent keeps a fresh Windows/Linux checkout buildable
# without platform-specific project files or manual library setup.
find_package(glfw3 3.3 CONFIG QUIET)

if(TARGET glfw)
    set(GAME3D_GLFW_TARGET glfw)
elseif(TARGET glfw3::glfw)
    set(GAME3D_GLFW_TARGET glfw3::glfw)
elseif(GAME3D_FETCH_GLFW)
    include(FetchContent)

    # Game3D needs only the library, not GLFW's examples, tests or docs.
    # GLFW_INSTALL тоже выключен: скачанная копия GLFW принадлежит этой сборке,
    # и `cmake --install` не должен раскладывать её заголовки по системе.
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    # GLFW 3.4 собирается с двумя бэкендами сразу — X11 и Wayland, — и каждый
    # требует своих заголовков, поэтому по умолчанию сборка падает на том,
    # которого нет: «Could NOT find X11» или «Failed to find wayland-scanner».
    # Game3D работает на любом из них, так что включаем то, что есть в системе,
    # а лишний бэкенд выключаем явно. GAME3D_GLFW_WAYLAND=ON/OFF задаёт выбор
    # вручную.
    if(UNIX AND NOT APPLE)
        if(NOT GAME3D_GLFW_WAYLAND MATCHES "^(AUTO|ON|OFF)$")
            message(FATAL_ERROR
                "GAME3D_GLFW_WAYLAND must be AUTO, ON or OFF, "
                "not '${GAME3D_GLFW_WAYLAND}'.")
        endif()

        # --- Wayland ---------------------------------------------------------
        # GLFW зовёт wayland-scanner (протоколы он генерирует из собственного
        # каталога deps/wayland, отдельный пакет wayland-protocols не нужен) и
        # просит у pkg-config модули wayland-client, wayland-cursor, wayland-egl
        # и xkbcommon.
        set(GAME3D_WAYLAND_FOUND FALSE)

        find_program(GAME3D_WAYLAND_SCANNER NAMES wayland-scanner)
        find_package(PkgConfig QUIET)

        if(GAME3D_WAYLAND_SCANNER AND PKG_CONFIG_FOUND)
            pkg_check_modules(GAME3D_WAYLAND QUIET
                wayland-client>=0.2.7
                wayland-cursor>=0.2.7
                wayland-egl>=0.2.7
                xkbcommon>=0.5.0)
        endif()

        # --- X11 -------------------------------------------------------------
        # Те же проверки, что делает сам GLFW: ему нужны не только заголовки
        # Xlib, но и Xrandr, Xinerama, Xkb, Xcursor, XInput2 и Xshape.
        set(GAME3D_X11_MISSING "")

        find_package(X11 QUIET)

        foreach(_check IN ITEMS
                "x11:X11_X11_INCLUDE_PATH"
                "xrandr:X11_Xrandr_INCLUDE_PATH"
                "xinerama:X11_Xinerama_INCLUDE_PATH"
                "xkb:X11_Xkb_INCLUDE_PATH"
                "xcursor:X11_Xcursor_INCLUDE_PATH"
                "xi:X11_Xi_INCLUDE_PATH"
                "xext:X11_Xshape_INCLUDE_PATH")
            string(REPLACE ":" ";" _parts "${_check}")
            list(GET _parts 0 _role)
            list(GET _parts 1 _variable)
            if(NOT ${_variable})
                game3d_library_label(${_role} _label)
                list(APPEND GAME3D_X11_MISSING "${_label}")
            endif()
        endforeach()

        if(GAME3D_X11_MISSING)
            set(GAME3D_X11_FOUND FALSE)
            # Когда X11 не найден целиком, перечислять нечего: ответ — команда
            # установки. А если это только часть заголовков, стоит сказать,
            # каких именно не хватает.
            if(X11_FOUND)
                list(JOIN GAME3D_X11_MISSING ", " _x11_missing)
                set(GAME3D_X11_MISSING_NOTE
                    "Не хватает заголовков X11: ${_x11_missing}.\n")
            else()
                set(GAME3D_X11_MISSING_NOTE "")
            endif()
        else()
            set(GAME3D_X11_FOUND TRUE)
            set(GAME3D_X11_MISSING_NOTE "")
        endif()

        # --- какой бэкенд собирать -------------------------------------------
        set(GAME3D_BUILD_X11 ${GAME3D_X11_FOUND})

        if(GAME3D_GLFW_WAYLAND STREQUAL "AUTO")
            set(GAME3D_BUILD_WAYLAND ${GAME3D_WAYLAND_FOUND})
        elseif(GAME3D_GLFW_WAYLAND)
            set(GAME3D_BUILD_WAYLAND TRUE)
        else()
            set(GAME3D_BUILD_WAYLAND FALSE)
            set(GAME3D_BUILD_X11 TRUE)  # X11 попросили явно
        endif()

        game3d_install_hint(GAME3D_HINT_X11 x11 xkb xrandr xinerama xcursor xi xext gl)
        game3d_install_hint(GAME3D_HINT_WAYLAND wayland xkbcommon gl)

        if(GAME3D_GLFW_WAYLAND STREQUAL "ON" AND NOT GAME3D_WAYLAND_FOUND)
            message(WARNING
                "GAME3D_GLFW_WAYLAND=ON, but Wayland development files not found.")
        elseif(GAME3D_GLFW_WAYLAND STREQUAL "OFF" AND NOT GAME3D_X11_FOUND)
            message(WARNING
                "GAME3D_GLFW_WAYLAND=OFF, but X11 development files not found.")
        elseif(NOT GAME3D_BUILD_X11 AND NOT GAME3D_BUILD_WAYLAND)
            message(STATUS "GLFW: building with null/headless platform")
        endif()

        # FORCE, а не просто значение по умолчанию: каталог сборки, оставшийся
        # от прошлой конфигурации, должен следовать за тем, что установлено
        # в системе сейчас.
        set(GLFW_BUILD_X11 ${GAME3D_BUILD_X11} CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND ${GAME3D_BUILD_WAYLAND} CACHE BOOL "" FORCE)

        if(GAME3D_BUILD_X11 AND GAME3D_BUILD_WAYLAND)
            message(STATUS "GLFW backends: X11 and Wayland")
        elseif(GAME3D_BUILD_WAYLAND)
            message(STATUS "GLFW backends: Wayland only")
        elseif(GAME3D_BUILD_X11)
            message(STATUS "GLFW backends: X11 only")
        else()
            message(STATUS "GLFW backends: null platform")
        endif()
    endif()

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glfw)
    set(GAME3D_GLFW_TARGET glfw)
else()
    game3d_install_hint(_hint glfw)
    message(FATAL_ERROR
        "GLFW 3.3+ was not found. Install glfw3 or configure with "
        "-DGAME3D_FETCH_GLFW=ON.\n"
        "GLFW:\n${_hint}")
endif()

# Текст меню растеризуется готовой single-header библиотекой stb_truetype
# (public domain, https://github.com/nothings/stb) — без неё пришлось бы
# рисовать буквы собственным растровым шрифтом. Заголовочная библиотека:
# реализации нет, нужен только путь к заголовкам.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/stb/stb_truetype.h")
    set(GAME3D_STB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/stb")
elseif(GAME3D_FETCH_STB)
    include(FetchContent)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
        GIT_SHALLOW    TRUE
    )
    # В каталоге stb нет CMakeLists.txt: MakeAvailable просто скачает его.
    FetchContent_MakeAvailable(stb)
    set(GAME3D_STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
    if(NOT EXISTS "${GAME3D_STB_INCLUDE_DIR}/stb_truetype.h")
        message(FATAL_ERROR
            "stb скачан в ${GAME3D_STB_INCLUDE_DIR}, но stb_truetype.h там нет. "
            "Удалите каталог сборки и сконфигурируйте заново.")
    endif()
else()
    set(GAME3D_STB_INCLUDE_DIR "" CACHE PATH
        "Directory with stb_truetype.h when GAME3D_FETCH_STB is OFF")
    if(NOT EXISTS "${GAME3D_STB_INCLUDE_DIR}/stb_truetype.h")
        message(FATAL_ERROR
            "GAME3D_FETCH_STB is OFF, but stb_truetype.h is not in "
            "${GAME3D_STB_INCLUDE_DIR}. Set -DGAME3D_STB_INCLUDE_DIR=<каталог> "
            "или включите -DGAME3D_FETCH_STB=ON.")
    endif()
endif()

find_package(OpenGL QUIET)

if(TARGET OpenGL::GL)
    set(GAME3D_OPENGL_TARGET OpenGL::GL)
elseif(TARGET OpenGL::OpenGL)
    set(GAME3D_OPENGL_TARGET OpenGL::OpenGL)
elseif(OpenGL_FOUND)
    set(GAME3D_OPENGL_TARGET ${OPENGL_LIBRARIES})
else()
    # Если системный OpenGL не найден (например, в headless-окружении),
    # создаём псевдо-цель с локальными заголовками и заглушкой
    add_library(game3d_opengl_fallback INTERFACE)
    target_include_directories(game3d_opengl_fallback INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/deps")
    if(EXISTS "/tmp/libGL.so")
        target_link_libraries(game3d_opengl_fallback INTERFACE "/tmp/libGL.so")
    endif()
    set(GAME3D_OPENGL_TARGET game3d_opengl_fallback)
endif()

add_executable(game3d ${GAME3D_SOURCES} ${GAME3D_HEADERS})

# Раскладка файлов в IDE повторяет каталоги репозитория (render, map, editor).
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
             FILES ${GAME3D_SOURCES} ${GAME3D_HEADERS})

target_compile_features(game3d PRIVATE c_std_11)
target_include_directories(game3d PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/deps")
# SYSTEM: stb_truetype.h — сторонний заголовок, его предупреждения не должны
# мешать сборке с -Wall -Wextra -Wpedantic.
target_include_directories(game3d SYSTEM PRIVATE "${GAME3D_STB_INCLUDE_DIR}")
target_link_libraries(game3d PRIVATE
    ${GAME3D_GLFW_TARGET}
    ${GAME3D_OPENGL_TARGET}
)

# libm is needed on Unix; MSVC and Windows OpenGL do not need it.
if(UNIX AND NOT APPLE)
    target_link_libraries(game3d PRIVATE m)
endif()

# ---------------------------------------------------------------------------
# Windows: два условия, без которых сборка не проходит или портится текст.
#
# 1. <GL/gl.h> из Windows SDK (и из MinGW) написан в расчёте на windows.h:
#    сам он WINGDIAPI и APIENTRY не объявляет, поэтому без windows.h впереди
#    разбирается с ошибками. Модули рендера подключают <GL/gl.h> напрямую, так
#    что windows.h подставляется компилятором в начало каждого файла — это
#    держит исходники переносимыми. WIN32_LEAN_AND_MEAN убирает из windows.h
#    сокеты, DDE, RPC и оболочку (GDI, откуда WINGDIAPI, остаётся на месте).
# 2. Исходники в UTF-8, в меню русский текст, а font.c ждёт на входе именно
#    UTF-8. MSVC без /utf-8 читает файлы в кодовой странице системы, и вместо
#    подписей кнопок получается мусор.
# ---------------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(game3d PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        # /FI — принудительное подключение заголовка, /utf-8 — кодировка
        # исходников и строковых литералов. Подходит и для clang-cl.
        target_compile_options(game3d PRIVATE /FIwindows.h /utf-8)
    else()
        # MinGW: у GCC и Clang то же самое пишется иначе. Кодировку задаём
        # только GCC — Clang и так считает исходники и литералы UTF-8.
        target_compile_options(game3d PRIVATE "SHELL:-include windows.h")
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_compile_options(game3d PRIVATE
                -finput-charset=UTF-8 -fexec-charset=UTF-8)
        endif()
    endif()
endif()

if(MSVC)
    target_compile_options(game3d PRIVATE /W4 /permissive-)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(game3d PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(GAME3D_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(game3d PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(game3d PRIVATE -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "GAME3D_SANITIZERS requires GCC or Clang")
    endif()
endif()

# Strict C11 without compiler extensions on every generator.
set_target_properties(game3d PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

# Шрифт меню должен лежать рядом с игрой: main.c ищет его в каталоге
# исполняемого файла (и в текущем каталоге, если запущено из сборки).
if(NOT EXISTS "${GAME3D_FONT_SOURCE}")
    message(FATAL_ERROR "Interface font not found: ${GAME3D_FONT_SOURCE}")
endif()

add_custom_command(TARGET game3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:game3d>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GAME3D_FONT_SOURCE}"
            "$<TARGET_FILE_DIR:game3d>/assets/fonts/${GAME3D_FONT_FILE}"
    COMMENT "Copying ${GAME3D_FONT_FILE} next to game3d"
    VERBATIM)

# Необязательная установка: `cmake --install build --prefix <каталог>` кладёт
# редактор и шрифт тем же деревом, что и сборка, — каталог можно унести целиком.
install(TARGETS game3d RUNTIME DESTINATION ".")
install(FILES "${GAME3D_FONT_SOURCE}" DESTINATION "assets/fonts")

cmake_minimum_required(VERSION 3.20)

project(Game3D VERSION 1.3 LANGUAGES C)

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
    map/gen.c
    map/list.c
    map/map.c
    physics/coll.c
    physics/physics.c
    render/font.c
    render/prim.c
    render/render.c
    render/texpool.c
    render/ui.c
    render/window.c
)

# Шрифт меню: лежит в репозитории и копируется к собранной игре, чтобы
# exe/ELF находил его рядом с собой (см. find_font в main.c).
set(GAME3D_FONT_FILE DejaVuSans.ttf)
set(GAME3D_FONT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/${GAME3D_FONT_FILE})

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
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)

    # GLFW 3.4 enables its Wayland backend by default on Linux and stops with
    # "Failed to find wayland-scanner" when the Wayland development files are
    # not installed. Game3D runs on any backend GLFW can build, so enable
    # Wayland only when its tools really are there and stay on X11 otherwise.
    # GAME3D_GLFW_WAYLAND=ON/OFF overrides the check in both directions.
    if(UNIX AND NOT APPLE)
        set(GAME3D_WAYLAND_TOOLS_FOUND FALSE)

        find_program(GAME3D_WAYLAND_SCANNER NAMES wayland-scanner)
        find_package(PkgConfig QUIET)

        if(GAME3D_WAYLAND_SCANNER AND PKG_CONFIG_FOUND)
            # The same modules GLFW itself asks pkg-config for, plus
            # wayland-protocols which the scanner reads at build time.
            pkg_check_modules(GAME3D_WAYLAND QUIET
                wayland-client>=0.2.7
                wayland-cursor>=0.2.7
                wayland-egl>=0.2.7
                xkbcommon>=0.5.0)
            if(GAME3D_WAYLAND_FOUND)
                pkg_check_modules(GAME3D_WAYLAND_PROTOCOLS QUIET wayland-protocols)
                set(GAME3D_WAYLAND_TOOLS_FOUND ${GAME3D_WAYLAND_PROTOCOLS_FOUND})
            endif()
        endif()

        if(GAME3D_GLFW_WAYLAND STREQUAL "AUTO")
            # Forced, not just defaulted, so a build directory left over from an
            # earlier configure follows the tools actually installed right now.
            set(GLFW_BUILD_WAYLAND ${GAME3D_WAYLAND_TOOLS_FOUND} CACHE BOOL "" FORCE)
        elseif(GAME3D_GLFW_WAYLAND)
            set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
        else()
            set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        endif()

        if(NOT GLFW_BUILD_WAYLAND)
            find_package(X11 QUIET)
            if(NOT X11_FOUND)
                message(FATAL_ERROR
                    "GLFW needs either the X11 or the Wayland development files and "
                    "found neither.\n"
                    "X11: Debian/Ubuntu 'xorg-dev libglu1-mesa-dev', "
                    "Fedora 'libX11-devel libXrandr-devel libXinerama-devel "
                    "libXcursor-devel libXi-devel libXext-devel'.\n"
                    "Wayland: Debian/Ubuntu 'libwayland-dev wayland-protocols "
                    "libxkbcommon-dev', Fedora 'wayland-devel wayland-protocols "
                    "libxkbcommon-devel', then configure with "
                    "-DGAME3D_GLFW_WAYLAND=ON.")
            endif()
            if(NOT GAME3D_GLFW_WAYLAND STREQUAL "OFF")
                message(STATUS
                    "GLFW: Wayland tools not found, building the X11 backend only "
                    "(install wayland-scanner, wayland-protocols and the Wayland "
                    "client development packages, or force them with "
                    "-DGAME3D_GLFW_WAYLAND=ON)")
            endif()
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
    message(FATAL_ERROR
        "GLFW 3.3+ was not found. Install glfw3 or configure with "
        "-DGAME3D_FETCH_GLFW=ON.")
endif()

# Текст меню растеризуется готовой single-header библиотекой stb_truetype
# (public domain, https://github.com/nothings/stb) — без неё пришлось бы
# рисовать буквы собственным растровым шрифтом. Заголовочная библиотека:
# реализации нет, нужен только путь к заголовкам.
if(GAME3D_FETCH_STB)
    include(FetchContent)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
        GIT_SHALLOW    TRUE
    )
    # В каталоге stb нет CMakeLists.txt: MakeAvailable просто скачает его.
    FetchContent_MakeAvailable(stb)
    set(GAME3D_STB_INCLUDE_DIR ${stb_SOURCE_DIR})
else()
    set(GAME3D_STB_INCLUDE_DIR "" CACHE PATH
        "Directory with stb_truetype.h when GAME3D_FETCH_STB is OFF")
    if(NOT EXISTS ${GAME3D_STB_INCLUDE_DIR}/stb_truetype.h)
        message(FATAL_ERROR
            "GAME3D_FETCH_STB is OFF, but stb_truetype.h is not in "
            "${GAME3D_STB_INCLUDE_DIR}. Set -DGAME3D_STB_INCLUDE_DIR=<каталог> "
            "или включите -DGAME3D_FETCH_STB=ON.")
    endif()
endif()

find_package(OpenGL REQUIRED)

if(TARGET OpenGL::GL)
    set(GAME3D_OPENGL_TARGET OpenGL::GL)
elseif(TARGET OpenGL::OpenGL)
    set(GAME3D_OPENGL_TARGET OpenGL::OpenGL)
else()
    message(FATAL_ERROR "CMake found OpenGL, but no linkable OpenGL target is available.")
endif()

add_executable(game3d ${GAME3D_SOURCES})

target_compile_features(game3d PRIVATE c_std_11)
target_include_directories(game3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# SYSTEM: stb_truetype.h — сторонний заголовок, его предупреждения не должны
# мешать сборке с -Wall -Wextra -Wpedantic.
target_include_directories(game3d SYSTEM PRIVATE ${GAME3D_STB_INCLUDE_DIR})
target_link_libraries(game3d PRIVATE
    ${GAME3D_GLFW_TARGET}
    ${GAME3D_OPENGL_TARGET}
)

# libm is needed on Unix; MSVC and Windows OpenGL do not need it.
if(UNIX AND NOT APPLE)
    target_link_libraries(game3d PRIVATE m)
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
if(NOT EXISTS ${GAME3D_FONT_SOURCE})
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

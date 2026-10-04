# game3d-server — автономный CLI-сервер Game3D. Единственный проект этой
# ветки: клиент удалён и живёт в основной ветке репозитория Game3D.
# Сборка (OpenGL, GLFW и stb не нужны — подходит машина без графики):
#   cmake -S . -B build
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.20)

project(Game3DServer VERSION 1.0 LANGUAGES C)

option(GAME3D_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
       OFF)

add_executable(game3d-server
    main.c
    commands.c
    config.c
    log.c
    protocol.c
    server.c
    socket.c
    map/map.c
    player/col.c
    player/rayvisible.c
)

target_compile_features(game3d-server PRIVATE c_std_11)
# Заголовки подключаются от корня проекта: "socket.h", "map/map.h", ...
target_include_directories(game3d-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(game3d-server PRIVATE /W4 /permissive-)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(game3d-server PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    # Winsock для UDP; поток чтения stdin живёт в CRT.
    target_link_libraries(game3d-server PRIVATE ws2_32)
else()
    # Поток чтения команд stdin.
    find_package(Threads REQUIRED)
    target_link_libraries(game3d-server PRIVATE Threads::Threads)
endif()

if(GAME3D_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(game3d-server
            PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(game3d-server
            PRIVATE -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "GAME3D_SANITIZERS requires GCC or Clang")
    endif()
endif()

# Строгий C11 без расширений компилятора — как у игры.
set_target_properties(game3d-server PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

cmake_minimum_required(VERSION 3.20)

project(Game3D VERSION 1.3 LANGUAGES C)

option(GAME3D_FETCH_GLFW
       "Download GLFW when a system package is not available"
       ON)
option(GAME3D_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
       OFF)

set(GAME3D_SOURCES
    main.c
    image.c
    map/gen.c
    map/map.c
    physics/coll.c
    physics/physics.c
    render/prim.c
    render/render.c
    render/texpool.c
    render/window.c
)

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

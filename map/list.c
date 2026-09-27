/* Проект собирается строго по C11 (без расширений компилятора), поэтому
 * POSIX-функции нужно запросить явно: без этого readlink остаётся
 * необъявленным и GCC 14 останавливает сборку. */
#ifndef _WIN32
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map/list.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOGDI
#  define NOUSER
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

static char game_dir[MAP_LIST_PATH_MAX];
static int game_dir_resolved = 0;

/* ---------- Расширения карт ---------- */

/* Сравнение строк без учёта регистра: strcasecmp есть не везде, а имена
 * файлов в Windows и macOS часто пишут в другом регистре. */
static int same_text(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a;
        char y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
        if (x == '\0') return 1;
    }
}

static int is_map_file(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return 0;
    return same_text(dot, ".tfm");
}

/* ---------- Каталог с игрой ---------- */

/* Обрезает имя файла, оставляя каталог. "/" и "C:\" остаются как есть. */
static void keep_directory(char *path) {
    size_t len = strlen(path);
    for (size_t i = len; i > 0; i--) {
        if (path[i - 1] == '/' || path[i - 1] == '\\') {
            path[i - 1] = '\0';
            return;
        }
    }
    path[0] = '\0';   /* имя без каталога: запускаем из текущего */
}

const char *map_list_game_dir(void) {
    if (game_dir_resolved) return game_dir;
    game_dir_resolved = 1;
    snprintf(game_dir, sizeof game_dir, ".");

    char path[MAP_LIST_PATH_MAX];
    size_t len = 0;

#ifdef _WIN32
    DWORD written = GetModuleFileNameA(NULL, path, (DWORD)sizeof path);
    if (written > 0 && written < sizeof path) {
        path[written] = '\0';
        len = written;
    }
#else
    /* Linux: /proc/self/exe указывает на сам исполняемый файл. */
    ssize_t bytes = readlink("/proc/self/exe", path, sizeof path - 1);
    if (bytes > 0) {
        path[bytes] = '\0';
        len = (size_t)bytes;
    }
#endif

    if (len == 0) return game_dir;   /* не определили — остаёмся в текущем */

    const int rooted = (path[0] == '/' || path[0] == '\\');
    keep_directory(path);

    if (path[0] != '\0') {
        snprintf(game_dir, sizeof game_dir, "%s", path);
    } else if (rooted) {
        snprintf(game_dir, sizeof game_dir, "/");   /* "/game3d" */
    }
    /* иначе имя файла без каталога — остаёмся в текущем (".") */
    return game_dir;
}

/* ---------- Содержимое каталога ---------- */

static int list_dir(const char *dir, char names[][MAP_LIST_NAME_MAX], int max) {
    int count = 0;

#ifdef _WIN32
    char pattern[MAP_LIST_PATH_MAX];
    snprintf(pattern, sizeof pattern, "%s\\*", dir);

    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) return 0;

    do {
        if (count >= max) break;
        if (entry.cFileName[0] == '.') continue;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!is_map_file(entry.cFileName)) continue;
        const size_t name_len = strlen(entry.cFileName);
        if (name_len >= MAP_LIST_NAME_MAX) continue;

        memcpy(names[count], entry.cFileName, name_len + 1);
        count++;
    } while (FindNextFileA(search, &entry));

    FindClose(search);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;

    struct dirent *entry;
    while (count < max && (entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (!is_map_file(entry->d_name)) continue;
        /* Имя длиннее буфера не показываем: обрезанное не открыть. */
        const size_t name_len = strlen(entry->d_name);
        if (name_len >= MAP_LIST_NAME_MAX) continue;

        char full[MAP_LIST_PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, entry->d_name);

        struct stat info;
        if (stat(full, &info) != 0) continue;
        if (!S_ISREG(info.st_mode)) continue;

        memcpy(names[count], entry->d_name, name_len + 1);
        count++;
    }

    closedir(d);
#endif

    return count;
}

static int compare_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int map_list_scan(MapList *list, const char *dir) {
    if (!list) return 0;

    snprintf(list->dir, sizeof list->dir, "%s", (dir && dir[0]) ? dir : ".");
    list->count = list_dir(list->dir, list->names, MAP_LIST_MAX);

    if (list->count > 1) {
        qsort(list->names, (size_t)list->count, MAP_LIST_NAME_MAX, compare_names);
    }
    return list->count;
}

void map_list_path(const MapList *list, int index, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!list || index < 0 || index >= list->count) return;

#ifdef _WIN32
    snprintf(out, out_size, "%s\\%s", list->dir, list->names[index]);
#else
    snprintf(out, out_size, "%s/%s", list->dir, list->names[index]);
#endif
}

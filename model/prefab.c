#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map/list.h"
#include "model/emf.h"
#include "model/prefab.h"
#include "render/prim.h"
#include "render/texpool.h"

/* ---------- Реестр ----------
 * Префабы лежат отдельными блоками: карта хранит указатели на них, и
 * переезд массива не должен их ломать. Имена — отдельный список, потому
 * что у одного префаба их бывает два: имя из файла и имя, по которому его
 * попросили (файл мог называться иначе). */

typedef struct {
    char name[EMF_NAME_MAX];
    Prefab *prefab;
} PrefabName;

static Prefab **g_prefabs;
static size_t g_prefab_count;
static size_t g_prefab_capacity;

static PrefabName *g_names;
static size_t g_name_count;
static size_t g_name_capacity;

/* Текстуры моделей: один .raw, на который ссылаются несколько .emf,
 * загружается в видеопамять один раз. */
static TexPool g_textures;

static int g_ready;

static int same_name(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int copy_text(char *out, size_t size, const char *text) {
    const int written = snprintf(out, size, "%s", text ? text : "");
    return written >= 0 && (size_t)written < size;
}

static void add_name(const char *name, Prefab *prefab) {
    if (!name || name[0] == '\0') return;
    if (prefab_find(name)) return;   /* такое имя уже занято */

    if (g_name_count >= g_name_capacity) {
        const size_t new_cap = (g_name_capacity == 0) ? 8 : g_name_capacity * 2;
        PrefabName *grown = realloc(g_names, new_cap * sizeof(PrefabName));
        if (!grown) return;
        g_names = grown;
        g_name_capacity = new_cap;
    }

    copy_text(g_names[g_name_count].name, sizeof g_names[0].name, name);
    g_names[g_name_count].prefab = prefab;
    g_name_count++;
}

static void add_prefab(Prefab *prefab) {
    if (g_prefab_count >= g_prefab_capacity) {
        const size_t new_cap = (g_prefab_capacity == 0) ? 8 : g_prefab_capacity * 2;
        Prefab **grown = realloc(g_prefabs, new_cap * sizeof(Prefab *));
        if (!grown) return;
        g_prefabs = grown;
        g_prefab_capacity = new_cap;
    }
    g_prefabs[g_prefab_count++] = prefab;
    add_name(prefab->name, prefab);
}

const Prefab *prefab_find(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < g_name_count; i++) {
        if (same_name(g_names[i].name, name)) return g_names[i].prefab;
    }
    return NULL;
}

int prefab_count(void) {
    return (int)g_prefab_count;
}

const Prefab *prefab_builtin_cube(void) {
    return prefab_find(PREFAB_BUILTIN_CUBE);
}

void prefab_init(void) {
    if (g_ready) return;
    g_ready = 1;

    texpool_init(&g_textures);

    /* Встроенная модель куба: файла у неё нет, рисуется прежним путём
     * (текстура ложится по миру, см. map/map.c), поэтому меш ей не нужен. */
    Prefab *cube = calloc(1, sizeof(Prefab));
    if (!cube) return;
    copy_text(cube->name, sizeof cube->name, PREFAB_BUILTIN_CUBE);
    cube->builtin_cube = 1;
    cube->min_x = 0.0f; cube->min_y = 0.0f; cube->min_z = 0.0f;
    cube->max_x = 1.0f; cube->max_y = 1.0f; cube->max_z = 1.0f;
    add_prefab(cube);
}

void prefab_shutdown(void) {
    for (size_t i = 0; i < g_prefab_count; i++) {
        Prefab *prefab = g_prefabs[i];
        if (!prefab) continue;
        if (!prefab->builtin_cube) emf_free(&prefab->mesh);
        free(prefab);
    }
    free(g_prefabs);
    free(g_names);

    g_prefabs = NULL;
    g_prefab_count = 0;
    g_prefab_capacity = 0;
    g_names = NULL;
    g_name_count = 0;
    g_name_capacity = 0;

    /* Текстуры моделей удаляются, пока контекст OpenGL ещё жив. */
    texpool_free(&g_textures);
    g_ready = 0;
}

/* ---------- Пути ---------- */

static int file_readable(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int path_is_absolute(const char *path) {
    if (path[0] == '/' || path[0] == '\\') return 1;
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return 1;   /* C:\... */
    return 0;
}

static int has_separator(const char *name) {
    return strchr(name, '/') != NULL || strchr(name, '\\') != NULL;
}

static int has_emf_extension(const char *name) {
    const size_t len = strlen(name);
    return len > 4 && same_name(name + len - 4, ".emf");
}

/* Пробует файл dir/rel (dir может быть пустым) и копирует путь в out. */
static int probe(const char *dir, const char *rel, char *out, size_t size) {
    char path[PREFAB_PATH_MAX];

    if (dir && dir[0] != '\0') {
        const size_t len = strlen(dir);
        const char *sep = (dir[len - 1] == '/' || dir[len - 1] == '\\') ? "" : "/";
        const int written = snprintf(path, sizeof path, "%s%s%s", dir, sep, rel);
        if (written < 0 || (size_t)written >= sizeof path) return 0;
    } else {
        const int written = snprintf(path, sizeof path, "%s", rel);
        if (written < 0 || (size_t)written >= sizeof path) return 0;
    }

    if (!file_readable(path)) return 0;
    copy_text(out, size, path);
    return 1;
}

/* Файл модели по имени префаба: рядом с картой, в текущем каталоге,
 * в каталоге игры и в его подкаталогах models/ и assets/models/. */
static int find_prefab_file(const char *name, const char *base_dir,
                            char *out, size_t size) {
    if (!name || name[0] == '\0') return 0;

    if (path_is_absolute(name)) return probe("", name, out, size);

    if (has_separator(name) || has_emf_extension(name)) {
        return probe(base_dir, name, out, size) || probe("", name, out, size);
    }

    static const char *const subdirs[] = { "", "models", "assets/models" };
    const char *game_dir = map_list_game_dir();

    char file[PREFAB_PATH_MAX];
    for (size_t i = 0; i < sizeof subdirs / sizeof subdirs[0]; i++) {
        if (subdirs[i][0] != '\0') {
            const int written = snprintf(file, sizeof file, "%s/%s.emf", subdirs[i], name);
            if (written < 0 || (size_t)written >= sizeof file) continue;
        } else {
            const int written = snprintf(file, sizeof file, "%s.emf", name);
            if (written < 0 || (size_t)written >= sizeof file) continue;
        }

        if (probe(base_dir, file, out, size)) return 1;
        if (probe("", file, out, size)) return 1;
        if (probe(game_dir, file, out, size)) return 1;
    }
    return 0;
}

/* Каталог файла вместе с завершающим разделителем: "" — файл без каталога. */
static void directory_of(const char *path, char *out, size_t size) {
    out[0] = '\0';

    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    if (!slash) return;

    size_t len = (size_t)(slash - path) + 1;
    if (len >= size) len = size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

/* Имя файла без каталога и расширения — им подписывается модель, у которой
 * в файле не оказалось названия префаба. */
static void base_name(const char *path, char *out, size_t size) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;

    copy_text(out, size, slash ? slash + 1 : path);

    char *dot = strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
}

/* ---------- Загрузка ---------- */

static void mesh_bounds(const EmfMesh *mesh, Prefab *prefab) {
    prefab->min_x = prefab->max_x = mesh->points[0];
    prefab->min_y = prefab->max_y = mesh->points[1];
    prefab->min_z = prefab->max_z = mesh->points[2];

    for (uint32_t i = 1; i < mesh->num_points; i++) {
        const float x = mesh->points[i * 3 + 0];
        const float y = mesh->points[i * 3 + 1];
        const float z = mesh->points[i * 3 + 2];

        if (x < prefab->min_x) prefab->min_x = x;
        if (x > prefab->max_x) prefab->max_x = x;
        if (y < prefab->min_y) prefab->min_y = y;
        if (y > prefab->max_y) prefab->max_y = y;
        if (z < prefab->min_z) prefab->min_z = z;
        if (z > prefab->max_z) prefab->max_z = z;
    }
}

/* Текстура модели: путь ищется рядом с .emf, затем в каталоге игры и
 * в текущем каталоге. NULL — файла нет, объект нарисуется текстурой карты. */
static const Texture *load_prefab_texture(const char *model_path,
                                          const char *texture_name) {
    if (!texture_name || texture_name[0] == '\0') return NULL;

    char dir[PREFAB_PATH_MAX];
    char path[PREFAB_PATH_MAX];

    directory_of(model_path, dir, sizeof dir);

    if (path_is_absolute(texture_name)) {
        if (!probe("", texture_name, path, sizeof path)) return NULL;
        return texpool_get(&g_textures, path);
    }

    if (!probe(dir, texture_name, path, sizeof path)
        && !probe(map_list_game_dir(), texture_name, path, sizeof path)
        && !probe("", texture_name, path, sizeof path)) {
        fprintf(stderr, "Warning: model '%s': cannot find texture '%s'\n",
                model_path, texture_name);
        return NULL;
    }

    const Texture *tex = texpool_get(&g_textures, path);
    if (!tex) {
        fprintf(stderr, "Warning: model '%s': cannot load texture '%s'\n",
                model_path, path);
    }
    return tex;
}

/* Читает .emf и заводит по нему префаб. requested — имя, по которому его
 * попросили из карты: оно же становится запасным, если в файле имени нет. */
static Prefab *load_prefab_file(const char *path, const char *requested) {
    EmfMesh mesh;
    if (!emf_load(path, &mesh)) return NULL;

    Prefab *prefab = calloc(1, sizeof(Prefab));
    if (!prefab) {
        emf_free(&mesh);
        return NULL;
    }

    if (mesh.name[0] != '\0') {
        copy_text(prefab->name, sizeof prefab->name, mesh.name);
    } else if (requested && requested[0] != '\0') {
        copy_text(prefab->name, sizeof prefab->name, requested);
    } else {
        base_name(path, prefab->name, sizeof prefab->name);
        if (prefab->name[0] == '\0') {
            copy_text(prefab->name, sizeof prefab->name, PREFAB_BUILTIN_CUBE);
        }
    }

    prefab->mesh = mesh;   /* владение геометрией переходит префабу */
    copy_text(prefab->file, sizeof prefab->file, path);
    mesh_bounds(&prefab->mesh, prefab);
    prefab->texture = load_prefab_texture(path, prefab->mesh.texture);

    add_prefab(prefab);

    /* Файл мог называться иначе, чем записанный в нём префаб: карта
     * должна находить модель по обоим именам. */
    if (requested && requested[0] != '\0'
        && !same_name(requested, prefab->name)) {
        add_name(requested, prefab);
    }
    return prefab;
}

const Prefab *prefab_get(const char *name, const char *base_dir) {
    if (!name || name[0] == '\0') return NULL;

    prefab_init();

    const Prefab *known = prefab_find(name);
    if (known) return known;

    char path[PREFAB_PATH_MAX];
    if (!find_prefab_file(name, base_dir, path, sizeof path)) {
        fprintf(stderr,
                "Warning: prefab '%s' not found. Built-in models: %s; "
                "external ones are <name>.emf next to the map, in the game "
                "directory or in its models/ and assets/models/ subdirectories\n",
                name, PREFAB_BUILTIN_CUBE);
        return NULL;
    }

    /* Тот же файл могли попросить под другим именем: второй раз не читаем. */
    for (size_t i = 0; i < g_prefab_count; i++) {
        Prefab *loaded = g_prefabs[i];
        if (loaded->file[0] != '\0' && strcmp(loaded->file, path) == 0) {
            add_name(name, loaded);
            return loaded;
        }
    }

    Prefab *prefab = load_prefab_file(path, name);
    if (!prefab) {
        fprintf(stderr, "Warning: cannot load prefab '%s' from '%s'\n", name, path);
        return NULL;
    }

    printf("Loaded prefab '%s' from '%s': %u vertices, %u triangles\n",
           prefab->name, prefab->file,
           (unsigned)prefab->mesh.num_points,
           (unsigned)(prefab->mesh.num_indices / 3u));
    return prefab;
}

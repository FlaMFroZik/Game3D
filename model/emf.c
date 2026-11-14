#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model/emf.h"

/* Заголовки формата. Компилятор сам проверит их размер: он зафиксирован
 * в описании формата (model/emf.h), и раскладка полей не должна «поехать»
 * из-за выравнивания. */
#define EMF_HEADER_V4_SIZE 144u
#define EMF_HEADER_V3_SIZE 80u

/* Разумные пределы. Битый заголовок не должен заказывать память на
 * гигабайты, а честная модель такого размера в игру всё равно не влезет. */
#define EMF_MAX_POINTS  4000000u
#define EMF_MAX_INDICES 12000000u

typedef struct {
    char magic[4];
    uint32_t version;
    uint32_t num_points;
    uint32_t num_indices;
    char texture[EMF_NAME_MAX];
} EmfHeaderV3;

typedef struct {
    char magic[4];
    uint32_t version;
    char name[EMF_NAME_MAX];      /* название префаба — первое имя в файле */
    char texture[EMF_NAME_MAX];
    uint32_t num_points;
    uint32_t num_indices;
} EmfHeaderV4;

typedef char emf_header_v3_size_check
    [(sizeof(EmfHeaderV3) == EMF_HEADER_V3_SIZE) ? 1 : -1];
typedef char emf_header_v4_size_check
    [(sizeof(EmfHeaderV4) == EMF_HEADER_V4_SIZE) ? 1 : -1];

void emf_free(EmfMesh *mesh) {
    if (!mesh) return;
    free(mesh->points);
    free(mesh->uvs);
    free(mesh->indices);
    memset(mesh, 0, sizeof *mesh);
}

/* Ограничивает строку из заголовка длиной буфера: файл может прийти без
 * завершающего нуля. */
static void terminate_name(char *text, size_t size) {
    text[size - 1] = '\0';
}

/* Сколько байт занимают данные меша. */
static size_t payload_size(uint32_t num_points, uint32_t num_indices) {
    const size_t points = (size_t)num_points * 3u * sizeof(float);
    const size_t uvs = (size_t)num_points * 2u * sizeof(float);
    const size_t indices = (size_t)num_indices * sizeof(uint32_t);
    return points + uvs + indices;
}

/* Полный размер файла (0 — не удалось узнать). */
static long file_size(FILE *f) {
    if (fseek(f, 0, SEEK_END) != 0) return 0;
    const long size = ftell(f);
    if (size < 0) return 0;
    return size;
}

int emf_load(const char *path, EmfMesh *mesh) {
    if (!mesh) return 0;
    memset(mesh, 0, sizeof *mesh);
    if (!path || path[0] == '\0') return 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "emf: cannot open '%s'\n", path);
        return 0;
    }

    EmfHeaderV4 header;
    memset(&header, 0, sizeof header);

    int ok = fread(header.magic, 1, 4, f) == 4
          && fread(&header.version, sizeof header.version, 1, f) == 1;

    if (ok && memcmp(header.magic, EMF_MAGIC, 4) != 0) {
        fprintf(stderr, "emf: '%s' is not an EMF file\n", path);
        ok = 0;
    }

    unsigned long header_size = EMF_HEADER_V4_SIZE;

    if (ok) {
        if (header.version == EMF_VERSION) {
            ok = fread(header.name, 1, EMF_NAME_MAX, f) == EMF_NAME_MAX
              && fread(header.texture, 1, EMF_NAME_MAX, f) == EMF_NAME_MAX
              && fread(&header.num_points, sizeof header.num_points, 1, f) == 1
              && fread(&header.num_indices, sizeof header.num_indices, 1, f) == 1;
        } else if (header.version == 3) {
            /* Старый заголовок: без имени префаба, счётчики идут сразу
             * после версии, а имя текстуры — после них. */
            uint32_t num_points = 0;
            uint32_t num_indices = 0;
            ok = fread(&num_points, sizeof num_points, 1, f) == 1
              && fread(&num_indices, sizeof num_indices, 1, f) == 1
              && fread(header.texture, 1, EMF_NAME_MAX, f) == EMF_NAME_MAX;
            header.num_points = num_points;
            header.num_indices = num_indices;
            header_size = EMF_HEADER_V3_SIZE;
        } else {
            fprintf(stderr, "emf: '%s' has unsupported version %u\n",
                    path, (unsigned)header.version);
            ok = 0;
        }
    }

    if (ok) {
        terminate_name(header.name, sizeof header.name);
        terminate_name(header.texture, sizeof header.texture);

        if (header.num_points == 0 || header.num_indices == 0) {
            fprintf(stderr, "emf: '%s' has no geometry\n", path);
            ok = 0;
        } else if (header.num_points > EMF_MAX_POINTS
                   || header.num_indices > EMF_MAX_INDICES
                   || header.num_indices % 3u != 0u) {
            fprintf(stderr, "emf: '%s' has invalid counts (%u points, %u indices)\n",
                    path, (unsigned)header.num_points, (unsigned)header.num_indices);
            ok = 0;
        }
    }

    if (ok) {
        /* Файл должен быть не короче объявленного в заголовке: обрезанный
         * файл не читаем частично, это ошибка. */
        const size_t expected = (size_t)header_size
                              + payload_size(header.num_points, header.num_indices);
        const long actual = file_size(f);
        if (actual <= 0 || (size_t)actual < expected) {
            fprintf(stderr, "emf: '%s' is truncated (%ld bytes, expected %lu)\n",
                    path, actual, (unsigned long)expected);
            ok = 0;
        } else if (fseek(f, (long)header_size, SEEK_SET) != 0) {
            ok = 0;
        }
    }

    if (ok) {
        const uint32_t num_points = header.num_points;
        const uint32_t num_indices = header.num_indices;

        mesh->points = malloc((size_t)num_points * 3u * sizeof(float));
        mesh->uvs = malloc((size_t)num_points * 2u * sizeof(float));
        mesh->indices = malloc((size_t)num_indices * sizeof(uint32_t));

        if (!mesh->points || !mesh->uvs || !mesh->indices) {
            fprintf(stderr, "emf: out of memory while reading '%s'\n", path);
            ok = 0;
        } else {
            ok = fread(mesh->points, sizeof(float), (size_t)num_points * 3u, f)
                     == (size_t)num_points * 3u
              && fread(mesh->uvs, sizeof(float), (size_t)num_points * 2u, f)
                     == (size_t)num_points * 2u
              && fread(mesh->indices, sizeof(uint32_t), num_indices, f)
                     == num_indices;

            if (ok) {
                for (uint32_t i = 0; i < num_indices; i++) {
                    if (mesh->indices[i] >= num_points) {
                        fprintf(stderr,
                                "emf: '%s' has index %u out of %u points\n",
                                path, (unsigned)mesh->indices[i], (unsigned)num_points);
                        ok = 0;
                        break;
                    }
                }
            }

            if (ok) {
                mesh->num_points = num_points;
                mesh->num_indices = num_indices;
                memcpy(mesh->name, header.name, sizeof mesh->name);
                memcpy(mesh->texture, header.texture, sizeof mesh->texture);
            }
        }
    }

    fclose(f);

    if (!ok) {
        emf_free(mesh);
        return 0;
    }

    /* Имя префаба обязательно: без него модель нечем вызвать из карты.
     * У файлов версии 3 его нет — имя подставит загрузчик префабов. */
    if (mesh->name[0] == '\0') {
        printf("emf: '%s' has no prefab name, the file name is used\n", path);
    }
    return 1;
}

int emf_save(const char *path, const EmfMesh *mesh) {
    if (!path || !mesh || !mesh->points || !mesh->uvs || !mesh->indices
        || mesh->num_points == 0 || mesh->num_indices == 0) {
        return 0;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "emf: cannot create '%s'\n", path);
        return 0;
    }

    EmfHeaderV4 header;
    memset(&header, 0, sizeof header);
    memcpy(header.magic, EMF_MAGIC, 4);
    header.version = EMF_VERSION;
    header.num_points = mesh->num_points;
    header.num_indices = mesh->num_indices;
    strncpy(header.name, mesh->name, EMF_NAME_MAX - 1);
    strncpy(header.texture, mesh->texture, EMF_NAME_MAX - 1);

    int ok = fwrite(&header, sizeof header, 1, f) == 1
          && fwrite(mesh->points, sizeof(float),
                    (size_t)mesh->num_points * 3u, f) == (size_t)mesh->num_points * 3u
          && fwrite(mesh->uvs, sizeof(float),
                    (size_t)mesh->num_points * 2u, f) == (size_t)mesh->num_points * 2u
          && fwrite(mesh->indices, sizeof(uint32_t),
                    mesh->num_indices, f) == mesh->num_indices;

    if (fclose(f) != 0) ok = 0;
    if (!ok) fprintf(stderr, "emf: cannot write '%s'\n", path);
    return ok;
}

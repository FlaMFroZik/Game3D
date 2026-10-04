#ifndef SRV_PROTOCOL_H
#define SRV_PROTOCOL_H

/* ------------------------------------------------------------------
 * Формат датаграмм многопользовательского протокола Game3D.
 *
 * Полная спецификация — в README.md. Кратко: каждая датаграмма
 * начинается с однобайтового номера [num]; u32 и f32 (IEEE-754
 * binary32) записываются big-endian; строки завершаются нулём.
 * Модуль не решает, что отвечать, — он только собирает и разбирает
 * байты, чтобы обработчики в server.c работали с числами.
 * ------------------------------------------------------------------ */

#include <stddef.h>
#include <stdint.h>

/* Номера датаграмм; таблица — в README.md. */
#define SRV_MSG_INFO       1u
#define SRV_MSG_JOIN       2u
#define SRV_MSG_TRANSFORM  3u
#define SRV_MSG_PLAYERS    4u
#define SRV_MSG_LEAVE      5u

/* Максимальный размер датаграммы — как буфер приёма у клиента. */
#define SRV_PACKET_MAX     2048

/* [3][id:u32][x][y][z][lx][ly][lz] — 1 + 4 + 6*4 байт. */
#define SRV_TRANSFORM_LENGTH 29

/* Ответ [4]: 1 байт заголовка, затем записи по 24 байта, разделённые
 * [0]. 58 записей (1 + 24*58 + 57 = 1450 байт) не выходят за типичный
 * MTU 1500, чтобы список игроков не резался на пути к клиенту. */
#define SRV_PLAYERS_PER_PACKET 58

/* ---------- Числовые поля (big-endian) ---------- */

void protocol_write_u32_be(unsigned char *out, uint32_t value);
uint32_t protocol_read_u32_be(const unsigned char *in);
void protocol_write_f32_be(unsigned char *out, float value);
float protocol_read_f32_be(const unsigned char *in);

/* ---------- Разбор входящих датаграмм ---------- */

/* Ровно [num] длиной 1 байт — у [1], [2] и [5] нет аргументов. */
int protocol_is(const unsigned char *packet, size_t length, unsigned char num);

/* [3][id][x][y][z][lx][ly][lz] — строго 29 байт. */
int protocol_parse_transform(const unsigned char *packet, size_t length,
                             uint32_t *id, float state[6]);

/* [4][id] — строго 5 байт. */
int protocol_parse_players_request(const unsigned char *packet, size_t length,
                                   uint32_t *id);

/* ---------- Сборка ответов ---------- */

/* [1][max_players][players][tick][name\0][map\0]. Размер должен быть
 * точным, иначе браузер клиента выбросит ответ. Возвращает длину или
 * 0, если ответ не помещается в capacity (при валидных --name/--map
 * невозможно: 7 + 63 + 1 + 255 + 1 = 327 < SRV_PACKET_MAX). */
size_t protocol_build_info(unsigned char *out, size_t capacity,
                           uint8_t max_players, uint8_t players,
                           uint32_t tick, const char *name, const char *map);

/* [2][id:u32] — успешный вход. */
size_t protocol_build_join_id(unsigned char *out, uint32_t id);

/* [2] — сервер полон. */
size_t protocol_build_join_full(unsigned char *out);

/* [3] — подтверждение сохранённого состояния. */
size_t protocol_build_transform_ack(unsigned char *out);

/* Последовательная сборка [4]: заголовок, затем записи [x][y][z][lx]
 * [ly][lz], разделённые [0]. Пустой список — просто [4]. */
typedef struct {
    unsigned char *buffer;
    size_t capacity;
    size_t length;
    int count;
} ProtocolPlayersList;

void protocol_players_list_begin(ProtocolPlayersList *list,
                                 unsigned char *buffer, size_t capacity);

/* Добавляет запись; 0 — достигнут лимит записей или буфера. */
int protocol_players_list_add(ProtocolPlayersList *list,
                              const float state[6]);

#endif /* SRV_PROTOCOL_H */

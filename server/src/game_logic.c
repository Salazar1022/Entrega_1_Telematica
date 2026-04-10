/*
 * game_logic.c — Implementación de la Lógica del Juego
 *
 * Conceptos clave:
 *
 * MUTEX POR SALA:
 *   Cada Room tiene su propio room_mutex. Cuando un hilo quiere
 *   modificar una sala (mover un jugador, lanzar un ataque, etc.)
 *   debe adquirir ese mutex. Así dos clientes pueden estar en salas
 *   distintas sin bloquearse mutuamente — solo se bloquean si comparten sala.
 *
 * ARRAY GLOBAL DE SALAS (rooms[]):
 *   Para acceder a este array también hay un mutex global (rooms_mutex).
 *   Solo se usa para crear salas o listarlas, no en operaciones dentro
 *   de una sala ya existente.
 *
 * BROADCAST:
 *   Cuando ocurre un evento importante (ataque, victoria), el servidor
 *   envía el mensaje a TODOS los jugadores de la sala. Esto requiere
 *   iterar el array de jugadores dentro del mutex de la sala.
 */

#include "../include/game_logic.h"
#include "../include/logger.h"
#include "../include/net_utils.h"   /* BUFFER_SIZE */

#include <stdio.h>
#include <stdlib.h>     /* rand, srand        */
#include <string.h>     /* memset, strncpy    */
#include <unistd.h>     /* write, close       */
#include <math.h>       /* sqrt               */
#include <time.h>       /* time               */
#include <pthread.h>

/* ═══════════════════════════════════════════════════════
 * ESTADO GLOBAL DEL SERVIDOR
 * ═══════════════════════════════════════════════════════ */

/* Array de todas las salas activas */
static Room  rooms[MAX_ROOMS];

/* Mutex que protege el acceso al array rooms[] y a room_count */
static pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Contador de salas creadas (usado para generar IDs únicos) */
static int next_room_id = 1;

/* ── Función auxiliar: distancia euclidiana ─────────────────────────────── */
static double distance(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return sqrt((double)(dx * dx + dy * dy));
}

/* ── Función auxiliar: enviar mensaje a un socket ───────────────────────── */
static void send_msg(int sockfd, const char *msg) {
    if (sockfd < 0) return;
    write(sockfd, msg, strlen(msg));
}

/* ══════════════════════════════════════════════════════════════════════════
 * INICIALIZACIÓN
 * ══════════════════════════════════════════════════════════════════════════ */

void game_init(void) {
    /*
     * memset pone todos los bytes a 0 → inicializa el array de salas.
     * Después marcamos cada sala como inactiva con id = 0.
     */
    memset(rooms, 0, sizeof(rooms));
    for (int i = 0; i < MAX_ROOMS; i++) {
        rooms[i].id = 0;  /* 0 = sala inactiva */
        pthread_mutex_init(&rooms[i].room_mutex, NULL);
    }

    /* Inicializar el generador de números aleatorios con la hora actual.
     * srand() con time(NULL) garantiza que los recursos aparezcan en
     * posiciones distintas cada vez que se inicia el servidor. */
    srand((unsigned int)time(NULL));

    log_event(LOG_INFO, NULL, 0, "Sistema de juego inicializado");
}

/* ══════════════════════════════════════════════════════════════════════════
 * GESTIÓN DE SALAS
 * ══════════════════════════════════════════════════════════════════════════ */

int room_create(void) {
    pthread_mutex_lock(&rooms_mutex);

    /* Buscar un slot libre en el array de salas */
    int slot = -1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == 0) {  /* id=0 significa slot vacío */
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        pthread_mutex_unlock(&rooms_mutex);
        log_event(LOG_WARN, NULL, 0, "No hay slots disponibles para nuevas salas");
        return -1;
    }

    /* Inicializar la sala */
    Room *room         = &rooms[slot];
    room->id           = next_room_id++;
    room->state        = ROOM_WAITING;
    room->player_count = 0;
    room->attacker_count = 0;
    room->defender_count = 0;
    memset(room->players, 0, sizeof(room->players));

    /*
     * Colocar NUM_RESOURCES recursos críticos en posiciones aleatorias.
     * rand() % N genera un número entre 0 y N-1.
     *
     * Usamos +5 y -5 para asegurarnos que los recursos no queden
     * exactamente en los bordes del mapa.
     */
    for (int r = 0; r < NUM_RESOURCES; r++) {
        room->resources[r].id           = r;
        room->resources[r].x            = 5 + rand() % (MAP_WIDTH  - 10);
        room->resources[r].y            = 5 + rand() % (MAP_HEIGHT - 10);
        room->resources[r].under_attack = 0;
        memset(room->resources[r].attacker, 0, MAX_USERNAME);
    }

    char msg[128];
    snprintf(msg, sizeof(msg),
             "Sala %d creada — Recursos en: R0(%d,%d) R1(%d,%d)",
             room->id,
             room->resources[0].x, room->resources[0].y,
             room->resources[1].x, room->resources[1].y);
    log_event(LOG_INFO, NULL, 0, msg);

    int new_id = room->id;
    pthread_mutex_unlock(&rooms_mutex);
    return new_id;
}

/* ── room_list() ─────────────────────────────────────────────────────────── */

int room_list(char *out, int out_size) {
    pthread_mutex_lock(&rooms_mutex);

    int count = 0;
    int offset = 0;

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == 0) continue;

        /* Estado como string */
        const char *state_str;
        switch (rooms[i].state) {
            case ROOM_WAITING:  state_str = "WAITING";  break;
            case ROOM_RUNNING:  state_str = "RUNNING";  break;
            case ROOM_FINISHED: state_str = "FINISHED"; break;
            default:            state_str = "UNKNOWN";  break;
        }

        /* Formato v2: "ROOM <id> <estado> <atacantes>/<defensores>/<max>\n" */
        offset += snprintf(out + offset, out_size - offset,
                           "ROOM %d %s %d/%d/%d\n",
                           rooms[i].id,
                           state_str,
                           rooms[i].attacker_count,
                           rooms[i].defender_count,
                           MAX_PLAYERS);
        count++;
    }

    if (count == 0) {
        snprintf(out, out_size, "ROOM_LIST 0\n");
    }

    pthread_mutex_unlock(&rooms_mutex);
    return count;
}

/* ── room_join() ─────────────────────────────────────────────────────────── */

int room_join(int room_id, Player *player) {
    pthread_mutex_lock(&rooms_mutex);

    /* Buscar la sala por ID */
    Room *room = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == room_id) {
            room = &rooms[i];
            break;
        }
    }

    if (room == NULL) {
        pthread_mutex_unlock(&rooms_mutex);
        return -1;  /* Sala no encontrada */
    }

    if (room->player_count >= MAX_PLAYERS) {
        pthread_mutex_unlock(&rooms_mutex);
        return -2;  /* Sala llena */
    }

    if (room->state != ROOM_WAITING) {
        pthread_mutex_unlock(&rooms_mutex);
        return -3;  /* Partida ya iniciada */
    }

    /* Buscar slot libre en el array de jugadores de la sala */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (room->players[i] == NULL) {
            room->players[i] = player;
            room->player_count++;

            if (player->role == ROLE_ATTACKER) {
                room->attacker_count++;
                /* El atacante empieza en (0,0) — no conoce los recursos */
                player->x = 0;
                player->y = 0;
            } else {
                room->defender_count++;
                /* El defensor empieza en la esquina opuesta para separar spawns */
                player->x = MAP_WIDTH - 1;
                player->y = MAP_HEIGHT - 1;
            }

            player->in_room = 1;
            player->room_id = room_id;
            break;
        }
    }

    pthread_mutex_unlock(&rooms_mutex);

    /* Notificar a los demás jugadores de la sala */
    char event_msg[256];
    const char *role_str = (player->role == ROLE_ATTACKER) ? "ATTACKER" : "DEFENDER";
    snprintf(event_msg, sizeof(event_msg),
             "EVENT PLAYER_JOINED %s %s\n", player->username, role_str);
    room_broadcast(room_id, event_msg, player);

    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg),
             "Jugador '%s' (%s) se unió a la sala %d",
             player->username, role_str, room_id);
    log_event(LOG_INFO, player->client_ip, player->client_port, log_msg);

    return 0;
}

/* ── room_try_start() ────────────────────────────────────────────────────── */

int room_try_start(int room_id) {
    pthread_mutex_lock(&rooms_mutex);

    Room *room = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == room_id) {
            room = &rooms[i];
            break;
        }
    }

    if (room == NULL || room->state != ROOM_WAITING) {
        pthread_mutex_unlock(&rooms_mutex);
        return 0;
    }

    /* Necesitamos al menos 1 atacante Y 1 defensor */
    if (room->attacker_count < 1 || room->defender_count < 1) {
        pthread_mutex_unlock(&rooms_mutex);
        return 0;
    }

    room->state = ROOM_RUNNING;
    room->game_start_time = time(NULL);  /* Marcar inicio para GAME_TIMEOUT */
    room->game_timeout    = GAME_TIMEOUT;

    char log_msg[64];
    snprintf(log_msg, sizeof(log_msg), "¡Sala %d iniciada!", room_id);
    log_event(LOG_INFO, NULL, 0, log_msg);

    pthread_mutex_unlock(&rooms_mutex);

    /*
     * Notificar a todos los jugadores que la partida comenzó.
     * Los DEFENSORES reciben también las coordenadas de los recursos.
     */
    room_broadcast(room_id, "EVENT GAME_STARTED\n", NULL);

    /* Enviar posición de recursos solo a los defensores */
    pthread_mutex_lock(&rooms_mutex);
    Room *r = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == room_id) { r = &rooms[i]; break; }
    }
    if (r != NULL) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (r->players[i] == NULL) continue;
            if (r->players[i]->role == ROLE_DEFENDER) {
                /* Enviar posición de cada recurso al defensor */
                for (int res = 0; res < NUM_RESOURCES; res++) {
                    char res_msg[128];
                    snprintf(res_msg, sizeof(res_msg),
                             "EVENT RESOURCE_INFO %d %d %d\n",
                             r->resources[res].id,
                             r->resources[res].x,
                             r->resources[res].y);
                    send_msg(r->players[i]->socket_fd, res_msg);
                }
            }
        }
    }
    pthread_mutex_unlock(&rooms_mutex);

    return 1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * ACCIONES EN PARTIDA
 * ══════════════════════════════════════════════════════════════════════════ */

int player_move(Player *player, int dx, int dy) {
    if (!player->in_room) return -1;

    /*
     * Clampear las coordenadas dentro del mapa.
     * Clampear = limitar a un rango [min, max].
     * Así el jugador no puede salir del mapa aunque envíe dx=9999.
     */
    int new_x = player->x + dx;
    int new_y = player->y + dy;

    /* Limitar a [0, MAP_WIDTH-1] y [0, MAP_HEIGHT-1] */
    if (new_x < 0)            new_x = 0;
    if (new_x >= MAP_WIDTH)   new_x = MAP_WIDTH  - 1;
    if (new_y < 0)            new_y = 0;
    if (new_y >= MAP_HEIGHT)  new_y = MAP_HEIGHT - 1;

    player->x = new_x;
    player->y = new_y;

    return 0;
}

/* ── player_scan() ───────────────────────────────────────────────────────── */

int player_scan(Player *player, char *found_msg, int msg_size) {
    if (!player->in_room) return 0;

    pthread_mutex_lock(&rooms_mutex);

    Room *room = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == player->room_id) {
            room = &rooms[i]; break;
        }
    }

    int found_count = 0;
    int offset = 0;

    if (room != NULL) {
        for (int r = 0; r < NUM_RESOURCES; r++) {
            double dist = distance(player->x, player->y,
                                   room->resources[r].x,
                                   room->resources[r].y);
            if (dist <= DETECTION_RADIUS) {
                found_count++;
                offset += snprintf(found_msg + offset, msg_size - offset,
                                   "EVENT RESOURCE_FOUND %d %d %d\n",
                                   room->resources[r].id,
                                   room->resources[r].x,
                                   room->resources[r].y);
            }
        }
    }

    pthread_mutex_unlock(&rooms_mutex);
    return found_count;
}

/* ── resource_attack() ───────────────────────────────────────────────────── */

int resource_attack(Player *attacker, int resource_id) {
    if (!attacker->in_room) return -1;

    pthread_mutex_lock(&rooms_mutex);

    Room *room = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == attacker->room_id) {
            room = &rooms[i]; break;
        }
    }

    if (room == NULL || room->state != ROOM_RUNNING) {
        pthread_mutex_unlock(&rooms_mutex);
        return -1;
    }

    if (resource_id < 0 || resource_id >= NUM_RESOURCES) {
        pthread_mutex_unlock(&rooms_mutex);
        return -1;  /* Recurso no existe */
    }

    Resource *res = &room->resources[resource_id];

    double dist = distance(attacker->x, attacker->y, res->x, res->y);
    if (dist > DETECTION_RADIUS) {
        pthread_mutex_unlock(&rooms_mutex);
        return -3;  /* Muy lejos del recurso */
    }

    if (res->under_attack) {
        pthread_mutex_unlock(&rooms_mutex);
        return -2;  /* Ya está bajo ataque */
    }

    /* Marcar el recurso como bajo ataque */
    res->under_attack = 1;
    res->attack_time  = time(NULL);
    strncpy(res->attacker, attacker->username, MAX_USERNAME - 1);

    pthread_mutex_unlock(&rooms_mutex);

    /* Notificar a TODOS en la sala (incluido el atacante) */
    char event_msg[256];
    snprintf(event_msg, sizeof(event_msg),
             "EVENT ATTACK %d %s\n", resource_id, attacker->username);
    room_broadcast(attacker->room_id, event_msg, NULL);

    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg),
             "ATAQUE en recurso %d por '%s'", resource_id, attacker->username);
    log_event(LOG_WARN, attacker->client_ip, attacker->client_port, log_msg);

    return 0;
}

/* ── resource_defend() ───────────────────────────────────────────────────── */

int resource_defend(Player *defender, int resource_id) {
    if (!defender->in_room) return -1;

    pthread_mutex_lock(&rooms_mutex);

    Room *room = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == defender->room_id) {
            room = &rooms[i]; break;
        }
    }

    if (room == NULL || room->state != ROOM_RUNNING) {
        pthread_mutex_unlock(&rooms_mutex);
        return -1;
    }

    if (resource_id < 0 || resource_id >= NUM_RESOURCES) {
        pthread_mutex_unlock(&rooms_mutex);
        return -1;
    }

    Resource *res = &room->resources[resource_id];

    if (!res->under_attack) {
        pthread_mutex_unlock(&rooms_mutex);
        return -2;  /* No hay ataque activo en este recurso */
    }

    double dist = distance(defender->x, defender->y, res->x, res->y);
    if (dist > DETECTION_RADIUS) {
        pthread_mutex_unlock(&rooms_mutex);
        return -3;  /* Muy lejos del recurso */
    }

    /* Mitigar el ataque */
    res->under_attack = 0;
    memset(res->attacker, 0, MAX_USERNAME);

    pthread_mutex_unlock(&rooms_mutex);

    /* Notificar a todos */
    char event_msg[256];
    snprintf(event_msg, sizeof(event_msg),
             "EVENT DEFENDED %d %s\n", resource_id, defender->username);
    room_broadcast(defender->room_id, event_msg, NULL);

    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg),
             "Recurso %d DEFENDIDO por '%s'", resource_id, defender->username);
    log_event(LOG_INFO, defender->client_ip, defender->client_port, log_msg);

    return 0;
}

/* ──  room_broadcast() ───────────────────────────────────────────────────── */

void room_broadcast(int room_id, const char *message, Player *exclude) {
    pthread_mutex_lock(&rooms_mutex);

    Room *room = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == room_id) {
            room = &rooms[i]; break;
        }
    }

    if (room != NULL) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (room->players[i] == NULL) continue;
            if (room->players[i] == exclude) continue;  /* Saltamos al excluido */
            send_msg(room->players[i]->socket_fd, message);
        }
    }

    pthread_mutex_unlock(&rooms_mutex);
}

/* ──  room_broadcast_role() ──────────────────────────────────────────────── */
/*
 * Igual que room_broadcast() pero solo envía a jugadores con el rol dado.
 * Uso: difundir resultado de SCAN solo a los demás atacantes de la sala,
 * de modo que todos los atacantes vean los mismos recursos descubiertos.
 */
void room_broadcast_role(int room_id, const char *message,
                         Player *exclude, PlayerRole role) {
    pthread_mutex_lock(&rooms_mutex);

    Room *room = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == room_id) {
            room = &rooms[i]; break;
        }
    }

    if (room != NULL) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (room->players[i] == NULL)          continue;
            if (room->players[i] == exclude)        continue;  /* emisor excluido */
            if (room->players[i]->role != role)     continue;  /* solo el rol pedido */
            send_msg(room->players[i]->socket_fd, message);
        }
    }

    pthread_mutex_unlock(&rooms_mutex);
}

/* ── room_remove_player() ────────────────────────────────────────────────── */

void room_remove_player(Player *player) {
    if (!player->in_room) return;

    pthread_mutex_lock(&rooms_mutex);

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id != player->room_id) continue;

        Room *room = &rooms[i];
        for (int j = 0; j < MAX_PLAYERS; j++) {
            if (room->players[j] == player) {
                room->players[j] = NULL;
                room->player_count--;
                if (player->role == ROLE_ATTACKER) room->attacker_count--;
                if (player->role == ROLE_DEFENDER) room->defender_count--;
                break;
            }
        }

        /* Si la sala quedó vacía y no está en espera, marcarla como inactiva */
        if (room->player_count == 0 && room->state != ROOM_WAITING) {
            room->id    = 0;
            room->state = ROOM_WAITING;
            char log_msg[64];
            snprintf(log_msg, sizeof(log_msg),
                     "Sala %d eliminada — sin jugadores", player->room_id);
            log_event(LOG_INFO, NULL, 0, log_msg);
        }
        break;
    }

    player->in_room = 0;
    player->room_id = -1;
    pthread_mutex_unlock(&rooms_mutex);
}

/* ──  room_check_timers() ──────────────────────────────────────────────────────── */

/*
 * Revisa TODOS los timers activos:
 *  1. Si un recurso lleva más de ATTACK_TIMEOUT segundos bajo ataque sin
 *     ser defendido → emite EVENT ATTACK_TIMEOUT <id> + EVENT GAME_OVER ATTACKER.
 *  2. Si una sala RUNNING lleva más de game_timeout segundos activa
 *     → emite EVENT GAME_OVER DEFENDER (defensa exitosa por tiempo).
 *
 * Esta función se llama desde un hilo dedicado cada segundo.
 */
void room_check_timers(int game_timeout_secs) {
    time_t now = time(NULL);

    pthread_mutex_lock(&rooms_mutex);

    for (int i = 0; i < MAX_ROOMS; i++) {
        Room *room = &rooms[i];
        if (room->id == 0 || room->state != ROOM_RUNNING) continue;

        /* ── 1. Timer global de partida (GAME_TIMEOUT) ──────────────── */
        int max_secs = (room->game_timeout > 0) ? room->game_timeout : game_timeout_secs;
        if (room->game_start_time > 0 &&
            (now - room->game_start_time) >= max_secs) {

            room->state = ROOM_FINISHED;
            pthread_mutex_unlock(&rooms_mutex);

            /* Victoria defensora: tiempo agotado */
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg),
                     "Sala %d: GAME_TIMEOUT — victoria DEFENDER", room->id);
            log_event(LOG_INFO, NULL, 0, log_msg);

            room_broadcast(room->id, "EVENT GAME_OVER DEFENDER\n", NULL);

            pthread_mutex_lock(&rooms_mutex);
            continue;  /* Pasar a la siguiente sala */
        }

        /* ── 2. Timer de ataque por recurso (ATTACK_TIMEOUT) ────────── */
        for (int r = 0; r < NUM_RESOURCES; r++) {
            Resource *res = &room->resources[r];
            if (!res->under_attack) continue;

            double elapsed = difftime(now, res->attack_time);
            if (elapsed < ATTACK_TIMEOUT) continue;

            /* Timer expirado: el atacante gana este recurso */
            int room_id  = room->id;
            int res_id   = res->id;

            /* Marcar recurso como "comprometido" (no bajo ataque activo,
             * pero la sala termina) */
            res->under_attack = 0;
            room->state       = ROOM_FINISHED;

            pthread_mutex_unlock(&rooms_mutex);

            /* Emitir EVENT ATTACK_TIMEOUT ANTES de GAME_OVER (RFC_v2) */
            char timeout_msg[64];
            snprintf(timeout_msg, sizeof(timeout_msg),
                     "EVENT ATTACK_TIMEOUT %d\n", res_id);
            room_broadcast(room_id, timeout_msg, NULL);

            room_broadcast(room_id, "EVENT GAME_OVER ATTACKER\n", NULL);

            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg),
                     "Sala %d: ATTACK_TIMEOUT en recurso %d — victoria ATTACKER",
                     room_id, res_id);
            log_event(LOG_WARN, NULL, 0, log_msg);

            pthread_mutex_lock(&rooms_mutex);
            break;  /* Una sala solo puede tener un timer expirando a la vez */
        }
    }

    pthread_mutex_unlock(&rooms_mutex);
}

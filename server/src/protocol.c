/*
 * protocol.c — Implementación del Parser y Dispatcher del Protocolo CGSP
 *
 * Este módulo es el "intérprete" del servidor: recibe texto del cliente,
 * lo divide en tokens, identifica el comando y llama a la función correcta.
 *
 * MÁQUINA DE ESTADOS DEL CLIENTE:
 *
 *   [CONECTADO] → espera AUTH
 *       │
 *       ▼ AUTH exitoso
 *   [AUTENTICADO] → puede hacer LIST_ROOMS, CREATE_ROOM, JOIN
 *       │
 *       ▼ JOIN exitoso
 *   [EN_SALA] → puede hacer START
 *       │
 *       ▼ partida iniciada
 *   [EN_PARTIDA] → MOVE, SCAN (atacante), ATTACK (atacante),
 *                  DEFEND (defensor), STATUS, QUIT
 *
 * VALIDACIONES:
 *   Antes de ejecutar cualquier comando verificamos:
 *   1. ¿Está el cliente autenticado? (player->authenticated)
 *   2. ¿Tiene el rol correcto para este comando?
 *      (ATTACK solo atacantes, DEFEND solo defensores)
 *   3. ¿Está en el estado de sala correcto?
 *      (MOVE solo si la partida está en RUNNING)
 */

#include "../include/protocol.h"
#include "../include/game_logic.h"
#include "../include/identity.h"
#include "../include/logger.h"
#include "../include/net_utils.h"   /* BUFFER_SIZE */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>   /* atoi */
#include <unistd.h>   /* write */

/* ══════════════════════════════════════════════════════════════════════════
 * HELPERS DE ENVÍO DE MENSAJES
 * ══════════════════════════════════════════════════════════════════════════ */

void send_ok(int sockfd, const char *message) {
    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "OK %s\n", message);
    write(sockfd, buf, strlen(buf));
}

void send_err(int sockfd, int code, const char *message,
              const char *client_ip, int client_port) {
    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "ERR %d %s\n", code, message);
    write(sockfd, buf, strlen(buf));

    /* Registrar el error con el log */
    char log_msg[BUFFER_SIZE];
    snprintf(log_msg, sizeof(log_msg), "ERR %d %s", code, message);
    log_event(LOG_WARN, client_ip, client_port, log_msg);
}

void send_event(int sockfd, const char *event_type, const char *params) {
    char buf[BUFFER_SIZE];
    if (params && strlen(params) > 0) {
        snprintf(buf, sizeof(buf), "EVENT %s %s\n", event_type, params);
    } else {
        snprintf(buf, sizeof(buf), "EVENT %s\n", event_type);
    }
    write(sockfd, buf, strlen(buf));
}

/* ══════════════════════════════════════════════════════════════════════════
 * PARSER DE MENSAJES
 * ══════════════════════════════════════════════════════════════════════════ */

int parse_message(char *raw, ParsedMessage *out) {
    if (raw == NULL || out == NULL) return -1;

    /*
     * Eliminar \r\n del final (los clientes telnet envían \r\n, los
     * clientes Python pueden enviar solo \n).
     * strcspn retorna el índice del primer carácter que pertenezca al
     * conjunto dado — aquí buscamos el primer \r o \n.
     */
    raw[strcspn(raw, "\r\n")] = '\0';

    /* Si el mensaje quedó vacío después de limpiar, es inválido */
    if (strlen(raw) == 0) return -1;

    /* Inicializar la estructura de salida */
    memset(out, 0, sizeof(ParsedMessage));
    out->param_count = 0;

    /*
     * strtok() divide el string por espacios.
     * IMPORTANTE: strtok() modifica el string original (pone '\0' donde
     * había espacios). Por eso trabajamos con `raw` que ya fue modificado.
     *
     * Primera llamada: extrae el comando
     */
    char *token = strtok(raw, " ");
    if (token == NULL) return -1;

    strncpy(out->cmd, token, sizeof(out->cmd) - 1);

    /* Convertir el comando a mayúsculas para comparaciones insensibles */
    for (int i = 0; out->cmd[i]; i++) {
        if (out->cmd[i] >= 'a' && out->cmd[i] <= 'z') {
            out->cmd[i] -= 32;
        }
    }

    /*
     * Llamadas siguientes: extrae los parámetros.
     * strtok(NULL, " ") continúa donde se quedó en la última llamada.
     */
    while ((token = strtok(NULL, " ")) != NULL &&
           out->param_count < MAX_TOKENS) {
        strncpy(out->params[out->param_count], token,
                sizeof(out->params[0]) - 1);
        out->param_count++;
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * HANDLERS DE CADA COMANDO
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Manejador: AUTH ─────────────────────────────────────────────────────── */
static int handle_auth(Player *player, ParsedMessage *msg) {
    /* Validar que el jugador no esté ya autenticado */
    if (player->authenticated) {
        send_err(player->socket_fd, 403, "Ya estas autenticado",
                 player->client_ip, player->client_port);
        return 0;
    }

    /* Validar parámetros: AUTH necesita usuario y contraseña */
    if (msg->param_count < 2) {
        send_err(player->socket_fd, 400, "Uso: AUTH <usuario> <password>",
                 player->client_ip, player->client_port);
        return 0;
    }

    const char *username = msg->params[0];
    const char *password = msg->params[1];

    /* Log de la petición entrante (SIN mostrar la contraseña) */
    {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "AUTH %s [password omitido]", username);
        log_event(LOG_INFO, player->client_ip, player->client_port, log_msg);
    }

    /*
     * Consultar al servicio de identidad externo.
     * Esta llamada puede fallar si el servicio está caído —
     * en ese caso respondemos 503 y NO hacemos crash.
     */
    PlayerRole role;
    IdentityResult result = identity_authenticate(username, password, &role);

    switch (result) {
        case IDENTITY_OK:
            /* Guardar datos del jugador autenticado */
            strncpy(player->username, username, MAX_USERNAME - 1);
            player->role          = role;
            player->authenticated = 1;
            player->room_id       = -1;
            player->in_room       = 0;

            {
                const char *role_str = (role == ROLE_ATTACKER) ? "ATTACKER" : "DEFENDER";
                char ok_msg[64];
                char role_msg[64];
                char log_msg2[256];

                /* Responder al cliente con su rol */
                snprintf(ok_msg, sizeof(ok_msg), "Bienvenido %s", username);
                send_ok(player->socket_fd, ok_msg);

                snprintf(role_msg, sizeof(role_msg), "ROLE %s\n", role_str);
                write(player->socket_fd, role_msg, strlen(role_msg));

                snprintf(log_msg2, sizeof(log_msg2),
                         "AUTH OK: '%s' rol %s", username, role_str);
                log_event(LOG_INFO, player->client_ip, player->client_port, log_msg2);
            }
            break;

        case IDENTITY_WRONG_CREDS:
            send_err(player->socket_fd, 401, "Credenciales incorrectas",
                     player->client_ip, player->client_port);
            break;

        case IDENTITY_SERVICE_DOWN:
        case IDENTITY_PARSE_ERROR:
            send_err(player->socket_fd, 503,
                     "Servicio de identidad no disponible, intenta luego",
                     player->client_ip, player->client_port);
            break;
    }

    return 0;
}

/* ── Manejador: LIST_ROOMS ───────────────────────────────────────────────── */
static int handle_list_rooms(Player *player) {
    log_event(LOG_INFO, player->client_ip, player->client_port, "LIST_ROOMS");

    char room_list_buf[BUFFER_SIZE * 4];
    memset(room_list_buf, 0, sizeof(room_list_buf));
    int count = room_list(room_list_buf, sizeof(room_list_buf));

    if (count == 0) {
        send_ok(player->socket_fd, "No hay salas activas");
    } else {
        /* Enviar el prefijo de cantidad y luego la lista */
        char header[64];
        snprintf(header, sizeof(header), "ROOM_LIST %d\n", count);
        write(player->socket_fd, header, strlen(header));
        write(player->socket_fd, room_list_buf, strlen(room_list_buf));
    }
    return 0;
}

/* ── Manejador: CREATE_ROOM ──────────────────────────────────────────────── */
static int handle_create_room(Player *player) {
    log_event(LOG_INFO, player->client_ip, player->client_port, "CREATE_ROOM");

    int room_id = room_create();
    if (room_id < 0) {
        send_err(player->socket_fd, 409, "No se pudo crear la sala: limite alcanzado",
                 player->client_ip, player->client_port);
        return 0;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "ROOM_CREATED %d\n", room_id);
    write(player->socket_fd, msg, strlen(msg));

    char log_msg[64];
    snprintf(log_msg, sizeof(log_msg), "Sala %d creada por '%s'",
             room_id, player->username);
    log_event(LOG_INFO, player->client_ip, player->client_port, log_msg);

    return 0;
}

/* ── Manejador: JOIN ─────────────────────────────────────────────────────── */
static int handle_join(Player *player, ParsedMessage *msg) {
    if (msg->param_count < 1) {
        send_err(player->socket_fd, 400, "Uso: JOIN <room_id>",
                 player->client_ip, player->client_port);
        return 0;
    }

    if (player->in_room) {
        send_err(player->socket_fd, 403, "Ya estas en una sala",
                 player->client_ip, player->client_port);
        return 0;
    }

    int room_id = atoi(msg->params[0]);

    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "JOIN sala=%d usuario='%s'",
             room_id, player->username);
    log_event(LOG_INFO, player->client_ip, player->client_port, log_msg);

    int result = room_join(room_id, player);
    switch (result) {
        case  0: send_ok(player->socket_fd, "Te uniste a la sala");  break;
        case -1: send_err(player->socket_fd, 404, "Sala no encontrada",
                          player->client_ip, player->client_port); break;
        case -2: send_err(player->socket_fd, 409, "Sala llena",
                          player->client_ip, player->client_port); break;
        case -3: send_err(player->socket_fd, 409, "Partida ya iniciada",
                          player->client_ip, player->client_port); break;
    }
    return 0;
}

/* ── Manejador: START ────────────────────────────────────────────────────── */
static int handle_start(Player *player) {
    if (!player->in_room) {
        send_err(player->socket_fd, 403, "No estas en ninguna sala",
                 player->client_ip, player->client_port);
        return 0;
    }

    log_event(LOG_INFO, player->client_ip, player->client_port, "START");

    int started = room_try_start(player->room_id);
    if (started) {
        send_ok(player->socket_fd, "Partida iniciada");
    } else {
        send_err(player->socket_fd, 409,
                 "Se necesita al menos 1 atacante y 1 defensor para iniciar",
                 player->client_ip, player->client_port);
    }
    return 0;
}

/* ── Manejador: MOVE ─────────────────────────────────────────────────────── */
static int handle_move(Player *player, ParsedMessage *msg) {
    if (msg->param_count < 2) {
        send_err(player->socket_fd, 400, "Uso: MOVE <dx> <dy>",
                 player->client_ip, player->client_port);
        return 0;
    }

    int dx = atoi(msg->params[0]);
    int dy = atoi(msg->params[1]);

    player_move(player, dx, dy);

    char ok_msg[64];
    snprintf(ok_msg, sizeof(ok_msg), "Posicion: %d %d", player->x, player->y);
    send_ok(player->socket_fd, ok_msg);

    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg),
             "MOVE dx=%d dy=%d → pos(%d,%d) usuario='%s'",
             dx, dy, player->x, player->y, player->username);
    log_event(LOG_INFO, player->client_ip, player->client_port, log_msg);

    return 0;
}

/* ── Manejador: SCAN ─────────────────────────────────────────────────────── */
static int handle_scan(Player *player) {
    if (player->role != ROLE_ATTACKER) {
        send_err(player->socket_fd, 403, "Solo los atacantes pueden usar SCAN",
                 player->client_ip, player->client_port);
        return 0;
    }

    char found_buf[BUFFER_SIZE];
    memset(found_buf, 0, sizeof(found_buf));
    int found = player_scan(player, found_buf, sizeof(found_buf));

    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg),
             "SCAN pos(%d,%d) → %d recursos encontrados",
             player->x, player->y, found);
    log_event(LOG_INFO, player->client_ip, player->client_port, log_msg);

    if (found > 0) {
        write(player->socket_fd, found_buf, strlen(found_buf));
    } else {
        send_ok(player->socket_fd, "SCAN: no se encontraron recursos cercanos");
    }
    return 0;
}

/* ── Manejador: ATTACK ───────────────────────────────────────────────────── */
static int handle_attack(Player *player, ParsedMessage *msg) {
    if (player->role != ROLE_ATTACKER) {
        send_err(player->socket_fd, 403, "Solo los atacantes pueden usar ATTACK",
                 player->client_ip, player->client_port);
        return 0;
    }
    if (msg->param_count < 1) {
        send_err(player->socket_fd, 400, "Uso: ATTACK <resource_id>",
                 player->client_ip, player->client_port);
        return 0;
    }

    int res_id = atoi(msg->params[0]);
    int result = resource_attack(player, res_id);

    switch (result) {
        case  0: send_ok(player->socket_fd, "Ataque lanzado"); break;
        case -1: send_err(player->socket_fd, 404, "Recurso no encontrado",
                          player->client_ip, player->client_port); break;
        case -2: send_err(player->socket_fd, 409, "Recurso ya bajo ataque",
                          player->client_ip, player->client_port); break;
        case -3: send_err(player->socket_fd, 409, "Debes estar cerca del recurso para atacarlo",
                          player->client_ip, player->client_port); break;
    }
    return 0;
}

/* ── Manejador: DEFEND ───────────────────────────────────────────────────── */
static int handle_defend(Player *player, ParsedMessage *msg) {
    if (player->role != ROLE_DEFENDER) {
        send_err(player->socket_fd, 403, "Solo los defensores pueden usar DEFEND",
                 player->client_ip, player->client_port);
        return 0;
    }
    if (msg->param_count < 1) {
        send_err(player->socket_fd, 400, "Uso: DEFEND <resource_id>",
                 player->client_ip, player->client_port);
        return 0;
    }

    int res_id = atoi(msg->params[0]);
    int result = resource_defend(player, res_id);

    switch (result) {
        case  0: send_ok(player->socket_fd, "Recurso defendido"); break;
        case -1: send_err(player->socket_fd, 404, "Recurso no encontrado",
                          player->client_ip, player->client_port); break;
        case -2: send_err(player->socket_fd, 409, "No hay ataque activo en ese recurso",
                          player->client_ip, player->client_port); break;
        case -3: send_err(player->socket_fd, 409, "Debes estar cerca del recurso para defenderlo",
                          player->client_ip, player->client_port); break;
    }
    return 0;
}

/* ── Manejador: STATUS ───────────────────────────────────────────────────── */
static int handle_status(Player *player) {
    char status_msg[256];
    const char *role_str = (player->role == ROLE_ATTACKER) ? "ATTACKER" : "DEFENDER";
    snprintf(status_msg, sizeof(status_msg),
             "STATUS usuario=%s rol=%s pos=(%d,%d) sala=%d",
             player->username, role_str,
             player->x, player->y,
             player->room_id);
    send_ok(player->socket_fd, status_msg);
    log_event(LOG_INFO, player->client_ip, player->client_port, "STATUS");
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * DISPATCHER PRINCIPAL
 * ══════════════════════════════════════════════════════════════════════════ */

int handle_client_message(Player *player, ParsedMessage *msg) {
    /* Log de cada petición recibida */
    char log_msg[BUFFER_SIZE];
    snprintf(log_msg, sizeof(log_msg), "← %s", msg->cmd);
    for (int i = 0; i < msg->param_count; i++) {
        /* No loguear contraseñas */
        if (strcmp(msg->cmd, "AUTH") == 0 && i == 1) {
            strncat(log_msg, " [***]", sizeof(log_msg) - strlen(log_msg) - 1);
        } else {
            strncat(log_msg, " ", sizeof(log_msg) - strlen(log_msg) - 1);
            strncat(log_msg, msg->params[i], sizeof(log_msg) - strlen(log_msg) - 1);
        }
    }
    log_event(LOG_INFO, player->client_ip, player->client_port, log_msg);

    /* ── Comando QUIT: siempre permitido ──────────────────────────────── */
    if (strcmp(msg->cmd, "QUIT") == 0) {
        send_ok(player->socket_fd, "Hasta luego");
        return -1;  /* Señal para cerrar la conexión */
    }

    /* ── AUTH: único comando que no requiere autenticación previa ──────── */
    if (strcmp(msg->cmd, "AUTH") == 0) {
        return handle_auth(player, msg);
    }

    /* ── Todos los demás comandos requieren estar autenticado ─────────── */
    if (!player->authenticated) {
        send_err(player->socket_fd, 401,
                 "Debes autenticarte primero con AUTH <usuario> <password>",
                 player->client_ip, player->client_port);
        return 0;
    }

    /* ── Dispatch por comando ─────────────────────────────────────────── */
    if (strcmp(msg->cmd, "LIST_ROOMS")   == 0) return handle_list_rooms(player);
    if (strcmp(msg->cmd, "CREATE_ROOM")  == 0) return handle_create_room(player);
    if (strcmp(msg->cmd, "JOIN")         == 0) return handle_join(player, msg);
    if (strcmp(msg->cmd, "START")        == 0) return handle_start(player);
    if (strcmp(msg->cmd, "MOVE")         == 0) return handle_move(player, msg);
    if (strcmp(msg->cmd, "SCAN")         == 0) return handle_scan(player);
    if (strcmp(msg->cmd, "ATTACK")       == 0) return handle_attack(player, msg);
    if (strcmp(msg->cmd, "DEFEND")       == 0) return handle_defend(player, msg);
    if (strcmp(msg->cmd, "STATUS")       == 0) return handle_status(player);

    /* Comando desconocido */
    send_err(player->socket_fd, 400,
             "Comando desconocido. Comandos validos: AUTH LIST_ROOMS CREATE_ROOM JOIN START MOVE SCAN ATTACK DEFEND STATUS QUIT",
             player->client_ip, player->client_port);
    return 0;
}

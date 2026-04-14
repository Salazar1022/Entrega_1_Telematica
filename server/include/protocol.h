/*
 * protocol.h — Parser y Dispatcher del Protocolo CGSP
 *
 * CGSP: CyberGame Socket Protocol
 *
 * Define el vocabulario completo de mensajes y las funciones para
 * parsear los comandos entrantes y ejecutar la acción correspondiente.
 *
 * FORMATO DE MENSAJES:
 *   Texto plano, terminado en '\n'. Un mensaje por línea.
 *   Campos separados por espacios.
 *
 *   Cliente - Servidor:
 *     AUTH <usuario> <password>
 *     LIST_ROOMS
 *     CREATE_ROOM
 *     JOIN <room_id>
 *     START
 *     MOVE <dx> <dy>
 *     ATTACK <resource_id>
 *     DEFEND <resource_id>
 *     SCAN
 *     STATUS
 *     QUIT
 *
 *   Servidor - Cliente:
 *     OK <mensaje>
 *     ERR <código> <mensaje>
 *     ROLE <ATTACKER|DEFENDER>
 *     ROOM_CREATED <room_id>
 *     ROOM_LIST <n> <id:jugadores> [<id:jugadores> ...]
 *     EVENT ATTACK <resource_id> <atacante>
 *     EVENT RESOURCE_FOUND <resource_id> <x> <y>
 *     EVENT DEFENDED <resource_id> <defensor>
 *     EVENT PLAYER_JOINED <usuario> <rol>
 *     EVENT GAME_STARTED
 *     EVENT GAME_OVER <ATTACKER|DEFENDER>
 *
 * CÓDIGOS DE ERROR:
 *   400 - Formato de mensaje inválido
 *   401 - Autenticación fallida
 *   403 - Acción no permitida para tu rol o estado
 *   404 - Sala o recurso no encontrado
 *   409 - Conflicto (sala llena, partida ya iniciada, etc.)
 *   500 - Error interno del servidor
 *   503 - Servicio de identidad no disponible
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "game_logic.h"   /* Player */

/* Número máximo de tokens (palabras) que puede tener un mensaje */
#define MAX_TOKENS  8

/*
 * ParsedMessage — Resultado de parsear una línea del cliente
 *
 * Después de llamar a parse_message(), tenemos:
 *   cmd - el comando principal (ej: "AUTH", "MOVE")
 *   params - los parámetros (ej: params[0]="juan", params[1]="pass")
 *   param_count - cuántos parámetros había
 */
typedef struct {
    char  cmd[32];                   /* Comando extraído                */
    char  params[MAX_TOKENS][256];   /* Parámetros del comando          */
    int   param_count;               /* Cantidad de parámetros          */
} ParsedMessage;

/*
 * parse_message()
 *
 * Divide una línea de texto en comando + parámetros.
 * Elimina el '\n' y '\r' del final si existen.
 *
 * Ejemplo:
 *   Input - "MOVE 5 -3\n"
 *   Output - cmd="MOVE", params={"5","-3"}, param_count=2
 *
 * Parámetros:
 *   raw - línea recibida del cliente (se modifica internamente)
 *   out - estructura donde se guardan los resultados
 *
 * Retorna:
 *   0 - mensaje bien formado
 *  -1 - mensaje vacío o inválido
 */
int parse_message(char *raw, ParsedMessage *out);

/*
 * handle_client_message()
 *
 * Función principal del protocolo: recibe un mensaje ya parseado y
 * ejecuta la acción correspondiente según el estado del jugador.
 *
 * Internamente valida:
 *   - Que el jugador esté autenticado antes de hacer cualquier otra cosa
 *   - Que el rol del jugador sea correcto para el comando
 *     (ej: un DEFENDER no puede hacer ATTACK)
 *   - Que la sala exista y la partida esté en el estado correcto
 *
 * Parámetros:
 *   player - jugador que envió el mensaje
 *   msg - mensaje ya parseado
 *
 * Retorna:
 *   0 - conexión sigue activa
 *  -1 - el cliente envió QUIT o hubo un error fatal (cerrar conexión)
 */
int handle_client_message(Player *player, ParsedMessage *msg);

/*
 * send_ok() / send_err()
 *
 * Helpers para enviar respuestas estándar al cliente.
 * Añaden automáticamente el '\n' al final.
 *
 * send_err() también registra el error en el log.
 */
void send_ok(int sockfd, const char *message);
void send_err(int sockfd, int code, const char *message,
              const char *client_ip, int client_port);

/*
 * send_event()
 *
 * Envía un mensaje de evento a un socket específico.
 * Formato: "EVENT <tipo> <params...>\n"
 */
void send_event(int sockfd, const char *event_type, const char *params);

#endif /* PROTOCOL_H */

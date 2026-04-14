/*
 * game_logic.h — Estructuras de Datos y Lógica del Juego
 *
 * Define todas las estructuras que representan el estado del juego:
 *   - Player: un jugador conectado (socket, usuario, rol, posición)
 *   - Resource: un recurso crítico en el mapa (servidor/router)
 *   - Room: una sala de juego con sus jugadores y recursos
 *
 * EL MAPA:
 *   Es un plano 2D de MAP_WIDTH x MAP_HEIGHT unidades.
 *   Los recursos aparecen en posiciones aleatorias al crear la sala.
 *   Los atacantes empiezan en (0,0) y deben MOVERSE para encontrar recursos.
 *   Los defensores conocen desde el inicio las coordenadas de los recursos.
 *
 * RADIO DE DETECCIÓN:
 *   Cuando un atacante hace SCAN, si está a menos de DETECTION_RADIUS
 *   unidades de un recurso, es notificado de su presencia.
 *
 * CONCURRENCIA:
 *   Cada Room tiene un mutex (room_mutex). Cualquier operación que
 *   lea o modifique la sala debe adquirir este mutex primero.
 *   Esto es CRÍTICO con múltiples hilos.
 */

#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <pthread.h>  /* pthread_mutex_t */
#include <time.h>     /* time_t          */


#define MAP_WIDTH 100      /* Ancho del plano de juego          */
#define MAP_HEIGHT 100     /* Alto del plano de juego           */
#define MAX_PLAYERS 10     /* Máx. jugadores por sala           */
#define MAX_ROOMS 20       /* Máx. salas simultáneas            */
#define NUM_RESOURCES 2    /* Recursos críticos por sala        */
#define DETECTION_RADIUS 12 /* Radio de detección al hacer SCAN  */
#define ATTACK_TIMEOUT 30  /* Segundos para defender un ataque  */
#define GAME_TIMEOUT 600   /* Duración máx. de partida (seg)    */
#define IDLE_TIMEOUT_DEF 600 /* Inactividad del cliente (seg)     */
#define MAX_USERNAME 64    /* Largo máximo del nombre de usuario*/
#define MAX_ROOM_ID 16     /* Largo máximo del ID de sala       */

/* Rol del jugador — viene del servicio de identidad */
typedef enum {
    ROLE_ATTACKER,   /* Atacante: se mueve y ataca recursos */
    ROLE_DEFENDER,   /* Defensor: protege recursos críticos */
    ROLE_UNDEFINED   /* Aún no autenticado                  */
} PlayerRole;

/* Estado de la sala de juego */
typedef enum {
    ROOM_WAITING,    /* Esperando jugadores (mín: 1 atacante + 1 defensor) */
    ROOM_RUNNING,    /* Partida en curso                                   */
    ROOM_FINISHED    /* Partida terminada                                  */
} RoomState;

/*
 * Resource — Recurso Crítico en el mapa
 * Representa un servidor/router que puede ser atacado y defendido.
 */

typedef struct {
    int id;              /* Identificador único del recurso (0, 1, ...) */
    int x;              /* Posición X en el mapa                        */
    int y;              /* Posición Y en el mapa                        */
    int under_attack;   /* 1 si está siendo atacado actualmente         */
    time_t attack_time;  /* Timestamp del inicio del ataque              */
    char attacker[MAX_USERNAME]; /* Quién está atacando                  */
} Resource;

/*
 * Player — Jugador conectado al servidor
 * La estructura vive mientras dure la conexión del cliente (en el hilo).
 */
typedef struct {
    int socket_fd;             /* Descriptor del socket del cliente    */
    char username[MAX_USERNAME];/* Nombre de usuario autenticado        */
    PlayerRole role;              /* ATTACKER o DEFENDER                  */
    int x;                     /* Posición actual X en el mapa         */
    int y;                     /* Posición actual Y en el mapa         */
    int authenticated;         /* 1 si ya pasó por AUTH correctamente  */
    int in_room;               /* 1 si ya se unió a una sala           */
    int room_id;               /* ID de la sala actual (-1 si ninguna) */
    char client_ip[48];         /* IP del cliente (para logging)        */
    int client_port;           /* Puerto del cliente (para logging)    */
} Player;

/*
 * Room — Sala de Juego
 * Contenedor de todos los jugadores y recursos de una partida.
 */
typedef struct {
    int id;                        /* ID numérico de la sala      */
    RoomState state;                     /* Estado actual               */
    Player *players[MAX_PLAYERS];      /* Punteros a jugadores        */
    int player_count;              /* Cuántos jugadores hay       */
    int attacker_count;            /* Cuántos son atacantes       */
    int defender_count;            /* Cuántos son defensores      */
    Resource resources[NUM_RESOURCES];  /* Recursos del mapa           */
    time_t game_start_time;           /* Timestamp de inicio (RUNNING)*/
    int game_timeout;             /* Duración máx. de partida (s) */
    pthread_mutex_t room_mutex;             /* Mutex para acceso seguro    */
} Room;

/*
 * game_init()
 * Inicializa el sistema de salas. Llámala UNA vez al arrancar el servidor.
 */
void game_init(void);

/*
 * room_create()
 * Crea una nueva sala y coloca los recursos críticos en el mapa.
 * Retorna el ID de la sala creada, o -1 si hay demasiadas salas.
 */
int room_create(void);

/*
 * room_join()
 * Añade un jugador autenticado a la sala especificada.
 * Retorna 0 si tuvo éxito, o un código de error negativo:
 *  -1 - sala no existe
 *  -2 - sala llena
 *  -3 - partida ya iniciada
 */
int room_join(int room_id, Player *player);

/*
 * room_list()
 * Llena el buffer `out` con la lista de salas activas en formato legible.
 * Retorna la cantidad de salas activas.
 */
int room_list(char *out, int out_size);

/*
 * room_try_start()
 * Intenta iniciar la partida si hay al menos 1 atacante y 1 defensor.
 * Si puede iniciar, pone la sala en ROOM_RUNNING y notifica a todos.
 * Retorna 1 si inició, 0 si todavía faltan jugadores.
 */
int room_try_start(int room_id);

/*
 * room_is_running()
 * Retorna 1 si la sala existe y está en ROOM_RUNNING, 0 en caso contrario.
 */
int room_is_running(int room_id);

/*
 * player_move()
 * Mueve al jugador (dx, dy) unidades. Clampea dentro de los límites del mapa.
 * Retorna 0 si OK, -1 si el jugador no está en partida.
 */
int player_move(Player *player, int dx, int dy);

/*
 * player_scan()
 * El atacante escanea su posición actual.
 * Si hay un recurso a distancia <= DETECTION_RADIUS, llena `found_msg`.
 * Retorna la cantidad de recursos encontrados.
 */
int player_scan(Player *player, char *found_msg, int msg_size);

/*
 * resource_attack()
 * El atacante lanza un ataque al recurso `resource_id`.
 * Notifica a TODOS los defensores de la sala con EVENT ATTACK.
 * Retorna 0 si OK, -1 si el recurso no existe/sala invalida,
 * -2 si ya está bajo ataque, -3 si el atacante está muy lejos.
 */
int resource_attack(Player *attacker, int resource_id);

/*
 * resource_defend()
 * El defensor mitiga el ataque al recurso `resource_id`.
 * Detiene el timer de ataque y notifica a todos con EVENT DEFENDED.
 * Retorna 0 si OK, -1 si el recurso no existe/sala invalida,
 * -2 si no hay ataque activo, -3 si el defensor está muy lejos.
 */
int resource_defend(Player *defender, int resource_id);

/*
 * room_broadcast()
 * Envía un mensaje a TODOS los jugadores de una sala (excepto opcionalmente
 * al jugador `exclude`, que puede ser NULL para enviar a todos).
 */
void room_broadcast(int room_id, const char *message, Player *exclude);

/*
 * room_broadcast_role()
 * Igual que room_broadcast pero solo envía a jugadores con el rol especificado.
 * Si exclude != NULL, ese jugador es omitido aunque tenga el rol correcto.
 * Uso típico: difundir resultado de SCAN solo a los demás atacantes.
 */
void room_broadcast_role(int room_id, const char *message,
                         Player *exclude, PlayerRole role);

/*
 * room_remove_player()
 * Elimina al jugador de la sala cuando se desconecta.
 * Thread-safe (usa el mutex de la sala).
 */
void room_remove_player(Player *player);

/*
 * room_check_timers()
 * Revisa todos los ataques activos (timer de 30 s) y el timer global de
 * partida (GAME_TIMEOUT). Debe llamarse periódicamente desde un hilo
 * dedicado. Emite EVENT ATTACK_TIMEOUT / EVENT GAME_OVER según corresponda.
 */
void room_check_timers(int game_timeout_secs);

#endif /* GAME_LOGIC_H */

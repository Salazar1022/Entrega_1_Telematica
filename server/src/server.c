/*
 * server.c — Servidor Central del Juego CyberGame (CGSP)
 *
 * PUNTO DE ENTRADA del servidor. Aquí vive el main().
 *
 * USO:
 *   ./server <puerto> <archivoDeLogs>
 *
 * Ejemplo:
 *   ./server 8080 server.log
 *
 * FUNCIONAMIENTO GENERAL:
 *
 *   1. Validar argumentos de consola
 *   2. Inicializar logger, sistema de juego, socket del servidor
 *   3. Entrar en el loop principal de accept()
 *   4. Por cada cliente nuevo → crear un hilo (pthread) que maneje
 *      TODA la sesión de ese cliente
 *   5. El hilo lee mensajes en un loop hasta que el cliente se
 *      desconecte o envíe QUIT
 *
 * POR QUÉ USAMOS HILOS (pthreads):
 *   accept() es bloqueante: espera hasta que un cliente se conecte.
 *   Si manejáramos todo en un solo hilo, mientras atendemos a un
 *   cliente, los demás quedarían esperando.
 *
 *   Con pthreads, cada cliente tiene su propio hilo. El hilo principal
 *   solo acepta conexiones y delega el manejo a hilos secundarios.
 *   El SO se encarga de ejecutarlos en paralelo.
 *
 * FLUJO DEL HILO POR CLIENTE:
 *
 *   pthread_create() → client_thread()
 *                          │
 *                          ├─ inicializar Player
 *                          │
 *                          └─ loop:
 *                               recv() → parse_message() → handle_client_message()
 *                               repetir hasta QUIT o desconexión
 *                          │
 *                          └─ limpiar: room_remove_player(), close(), free()
 *
 * MANEJO DE SEÑALES:
 *   Capturamos SIGINT (Ctrl+C) para cerrar el servidor de forma limpia:
 *   cerrar el log, liberar recursos, etc.
 */

#include <stdio.h>
#include <stdlib.h>      /* atoi, exit          */
#include <string.h>      /* memset              */
#include <unistd.h>      /* close, read         */
#include <signal.h>      /* signal, SIGINT      */
#include <pthread.h>     /* pthread_create, etc.*/
#include <errno.h>       /* errno, EAGAIN       */
#include <time.h>        /* time                */
#include <sys/socket.h>
#include <sys/time.h>    /* struct timeval      */
#include <netinet/in.h>

#include "../include/logger.h"
#include "../include/net_utils.h"
#include "../include/game_logic.h"
#include "../include/protocol.h"

/* Variable global para el señal handler */
static volatile int server_running = 1;
static int          server_fd      = -1;

/* Configuracion de timeouts leida de variables de entorno (RFC_v2) */
static int idle_timeout_secs = IDLE_TIMEOUT_DEF;  /* default: 600 s */
static int game_timeout_secs = GAME_TIMEOUT;       /* default: 600 s */

/* HILO DE CLIENTE — se ejecuta uno por cada conexión aceptada */

/*
 * ClientArgs — estructura que pasamos al hilo nuevo.
 * pthread_create() acepta un solo void* como argumento al hilo.
 * Empaquetamos lo que necesita el hilo en esta estructura.
 */
typedef struct {
    int              socket_fd;   /* Descriptor del socket del cliente  */
    struct sockaddr_in addr;      /* Dirección del cliente (IP + puerto) */
} ClientArgs;

static void *client_thread(void *arg) {
    ClientArgs *cargs = (ClientArgs *)arg;

    /*
     * PASO 1: Alocar e inicializar la estructura Player para este cliente.
     * Usamos malloc() porque el Player vive en el heap — si lo declaramos
     * en el stack del hilo y el hilo termina, la memoria se libera y
     * podría corromperse si otra parte del código aún tiene un puntero.
     */
    Player *player = (Player *)malloc(sizeof(Player));
    if (player == NULL) {
        log_event(LOG_ERROR, NULL, 0, "malloc() falló para Player — rechazando cliente");
        close(cargs->socket_fd);
        free(cargs);
        return NULL;
    }

    /* Inicializar todos los campos del Player */
    memset(player, 0, sizeof(Player));
    player->socket_fd     = cargs->socket_fd;
    player->role          = ROLE_UNDEFINED;
    player->authenticated = 0;
    player->in_room       = 0;
    player->room_id       = -1;
    player->x             = 0;
    player->y             = 0;

    /* Extraer IP y puerto del cliente para logging */
    get_client_info(&cargs->addr,
                    player->client_ip, sizeof(player->client_ip),
                    &player->client_port);

    free(cargs);  /* Ya copiamos los datos que necesitábamos */

    /*
     * Configurar SO_RCVTIMEO para detectar inactividad (RFC_v2).
     * Si el cliente no envía nada en `idle_timeout_secs` segundos,
     * read() retorna -1 con errno == EAGAIN o EWOULDBLOCK.
     */
    struct timeval tv;
    tv.tv_sec  = idle_timeout_secs;
    tv.tv_usec = 0;
    setsockopt(player->socket_fd, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&tv, sizeof(tv));

    /* Log de nueva conexión */
    char connect_msg[128];
    snprintf(connect_msg, sizeof(connect_msg),
             "Nueva conexion aceptada — fd=%d", player->socket_fd);
    log_event(LOG_INFO, player->client_ip, player->client_port, connect_msg);

    /* Enviar mensaje de bienvenida */
    const char *welcome =
        "OK Bienvenido al servidor CyberGame (CGSP v2.0). Usa AUTH <usuario> <password>\n";
    write(player->socket_fd, welcome, strlen(welcome));

    /*
     * PASO 2: Loop principal de lectura.
     *
     * read() es bloqueante: el hilo espera aquí hasta que el cliente
     * envíe datos. Cuando recibe datos, los procesa y vuelve a esperar.
     *
     * El loop termina cuando:
     *   a) handle_client_message() retorna -1 (cliente envió QUIT)
     *   b) read() retorna 0 → el cliente cerró la conexión (EOF)
     *   c) read() retorna <0 → error de socket (cliente se desconectó abruptamente)
     */
    char buffer[BUFFER_SIZE];
    ParsedMessage msg;

    while (1) {
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytes_received = read(player->socket_fd, buffer, sizeof(buffer) - 1);

        if (bytes_received <= 0) {
            /*
             * bytes_received == 0 - EOF (cliente cerró la conexión)
             * bytes_received <  0 - error o TIMEOUT de inactividad (RFC_v2)
             */
            if (bytes_received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    /* Idle timeout: enviar ERR 408 y cerrar hilo */
                    const char *tmsg =
                        "ERR 408 Timeout: conexion cerrada por inactividad\n";
                    write(player->socket_fd, tmsg, strlen(tmsg));
                    log_event(LOG_WARN, player->client_ip, player->client_port,
                              "ERR 408: cliente inactivo — conexion cerrada");
                } else {
                    log_event(LOG_WARN, player->client_ip, player->client_port,
                              "Error de lectura — conexion perdida abruptamente");
                }
            } else {
                log_event(LOG_INFO, player->client_ip, player->client_port,
                          "Cliente cerro la conexion (EOF)");
            }
            break;  /* Salir del loop - limpiar y terminar el hilo */
        }

        /*
         * Un cliente podría enviar múltiples comandos en un solo read().
         * Procesamos línea por línea dividiendo por '\n'.
         *
         * strtok_r es la versión thread-safe de strtok (usa un estado
         * interno `saveptr` en lugar de una variable estática global).
         */
        char *saveptr = NULL;
        char *line = strtok_r(buffer, "\n", &saveptr);

        while (line != NULL) {
            /* Parsear el mensaje */
            if (parse_message(line, &msg) != 0) {
                /* Línea vacía o malformada — ignorar silenciosamente */
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }

            /* Despachar al handler correspondiente */
            int ret = handle_client_message(player, &msg);

            if (ret == -1) {
                /* Cliente envió QUIT o error fatal → cerrar conexión */
                goto cleanup;  /* Salir de los dos loops a la vez */
            }

            line = strtok_r(NULL, "\n", &saveptr);
        }
    }

cleanup:
    /*
     * PASO 3: Limpieza al desconectarse.
     *
     * Orden importante:
     * 1. Quitar al jugador de su sala (si estaba en alguna)
     *    — esto notifica a los demás jugadores
     * 2. Cerrar el socket
     * 3. Liberar la memoria del Player
     */
    if (player->in_room) {
        char leave_event[256];
        snprintf(leave_event, sizeof(leave_event),
                 "EVENT PLAYER_LEFT %s\n", player->username);
        room_broadcast(player->room_id, leave_event, player);
        room_remove_player(player);
    }

    char disconnect_msg[128];
    snprintf(disconnect_msg, sizeof(disconnect_msg),
             "Conexion cerrada — usuario='%s'",
             strlen(player->username) > 0 ? player->username : "(no autenticado)");
    log_event(LOG_INFO, player->client_ip, player->client_port, disconnect_msg);

    close(player->socket_fd);
    free(player);

    return NULL;
}

/* MANEJADOR DE SEÑAL, Ctrl+C cierra el servidor limpiamente */

static void signal_handler(int sig) {
    (void)sig;  /* Suprimir warning de "parámetro no usado" */
    printf("\n[SERVER] Señal recibida. Cerrando servidor...\n");
    server_running = 0;
    if (server_fd >= 0) close(server_fd);
}

/* HILO DE TIMERS, verifica cada segundo los timers de ataque y de partida */

static void *timer_thread(void *arg) {
    (void)arg;
    while (server_running) {
        sleep(1);  /* Verificar cada segundo */
        room_check_timers(game_timeout_secs);
    }
    return NULL;
}

/* Main */

int main(int argc, char *argv[]) {
    /*
     * PASO 1: Validar argumentos de consola.
     * El servidor DEBE recibir exactamente: ./server <puerto> <archivoDeLogs>
     */
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <puerto> <archivoDeLogs>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 8080 server.log\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "ERROR: Puerto inválido: %s (debe ser 1-65535)\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    const char *log_file = argv[2];

    /*
     * Leer timeouts desde variables de entorno (RFC_v2).
     * Permet configurar sin recompilar.
     */
    const char *env_idle = getenv("CGSP_IDLE_TIMEOUT");
    if (env_idle != NULL && atoi(env_idle) > 0) {
        idle_timeout_secs = atoi(env_idle);
    }
    const char *env_game = getenv("CGSP_GAME_TIMEOUT");
    if (env_game != NULL && atoi(env_game) > 0) {
        game_timeout_secs = atoi(env_game);
    }

    /*
     * PASO 2: Inicializar el sistema de logging.
     */
    if (logger_init(log_file) != 0) {
        fprintf(stderr, "ERROR: No se pudo inicializar el logger con archivo '%s'\n",
                log_file);
        exit(EXIT_FAILURE);
    }

    log_event(LOG_INFO, NULL, 0, "=== Servidor CyberGame CGSP v2.0 iniciando ===");

    char cfg_msg[256];
    snprintf(cfg_msg, sizeof(cfg_msg),
             "Configuracion: idle_timeout=%ds  game_timeout=%ds",
             idle_timeout_secs, game_timeout_secs);
    log_event(LOG_INFO, NULL, 0, cfg_msg);
    printf("[SERVER] %s\n", cfg_msg);

    /* PASO 3: Capturar Ctrl+C para cierre limpio */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /*
     * Ignorar SIGPIPE — cuando el cliente se desconecta abruptamente
     * y write() intentaría escribirle.
     */
    signal(SIGPIPE, SIG_IGN);

    /* PASO 4: Inicializar el sistema de juego */
    game_init();

    /* PASO 5: Lanzar hilo de timers (ATTACK_TIMEOUT + GAME_TIMEOUT) */
    pthread_t timer_tid;
    if (pthread_create(&timer_tid, NULL, timer_thread, NULL) == 0) {
        pthread_detach(timer_tid);
        log_event(LOG_INFO, NULL, 0, "Hilo de timers iniciado");
    } else {
        log_event(LOG_WARN, NULL, 0, "No se pudo crear hilo de timers");
    }

    /* PASO 6: Crear el socket del servidor */
    server_fd = create_server_socket(port);
    if (server_fd < 0) {
        log_event(LOG_ERROR, NULL, 0, "No se pudo crear el socket del servidor. Abortando.");
        logger_close();
        exit(EXIT_FAILURE);
    }

    char start_msg[128];
    snprintf(start_msg, sizeof(start_msg),
             "Servidor escuchando en puerto %d — esperando conexiones...", port);
    log_event(LOG_INFO, NULL, 0, start_msg);
    printf("[SERVER] %s\n", start_msg);
    printf("[SERVER] Usa Ctrl+C para detener el servidor\n");

    /*
     * PASO 6: Loop principal de accept().
     *
     * accept() bloquea el hilo principal hasta que un cliente se conecta.
     * Cuando lo hace, retorna un nuevo socket (new_socket) exclusivo
     * para esa comunicación. El socket original (server_fd) sigue
     * disponible para aceptar más conexiones.
     *
     * Para cada cliente:
     *   1. Preparar los argumentos del hilo (ClientArgs)
     *   2. pthread_create() - lanza un hilo nuevo con client_thread()
     *   3. pthread_detach() - el hilo libera sus recursos automáticamente
     *                         al terminar (no necesitamos pthread_join())
     */
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int new_socket = accept(server_fd,
                                (struct sockaddr *)&client_addr,
                                &addr_len);

        if (new_socket < 0) {
            if (!server_running) break;  /* Cierre intencional */
            log_event(LOG_WARN, NULL, 0, "accept() falló — continuando el loop");
            continue;
        }

        /*
         * Empaquetar argumentos para el hilo.
         * Usamos malloc() para que los datos sobrevivan hasta que el hilo
         * los lea — si usáramos el stack de main(), la variable podría
         * cambiar antes de que el hilo la lea.
         */
        ClientArgs *cargs = (ClientArgs *)malloc(sizeof(ClientArgs));
        if (cargs == NULL) {
            log_event(LOG_ERROR, NULL, 0, "malloc() falló para ClientArgs — rechazando conexión");
            close(new_socket);
            continue;
        }

        cargs->socket_fd = new_socket;
        cargs->addr      = client_addr;

        /* Crear hilo para manejar este cliente */
        pthread_t tid;
        int ret = pthread_create(&tid, NULL, client_thread, (void *)cargs);
        if (ret != 0) {
            log_event(LOG_ERROR, NULL, 0, "pthread_create() falló — rechazando conexión");
            close(new_socket);
            free(cargs);
            continue;
        }

        /*
         * pthread_detach() le dice al SO que cuando el hilo termine,
         * libere automáticamente sus recursos. Sin esto, los recursos
         * del hilo quedarían en el sistema hasta que alguien llame
         * pthread_join() (lo que no haremos porque tenemos muchos hilos).
         */
        pthread_detach(tid);
    }

    /* PASO 7: Cierre limpio */
    log_event(LOG_INFO, NULL, 0, "Servidor detenido.");
    logger_close();

    return 0;
}

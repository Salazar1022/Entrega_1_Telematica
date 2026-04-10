/*
 * net_utils.c — Implementación de Utilidades de Red
 *
 * Explicación de las funciones clave de la API de Berkeley:
 *
 *   socket(AF_INET, SOCK_STREAM, 0)
 *     → Crea un socket TCP/IPv4.
 *       AF_INET    = familia de direcciones IPv4
 *       SOCK_STREAM = tipo orientado a conexión (TCP)
 *       0           = el SO elige automáticamente el protocolo (TCP)
 *
 *   setsockopt(SO_REUSEADDR)
 *     → Permite reusar el puerto inmediatamente después de un reinicio.
 *       Sin esto, si el servidor se cae y se reinicia, el SO mantiene
 *       el puerto en estado TIME_WAIT por ~60 segundos y bind() falla.
 *
 *   bind()
 *     → Asocia el socket a una dirección IP y puerto.
 *       Usamos INADDR_ANY para aceptar conexiones en cualquier interfaz.
 *
 *   listen(backlog=10)
 *     → Pone el socket en modo pasivo. backlog=10 significa que el SO
 *       puede encolar hasta 10 conexiones pendientes mientras el servidor
 *       está ocupado en accept().
 *
 *   getaddrinfo()
 *     → Resuelve un hostname a una dirección IP usando DNS.
 *       Es la función moderna (reemplaza a gethostbyname()).
 *       Soporta IPv4 e IPv6 transparentemente.
 */

#include "../include/net_utils.h"
#include "../include/logger.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>          /* close()                       */
#include <sys/socket.h>      /* socket, bind, listen, accept  */
#include <netinet/in.h>      /* sockaddr_in, INADDR_ANY       */
#include <arpa/inet.h>       /* inet_ntop                     */
#include <netdb.h>           /* getaddrinfo, freeaddrinfo     */
#include <errno.h>           /* errno, strerror               */

/* ── create_server_socket() ─────────────────────────────────────────────── */

int create_server_socket(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;   /* Valor para setsockopt (activar la opción) */

    /*
     * PASO 1: Crear el socket.
     * socket() devuelve un "file descriptor" (entero >= 0).
     * En Linux/Unix, los sockets son tratados como archivos.
     * Si falla, devuelve -1.
     */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_event(LOG_ERROR, NULL, 0, "socket() falló al crear el socket del servidor");
        return -1;
    }

    /*
     * PASO 2: SO_REUSEADDR.
     * Permite reusar el puerto inmediatamente si el servidor se reinicia.
     * Sin esto, tendríamos que esperar ~60 segundos entre reinicios.
     *
     * setsockopt(socket, nivel, opción, valor, tamaño_valor)
     *   SOL_SOCKET = nivel "socket genérico" (no específico de TCP/IP)
     *   SO_REUSEADDR = opción para reusar dirección
     */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_event(LOG_WARN, NULL, 0, "setsockopt(SO_REUSEADDR) falló — continuando de todas formas");
    }

    /*
     * PASO 3: Configurar la dirección del servidor.
     * memset limpia la estructura (evita basura en la memoria).
     *   sin_family = AF_INET  → IPv4
     *   sin_addr   = INADDR_ANY → acepta conexiones en cualquier IP
     *                             que tenga este host (0.0.0.0)
     *   sin_port   = htons(port) → htons convierte el puerto de
     *                              "host byte order" a "network byte order"
     *                              (las redes usan big-endian)
     */
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    /*
     * PASO 4: bind() — enlazar socket a la dirección.
     * Asocia el socket al puerto configurado arriba.
     * El cast (struct sockaddr*) es necesario porque bind() acepta
     * cualquier familia de direcciones, no solo IPv4.
     */
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        char errmsg[128];
        snprintf(errmsg, sizeof(errmsg), "bind() falló en puerto %d: %s",
                 port, strerror(errno));
        log_event(LOG_ERROR, NULL, 0, errmsg);
        close(server_fd);
        return -1;
    }

    /*
     * PASO 5: listen() — poner en modo escucha.
     * LISTEN_BACKLOG es el máximo de conexiones en la cola de espera
     * mientras el servidor está ocupado procesando accept().
     */
    if (listen(server_fd, LISTEN_BACKLOG) < 0) {
        log_event(LOG_ERROR, NULL, 0, "listen() falló");
        close(server_fd);
        return -1;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Socket del servidor listo en puerto %d", port);
    log_event(LOG_INFO, NULL, 0, msg);

    return server_fd;
}

/* ── resolve_and_connect() ──────────────────────────────────────────────── */

int resolve_and_connect(const char *hostname, int port) {
    /*
     * getaddrinfo() hace la resolución DNS.
     * Recibe: hostname, servicio/puerto, hints (preferencias), y
     *         retorna una lista enlazada de resultados (addrinfo).
     *
     * Usamos hints para pedir solo resultados IPv4 + TCP.
     */
    struct addrinfo hints, *result, *rp;
    int sockfd;
    char port_str[8];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;       /* Solo IPv4           */
    hints.ai_socktype = SOCK_STREAM;   /* Solo TCP            */

    snprintf(port_str, sizeof(port_str), "%d", port);

    /*
     * getaddrinfo() puede fallar si:
     *   - El DNS no resuelve el hostname
     *   - No hay red disponible
     *   - El hostname es inválido
     * En ese caso, retornamos -1 SIN terminar el servidor.
     */
    int ret = getaddrinfo(hostname, port_str, &hints, &result);
    if (ret != 0) {
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg),
                 "getaddrinfo() falló para '%s': %s", hostname, gai_strerror(ret));
        log_event(LOG_ERROR, NULL, 0, errmsg);
        return -1;
    }

    /*
     * getaddrinfo() puede devolver múltiples resultados (diferentes IPs).
     * Intentamos conectarnos con cada uno hasta que uno funcione.
     */
    sockfd = -1;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) continue;  /* Este resultado no funcionó, probar siguiente */

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  /* ¡Conexión exitosa! Salimos del bucle */
        }

        close(sockfd);
        sockfd = -1;
    }

    /* Liberar la memoria de la lista de resultados DNS */
    freeaddrinfo(result);

    if (sockfd < 0) {
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg),
                 "No se pudo conectar a '%s:%d'", hostname, port);
        log_event(LOG_ERROR, NULL, 0, errmsg);
        return -1;
    }

    return sockfd;
}

/* ── get_client_info() ───────────────────────────────────────────────────── */

void get_client_info(struct sockaddr_in *addr, char *ip_buf, int buf_size,
                     int *port) {
    /*
     * inet_ntop() convierte la dirección IP binaria (32 bits) a string.
     * Es el opuesto de inet_pton().
     * inet_ntop = "network to presentation"
     * inet_pton = "presentation to network"
     */
    inet_ntop(AF_INET, &(addr->sin_addr), ip_buf, buf_size);

    /*
     * ntohs() = "network to host short"
     * Convierte el puerto de big-endian (red) a little-endian (x86).
     */
    *port = ntohs(addr->sin_port);
}

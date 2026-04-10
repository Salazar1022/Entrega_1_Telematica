/*
 * net_utils.h — Utilidades de Red del Servidor
 *
 * Este módulo encapsula las operaciones de red de bajo nivel:
 *   - Creación y configuración del socket del servidor
 *   - Resolución de nombres de dominio (DNS) — NUNCA IPs hardcodeadas
 *   - Conexión a servicios externos (servicio de identidad)
 *
 * POR QUÉ USAMOS getaddrinfo() EN LUGAR DE inet_pton():
 *   inet_pton() solo convierte una cadena con una IP (ej: "192.168.1.1").
 *   getaddrinfo() resuelve NOMBRES DE DOMINIO (ej: "identity.server.local")
 *   mediante DNS. Esto cumple el requisito del proyecto de no hardcodear IPs.
 *
 * TIPO DE SOCKET ELEGIDO: TCP (SOCK_STREAM)
 *   Justificación: El juego requiere confiabilidad absoluta. No podemos
 *   permitir que se pierda un mensaje de ATTACK o DEFEND. TCP garantiza:
 *   - Entrega ordenada de mensajes
 *   - Retransmisión ante pérdidas
 *   - Control de flujo
 *   UDP sería apropiado solo para métricas o posiciones en tiempo real
 *   donde una pérdida sea aceptable.
 */

#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <netinet/in.h>  /* struct sockaddr_in */

/* Tamaño máximo de los buffers de mensajes */
#define BUFFER_SIZE     2048

/* Capacidad máxima de la cola de conexiones pendientes en listen() */
#define LISTEN_BACKLOG  10

/*
 * create_server_socket()
 *
 * Crea, configura y pone en escucha un socket TCP del servidor.
 *
 * Internamente hace:
 *   1. socket()  → crea el socket
 *   2. setsockopt(SO_REUSEADDR) → permite reusar el puerto tras reinicios
 *   3. bind()    → asocia el socket al puerto indicado en INADDR_ANY
 *   4. listen()  → pone el socket en modo pasivo
 *
 * Parámetros:
 *   port → número de puerto en el que escucha el servidor (ej: 8080)
 *
 * Retorna:
 *   descriptor de archivo del socket servidor (>= 0) si todo salió bien
 *  -1 si ocurrió algún error
 */
int create_server_socket(int port);

/*
 * resolve_and_connect()
 *
 * Resuelve un hostname via DNS y establece una conexión TCP.
 * Esta función es la que usamos para contactar el servicio de identidad.
 *
 * Parámetros:
 *   hostname → nombre de dominio o IP como string (ej: "identity.local")
 *   port     → puerto del servicio destino
 *
 * Retorna:
 *   descriptor del socket conectado (>= 0) si todo salió bien
 *  -1 si la resolución DNS falló o no se pudo conectar
 *     (el servidor NO termina, solo maneja la excepción)
 */
int resolve_and_connect(const char *hostname, int port);

/*
 * get_client_info()
 *
 * Extrae la IP y el puerto de un cliente a partir de su sockaddr_in.
 * Se usa para el logging: registramos siempre IP:puerto del cliente.
 *
 * Parámetros:
 *   addr     → estructura con la dirección del cliente (devuelta por accept)
 *   ip_buf   → buffer donde se escribe la IP como string
 *   buf_size → tamaño del buffer ip_buf
 *   port     → se escribe aquí el puerto del cliente
 */
void get_client_info(struct sockaddr_in *addr, char *ip_buf, int buf_size,
                     int *port);

#endif /* NET_UTILS_H */

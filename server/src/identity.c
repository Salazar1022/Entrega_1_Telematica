/*
 * identity.c — Implementación del Cliente del Servicio de Identidad
 *
 * Cada vez que un jugador quiere autenticarse, abrimos UNA conexión TCP
 * al servicio de identidad, enviamos la consulta, leemos la respuesta
 * y cerramos la conexión. Es un protocolo simple de petición-respuesta.
 *
 * El host y puerto del servicio se leen de variables de entorno:
 *   export IDENTITY_HOST=identity.miservidor.local
 *   export IDENTITY_PORT=9090
 *
 * Si las variables no están definidas, usamos los valores por defecto.
 * Esto cumple el requisito de no tener IPs hardcodeadas en el código.
 */

#include "../include/identity.h"
#include "../include/net_utils.h"
#include "../include/logger.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>     /* getenv */
#include <unistd.h>     /* close, read, write */

/* Tamaño del buffer de comunicación con el servicio de identidad */
#define IDENTITY_BUFSIZE  512

IdentityResult identity_authenticate(const char *username,
                                     const char *password,
                                     PlayerRole *role_out) {
    /*
     * PASO 1: Leer configuración del servicio desde variables de entorno.
     *
     * getenv("IDENTITY_HOST") retorna el valor de la variable de entorno,
     * o NULL si no está definida. En ese caso usamos el valor por defecto.
     *
     * Por qué variables de entorno y no argumentos de consola:
     *   El servidor ya recibe "puerto" y "archivoDeLogs" por consola.
     *   Usar env vars para el servicio de identidad es la práctica
     *   estándar en servicios en nube (12-factor app).
     */
    const char *host = getenv("IDENTITY_HOST");
    if (host == NULL) {
        host = IDENTITY_DEFAULT_HOST;
    }

    const char *port_str = getenv("IDENTITY_PORT");
    int port = IDENTITY_DEFAULT_PORT;
    if (port_str != NULL) {
        port = atoi(port_str);
    }

    /* Log del intento de autenticación (sin mostrar la contraseña) */
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg),
             "Intentando autenticar usuario '%s' en %s:%d", username, host, port);
    log_event(LOG_INFO, NULL, 0, log_msg);

    /*
     * PASO 2: Conectar al servicio de identidad via DNS.
     * resolve_and_connect() hace el getaddrinfo() internamente.
     * Si falla (servicio caído, DNS falla), retorna -1.
     */
    int sock = resolve_and_connect(host, port);
    if (sock < 0) {
        log_event(LOG_ERROR, NULL, 0,
                  "Servicio de identidad no disponible — ERR 503");
        return IDENTITY_SERVICE_DOWN;
    }

    /*
     * PASO 3: Construir y enviar el mensaje de autenticación.
    * Formato per RFC-CGSP-2 §5: "AUTH_CHECK <usuario> <password>\n"
     * El servicio de identidad espera AUTH_CHECK, no AUTH (AUTH es el
     * comando del cliente CGSP hacia el servidor de juego).
     */
    char request[IDENTITY_BUFSIZE];
    int req_len = snprintf(request, sizeof(request),
                           "AUTH_CHECK %s %s\n", username, password);

    if (write(sock, request, req_len) < 0) {
        log_event(LOG_ERROR, NULL, 0, "Error al enviar petición al servicio de identidad");
        close(sock);
        return IDENTITY_SERVICE_DOWN;
    }

    /*
     * PASO 4: Recibir la respuesta del servicio.
     * Leemos hasta encontrar '\n' o llenar el buffer.
     */
    char response[IDENTITY_BUFSIZE];
    memset(response, 0, sizeof(response));
    int bytes_read = read(sock, response, sizeof(response) - 1);
    close(sock);   /* Cerramos la conexión inmediatamente */

    if (bytes_read <= 0) {
        log_event(LOG_ERROR, NULL, 0, "No se recibió respuesta del servicio de identidad");
        return IDENTITY_SERVICE_DOWN;
    }

    /* Eliminar el '\n' del final para facilitar comparaciones */
    response[strcspn(response, "\r\n")] = '\0';

    snprintf(log_msg, sizeof(log_msg),
             "Respuesta del servicio de identidad: '%s'", response);
    log_event(LOG_INFO, NULL, 0, log_msg);

    /*
     * PASO 5: Parsear la respuesta del servicio de identidad.
     *
     * Respuestas esperadas:
     *   "OK ATTACKER"  → usuario válido, es atacante
     *   "OK DEFENDER"  → usuario válido, es defensor
     *   "ERR 401 ..."  → credenciales inválidas
     *
     * strncmp() compara los primeros N caracteres. Lo usamos para
     * verificar el prefijo sin importar el resto del mensaje.
     */
    if (strncmp(response, "OK ATTACKER", 11) == 0) {
        *role_out = ROLE_ATTACKER;
        return IDENTITY_OK;
    }
    else if (strncmp(response, "OK DEFENDER", 11) == 0) {
        *role_out = ROLE_DEFENDER;
        return IDENTITY_OK;
    }
    else if (strncmp(response, "ERR 401", 7) == 0) {
        return IDENTITY_WRONG_CREDS;
    }
    else {
        /* Respuesta inesperada — el servicio respondió algo que no entendemos */
        snprintf(log_msg, sizeof(log_msg),
                 "Respuesta inesperada del servicio de identidad: '%s'", response);
        log_event(LOG_WARN, NULL, 0, log_msg);
        return IDENTITY_PARSE_ERROR;
    }
}

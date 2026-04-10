/*
 * logger.h — Sistema de Logging del Servidor de Juego
 *
 * Este módulo provee funciones para registrar mensajes simultáneamente
 * en la consola (stdout) y en un archivo de logs.
 *
 * FORMATO DE CADA LÍNEA DE LOG:
 *   [YYYY-MM-DD HH:MM:SS] [NIVEL] [IP:PUERTO] MENSAJE
 *
 * Ejemplo:
 *   [2026-04-09 14:32:01] [INFO] [192.168.1.5:52341] AUTH juan secreto
 *   [2026-04-09 14:32:01] [INFO] [192.168.1.5:52341] OK ROLE ATTACKER
 *
 * NIVELES:
 *   LOG_INFO  → eventos normales
 *   LOG_WARN  → situaciones sospechosas (ej: mensaje inválido)
 *   LOG_ERROR → errores graves (ej: fallo al conectar al servicio de identidad)
 *
 * THREAD-SAFETY:
 *   Usa un mutex interno para que múltiples hilos puedan escribir
 *   logs sin que se mezclen las líneas.
 */

#ifndef LOGGER_H
#define LOGGER_H

/* Niveles de log disponibles */
typedef enum {
    LOG_INFO,   /* Información general */
    LOG_WARN,   /* Advertencia         */
    LOG_ERROR   /* Error crítico       */
} LogLevel;

/*
 * logger_init()
 *
 * Inicializa el logger. Debe llamarse UNA SOLA VEZ al inicio del servidor.
 *
 * Parámetros:
 *   log_filepath → ruta del archivo donde se guardarán los logs
 *                  (ej: "server.log")
 *
 * Retorna:
 *   0 si todo salió bien
 *  -1 si no pudo abrir el archivo
 */
int logger_init(const char *log_filepath);

/*
 * log_event()
 *
 * Escribe un evento en consola y en el archivo de logs.
 *
 * Parámetros:
 *   level      → LOG_INFO, LOG_WARN o LOG_ERROR
 *   client_ip  → IP del cliente (ej: "192.168.1.5"), o NULL si es del servidor
 *   client_port→ puerto del cliente (ej: 52341), o 0 si es del servidor
 *   message    → texto del mensaje a registrar
 */
void log_event(LogLevel level, const char *client_ip, int client_port,
               const char *message);

/*
 * logger_close()
 *
 * Cierra el archivo de logs y libera el mutex.
 * Debe llamarse al apagar el servidor.
 */
void logger_close(void);

#endif /* LOGGER_H */

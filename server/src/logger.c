/*
 * logger.c — Implementación del Sistema de Logging
 *
 * Explicación de la concurrencia:
 *   El servidor crea un hilo por cada cliente. Si dos clientes hacen
 *   algo al mismo tiempo, dos hilos llamarán a log_event() en paralelo.
 *   Sin protección, las dos escrituras se mezclarían en el archivo/consola.
 *
 *   Solución: pthread_mutex_t log_mutex.
 *   Antes de escribir, el hilo llama a pthread_mutex_lock(&log_mutex).
 *   Si otro hilo ya tiene el lock, el hilo actual ESPERA hasta que se libere.
 *   Así garantizamos que las líneas de log nunca se entrelacen.
 */

#include "../include/logger.h"

#include <stdio.h>      /* fprintf, fopen, fflush  */
#include <stdlib.h>     /* exit                    */
#include <string.h>     /* strlen                  */
#include <time.h>       /* time, localtime, strftime */
#include <pthread.h>    /* pthread_mutex_*         */

/* Variables globales del módulo (privadas a este .c) */

/* Puntero al archivo de logs. NULL si el logger no fue inicializado. */
static FILE *log_file = NULL;

/*
 * Mutex para acceso exclusivo al archivo y a la consola.
 * PTHREAD_MUTEX_INITIALIZER es la forma estática de inicializar un mutex.
 * Es equivalente a llamar pthread_mutex_init() con atributos por defecto.
 */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Cadenas de texto para cada nivel de log */
static const char *level_strings[] = {
    "INFO ",   /* LOG_INFO  */
    "WARN ",   /* LOG_WARN  */
    "ERROR"    /* LOG_ERROR */
};

/* Implementación pública */

int logger_init(const char *log_filepath) {
    /*
     * Abrimos el archivo en modo "a" (append):
     *   - Si el archivo no existe, lo crea.
     *   - Si ya existe, añade al final (no borra el contenido anterior).
     *   Esto es importante: si el servidor se reinicia, los logs previos
     *   no se pierden.
     */
    log_file = fopen(log_filepath, "a");
    if (log_file == NULL) {
        fprintf(stderr, "[LOGGER] ERROR: No se pudo abrir el archivo de logs: %s\n",
                log_filepath);
        return -1;
    }

    /* Desactivamos el buffering del archivo para que cada línea se
     * escriba inmediatamente (sin esperar a que el buffer se llene).
     * Importante para logs en tiempo real. */
    setvbuf(log_file, NULL, _IOLBF, 0);

    fprintf(stdout, "[LOGGER] Logging iniciado → archivo: %s\n", log_filepath);
    return 0;
}

void log_event(LogLevel level, const char *client_ip, int client_port,
               const char *message) {
    /*
     * 1. Obtenemos la hora actual del sistema con precisión de segundos.
     *    time() - segundos desde epoch (1970-01-01)
     *    localtime() - convierte a hora local (struct tm)
     *    strftime() - formatea la hora como string legible
     */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    /*
     * 2. Componemos la parte de identificación del cliente.
     *    Si client_ip es NULL, es un mensaje propio del servidor.
     */
    char client_info[64];
    if (client_ip != NULL && client_port > 0) {
        snprintf(client_info, sizeof(client_info), "%s:%d", client_ip, client_port);
    } else {
        snprintf(client_info, sizeof(client_info), "server");
    }

    /*
     * 3. Adquirimos el mutex. Si otro hilo está escribiendo, esperamos.
     *    pthread_mutex_lock() es BLOQUEANTE: el hilo se duerme hasta
     *    que el mutex esté disponible. El SO lo despertará cuando toque.
     */
    pthread_mutex_lock(&log_mutex);

    /* 4. Escribimos en consola (stdout) */
    fprintf(stdout, "[%s] [%s] [%s] %s\n",
            timestamp, level_strings[level], client_info, message);

    /* 5. Escribimos en el archivo (si está abierto) */
    if (log_file != NULL) {
        fprintf(log_file, "[%s] [%s] [%s] %s\n",
                timestamp, level_strings[level], client_info, message);
    }

    /*
     * 6. Liberamos el mutex.
     *    CRÍTICO: siempre liberar el mutex después de usarlo.
     *    Si olvidamos esto, el servidor se bloqueará para siempre.
     */
    pthread_mutex_unlock(&log_mutex);
}

void logger_close(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_unlock(&log_mutex);

    /* Destruimos el mutex al cerrar (libera recursos del SO) */
    pthread_mutex_destroy(&log_mutex);
    fprintf(stdout, "[LOGGER] Logger cerrado.\n");
}

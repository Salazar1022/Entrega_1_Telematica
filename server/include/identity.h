/*
 * identity.h — Cliente del Servicio de Identidad
 *
 * El servidor central NO tiene usuarios locales. Cuando un jugador
 * quiere autenticarse, este módulo se conecta a un servicio externo
 * de identidad, le consulta si el usuario/contraseña son válidos,
 * y obtiene el rol asignado (ATTACKER o DEFENDER).
 *
 * PROTOCOLO CON EL SERVICIO DE IDENTIDAD (texto plano, TCP):
 *   El servidor C envía:
 *     AUTH_CHECK <usuario> <password>\n
 *
 *   El servicio responde:
 *     OK ATTACKER\n - usuario válido, rol atacante
 *     OK DEFENDER\n - usuario válido, rol defensor
 *     ERR 401 ...\n - credenciales incorrectas
 *
 * CONFIGURACIÓN (variables de entorno - sin IPs hardcodeadas):
 *   IDENTITY_HOST - hostname del servicio (ej: "identity.local")
 *   IDENTITY_PORT - puerto del servicio   (ej: "9090")
 *
 *   Si las variables no están definidas, se usan los defaults:
 *     host: "localhost"
 *     port: 9090
 *
 * MANEJO DE EXCEPCIONES:
 *   Si el servicio de identidad está caído o la DNS falla,
 *   identity_authenticate() retorna IDENTITY_SERVICE_DOWN.
 *   El servidor NO termina - solo informa al cliente del error
 *   con ERR 503.
 */

#ifndef IDENTITY_H
#define IDENTITY_H

#include "game_logic.h"   /* PlayerRole */

/* HOST Y PUERTO POR DEFECTO */
#define IDENTITY_DEFAULT_HOST  "localhost"
#define IDENTITY_DEFAULT_PORT  9090

/* CÓDIGOS DE RETORNO DE AUTENTICACIÓN */
typedef enum {
    IDENTITY_OK = 0,  /* Autenticación exitosa              */
    IDENTITY_WRONG_CREDS = -1,  /* Usuario o contraseña incorrectos   */
    IDENTITY_SERVICE_DOWN = -2,  /* Servicio de identidad no disponible*/
    IDENTITY_PARSE_ERROR = -3   /* Respuesta del servicio malformada  */
} IdentityResult;

/*
 * identity_authenticate()
 *
 * Intenta autenticar un usuario contra el servicio de identidad.
 *
 * Proceso:
 *   1. Lee IDENTITY_HOST e IDENTITY_PORT de las variables de entorno
 *   2. Resuelve el hostname con getaddrinfo() (DNS)
 *   3. Se conecta y envía "AUTH_CHECK <usuario> <password>\n"
 *   4. Parsea la respuesta y extrae el rol
 *   5. Cierra la conexión
 *
 * Parámetros:
 *   username - nombre de usuario ingresado por el jugador
 *   password - contraseña ingresada
 *   role_out - se escribe aquí el rol si la autenticación fue exitosa
 *               (ROLE_ATTACKER o ROLE_DEFENDER)
 *
 * Retorna:
 *   IDENTITY_OK - autenticado correctamente, role_out está listo
 *   IDENTITY_WRONG_CREDS - credenciales inválidas
 *   IDENTITY_SERVICE_DOWN - no se pudo contactar al servicio
 *   IDENTITY_PARSE_ERROR - respuesta inesperada del servicio
 */
IdentityResult identity_authenticate(const char *username,
                                     const char *password,
                                     PlayerRole *role_out);

#endif /* IDENTITY_H */

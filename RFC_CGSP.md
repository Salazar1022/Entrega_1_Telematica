# RFC-CGSP-1: CyberGame Socket Protocol (CGSP)

**Estado:** Propuesto  
**Versión:** 1.0  
**Fecha:** Abril 2026  
**Autores:** Equipo Entrega_1_Telematica — Internet y Protocolos 2026-1

---

## Abstract

Este documento especifica el **CyberGame Socket Protocol (CGSP)**, un protocolo de la capa de aplicación diseñado para soportar la comunicación en tiempo real entre clientes y el servidor central de un simulador interactivo multijugador de ciberseguridad. El protocolo define el formato de mensajes, el vocabulario de comandos, las reglas de procedimiento y el manejo de errores para una partida en la que jugadores con roles de "Atacante" o "Defensor" interactúan sobre un plano virtual compartido.

---

## 1. Visión General del Protocolo

### 1.1 Propósito

CGSP permite que múltiples jugadores interactúen simultáneamente con un servidor de juego central. A través de este protocolo, los jugadores pueden:

- Autenticarse en el sistema mediante un servicio de identidad externo
- Listar y unirse a salas de juego activas
- Moverse por un plano virtual bidimensional
- Ejecutar acciones específicas de su rol (atacar o defender recursos críticos)
- Recibir notificaciones de eventos del sistema en tiempo real

### 1.2 Modelo de Funcionamiento

CGSP adopta el modelo **cliente-servidor** con comunicación **bidireccional persistente**:

```
┌─────────────┐          TCP persistente         ┌──────────────────┐
│   Cliente   │ ───────────────────────────────► │ Servidor Central │
│  (Jugador)  │ ◄─────────────────────────────── │      (C)         │
└─────────────┘    mensajes texto plano / \n     └──────────────────┘
                                                          │
                                             TCP req/resp │
                                                          ▼
                                                 ┌─────────────────┐
                                                 │ Servicio de     │
                                                 │ Identidad       │
                                                 └─────────────────┘
```

El servidor acepta conexiones TCP entrantes y crea un hilo dedicado por cada cliente. La conexión se mantiene activa durante toda la sesión del jugador. El servidor también actúa como cliente frente al **Servicio de Identidad** para autenticar usuarios.

### 1.3 Capa de la Arquitectura

CGSP opera en la **capa de aplicación** del modelo TCP/IP. Se apoya en:

| Capa | Protocolo |
|------|-----------|
| Aplicación | **CGSP v1.0** (este documento) |
| Transporte | **TCP** (SOCK_STREAM) |
| Red | IPv4 |
| Enlace | Ethernet / Wi-Fi |

**Justificación de TCP sobre UDP:**  
El juego requiere confiabilidad absoluta en la entrega de mensajes. Un mensaje de `ATTACK` o `DEFEND` que se pierda comprometería la consistencia del estado del juego entre servidor y clientes. TCP garantiza entrega ordenada, detección de pérdidas y retransmisión automática.

### 1.4 Resolución de Nombres

CGSP prohíbe el uso de direcciones IP codificadas. Toda referencia a servicios externos se resuelve mediante DNS usando `getaddrinfo()`. Si la resolución falla, el servicio maneja la excepción sin finalizar su ejecución.

---

## 2. Especificación del Servicio

### 2.1 Primitivas del Servicio

#### Estado: CONECTADO

| Primitiva | Dirección | Descripción |
|-----------|-----------|-------------|
| `AUTH` | Cliente → Servidor | Autenticar un usuario |
| `QUIT` | Cliente → Servidor | Cerrar la conexión |

#### Estado: AUTENTICADO

| Primitiva | Dirección | Descripción |
|-----------|-----------|-------------|
| `LIST_ROOMS` | Cliente → Servidor | Listar salas activas |
| `CREATE_ROOM` | Cliente → Servidor | Crear nueva sala |
| `JOIN` | Cliente → Servidor | Unirse a sala existente |
| `STATUS` | Cliente → Servidor | Estado actual del jugador |
| `QUIT` | Cliente → Servidor | Cerrar la conexión |

#### Estado: EN PARTIDA

| Primitiva | Dirección | Rol | Descripción |
|-----------|-----------|-----|-------------|
| `MOVE` | Cliente → Servidor | Ambos | Mover en el plano |
| `SCAN` | Cliente → Servidor | ATTACKER | Detectar recursos cercanos |
| `ATTACK` | Cliente → Servidor | ATTACKER | Atacar un recurso crítico |
| `DEFEND` | Cliente → Servidor | DEFENDER | Mitigar un ataque |
| `START` | Cliente → Servidor | Ambos | Intentar iniciar la partida |
| `STATUS` | Cliente → Servidor | Ambos | Estado del jugador/sala |
| `QUIT` | Cliente → Servidor | Ambos | Abandonar partida |

#### Mensajes Asíncronos del Servidor

| Mensaje | Receptores | Descripción |
|---------|-----------|-------------|
| `EVENT PLAYER_JOINED` | Sala | Nuevo jugador se unió |
| `EVENT PLAYER_LEFT` | Sala | Jugador abandonó |
| `EVENT GAME_STARTED` | Sala | Partida comenzó |
| `EVENT RESOURCE_INFO` | Solo defensores | Coordenadas de recursos |
| `EVENT RESOURCE_FOUND` | Solo atacante | Recurso detectado por SCAN |
| `EVENT ATTACK` | Sala | Notificación de ataque activo |
| `EVENT DEFENDED` | Sala | Recurso fue defendido |
| `EVENT GAME_OVER` | Sala | Fin de partida con ganador |

---

## 3. Formato de Mensajes

### 3.1 Codificación General

- **Codificación:** UTF-8
- **Delimitador de mensaje:** `\n` (LF, 0x0A)
- **Separador de campos:** espacio (0x20)
- **Un mensaje por línea**
- **Longitud máxima:** 2048 bytes
- **Comandos:** insensibles a mayúsculas (el servidor normaliza)

**Estructura general:**
```
COMANDO [PARAM1] [PARAM2] ... [PARAMn]\n
```

### 3.2 Mensajes Cliente → Servidor

#### AUTH
```
AUTH <usuario> <password>\n
```
| Campo | Tipo | Máx | Descripción |
|-------|------|-----|-------------|
| `usuario` | string | 63 chars | Nombre de usuario |
| `password` | string | 63 chars | Contraseña |

#### LIST_ROOMS
```
LIST_ROOMS\n
```
Sin parámetros.

#### CREATE_ROOM
```
CREATE_ROOM\n
```
Sin parámetros.

#### JOIN
```
JOIN <room_id>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `room_id` | entero positivo | ID numérico de la sala |

#### START
```
START\n
```
Requiere ≥1 atacante y ≥1 defensor en la sala.

#### MOVE
```
MOVE <dx> <dy>\n
```
| Campo | Tipo | Rango | Descripción |
|-------|------|-------|-------------|
| `dx` | entero | [-100, 100] | Desplazamiento eje X |
| `dy` | entero | [-100, 100] | Desplazamiento eje Y |

Posición resultante clampea en [0,99]×[0,99].

#### SCAN
```
SCAN\n
```
Solo atacantes. Radio de detección: **5 unidades**.

#### ATTACK
```
ATTACK <resource_id>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `resource_id` | entero (0 ó 1) | ID del recurso a atacar |

#### DEFEND
```
DEFEND <resource_id>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `resource_id` | entero (0 ó 1) | ID del recurso a defender |

#### STATUS
```
STATUS\n
```

#### QUIT
```
QUIT\n
```

---

### 3.3 Mensajes Servidor → Cliente

#### OK
```
OK <mensaje>\n
```

#### ROLE
```
ROLE <ATTACKER|DEFENDER>\n
```
Enviado tras AUTH exitoso.

#### ERR
```
ERR <código> <descripción>\n
```

**Códigos de Error:**

| Código | Descripción |
|--------|-------------|
| `400` | Formato inválido o parámetros faltantes |
| `401` | Credenciales incorrectas / no autenticado |
| `403` | Acción no permitida para este rol o estado |
| `404` | Sala o recurso no encontrado |
| `409` | Conflicto (sala llena, partida iniciada, etc.) |
| `500` | Error interno del servidor |
| `503` | Servicio de identidad no disponible |

#### ROOM_CREATED
```
ROOM_CREATED <room_id>\n
```

#### ROOM_LIST
```
ROOM_LIST <n>\n
ROOM <id> <WAITING|RUNNING|FINISHED> <jugadores>/<max>\n
...
```

#### Eventos
```
EVENT PLAYER_JOINED <usuario> <ATTACKER|DEFENDER>\n
EVENT PLAYER_LEFT <usuario>\n
EVENT GAME_STARTED\n
EVENT RESOURCE_INFO <id> <x> <y>\n        ← solo defensores
EVENT RESOURCE_FOUND <id> <x> <y>\n      ← solo atacante que hizo SCAN
EVENT ATTACK <resource_id> <atacante>\n
EVENT DEFENDED <resource_id> <defensor>\n
EVENT GAME_OVER <ATTACKER|DEFENDER>\n
```

---

## 4. Reglas de Procedimiento

### 4.1 Máquina de Estados del Cliente

```
     [CONECTADO]
         │ AUTH exitoso → OK + ROLE
         ▼
     [AUTENTICADO]
         │ JOIN exitoso → OK
         ▼
     [EN SALA] ── START (con ≥1 ATC + ≥1 DEF)
         │           │ → EVENT GAME_STARTED
         │           │ → EVENT RESOURCE_INFO (solo defensores)
         ▼           ▼
     [EN PARTIDA]
         │
         └── QUIT → cierre limpio
```

### 4.2 Ciclo de Vida de la Sala

```
  WAITING ──────► RUNNING ──────► FINISHED
 (jugadores)   (en partida)    (resultado)
```

### 4.3 Flujo de Ataque y Defensa

```
ATACANTE            SERVIDOR             DEFENSOR(ES)
   │                    │                     │
   ├── ATTACK 0 ───────►│                     │
   │◄─ OK ──────────────│                     │
   │                    ├── EVENT ATTACK 0 ──►│
   │                    │◄─ DEFEND 0 ─────────┤
   │                    ├── EVENT DEFENDED ───►│
   │◄─ EVENT DEFENDED ──│                     │
```

**Timer:** Un recurso sin defender en **30 segundos** → victoria para atacantes.

### 4.4 Flujo de Exploración

```
ATACANTE            SERVIDOR
   ├── MOVE dx dy ────►│  nueva_pos = pos + (dx,dy), clampear a [0,99]
   │◄─ OK Posición ────│
   ├── SCAN ───────────►│  ¿dist(pos, recurso) ≤ 5 ?
   │◄─ EVENT RESOURCE_FOUND (si detecta)
   │◄─ OK: no encontrado (si no detecta)
```

### 4.5 Manejo de Excepciones

| Situación | Respuesta del Servidor |
|-----------|----------------------|
| Línea vacía | Ignorada silenciosamente |
| Comando desconocido | `ERR 400` |
| Parámetros faltantes | `ERR 400 Uso: CMD <params>` |
| Sin autenticar | `ERR 401` |
| Rol incorrecto | `ERR 403` |
| Sala inexistente | `ERR 404` |
| Servicio identidad caído | `ERR 503` — servidor **continúa** |
| Desconexión abrupta | `read() ≤ 0` → cierre limpio del hilo |
| AUTH duplicado | `ERR 403 Ya estas autenticado` |
| JOIN cuando ya está en sala | `ERR 403 Ya estas en una sala` |

---

## 5. Ejemplos de Implementación

### 5.1 Sesión Completa de un Atacante

```
S→C: OK Bienvenido al servidor CyberGame (CGSP v1.0). Usa AUTH <usuario> <password>

C→S: AUTH carlos hack2026
S→C: OK Bienvenido carlos
S→C: ROLE ATTACKER

C→S: LIST_ROOMS
S→C: ROOM_LIST 1
S→C: ROOM 3 WAITING 1/10

C→S: JOIN 3
S→C: OK Te uniste a la sala

C→S: START
S→C: OK Partida iniciada
S→C: EVENT GAME_STARTED

C→S: MOVE 30 35
S→C: OK Posicion: 30 35

C→S: SCAN
S→C: EVENT RESOURCE_FOUND 0 32 37

C→S: ATTACK 0
S→C: OK Ataque lanzado

S→C: EVENT DEFENDED 0 maria   ← defensor respondió a tiempo

C→S: QUIT
S→C: OK Hasta luego
```

### 5.2 Sesión Completa de un Defensor

```
C→S: AUTH maria seg2026
S→C: OK Bienvenido maria
S→C: ROLE DEFENDER

C→S: JOIN 3
S→C: OK Te uniste a la sala

C→S: START
S→C: EVENT GAME_STARTED
S→C: EVENT RESOURCE_INFO 0 32 37
S→C: EVENT RESOURCE_INFO 1 75 82

C→S: MOVE 32 37
S→C: OK Posicion: 32 37

S→C: EVENT ATTACK 0 carlos

C→S: DEFEND 0
S→C: OK Recurso defendido

C→S: QUIT
S→C: OK Hasta luego
```

### 5.3 Diagrama de Secuencia Completo

```
Atacante       Servidor        Defensor
   │               │               │
   ├─ AUTH ────────►│◄──── AUTH ───┤
   │◄─ OK/ROLE ────│──── OK/ROLE ─►│
   │               │               │
   ├─ CREATE_ROOM ─►│               │
   │◄─ ROOM_CREATED │               │
   │               │               │
   ├─ JOIN 5 ──────►│◄──── JOIN 5 ─┤
   │◄─ OK ──────────│───── OK ─────►│
   │               │               │
   ├─ START ────────►│◄──── START ──┤
   │◄─ GAME_STARTED │─ GAME_STARTED►│
   │                │─ RESOURCE_INFO►│
   │               │               │
   ├─ MOVE/SCAN ───►│               │
   │◄─ FOUND ───────│               │
   │               │               │
   ├─ ATTACK 0 ────►│─ EVENT ATTACK►│
   │◄─ OK ──────────│◄── DEFEND 0 ─┤
   │◄─ DEFENDED ────│──── OK ──────►│
```

### 5.4 Prueba con Telnet

```bash
# Terminal 1: Servicio de identidad stub
python3 server/identity_stub.py 9090

# Terminal 2: Servidor de juego
export IDENTITY_HOST=localhost
export IDENTITY_PORT=9090
cd server && ./server 8080 server.log

# Terminal 3: Atacante
telnet localhost 8080

# Terminal 4: Defensor
telnet localhost 8080
```

---

## 6. Parámetros del Servidor

| Parámetro | Fuente | Default | Descripción |
|-----------|--------|---------|-------------|
| Puerto de escucha | Arg CLI: `./server <puerto>` | — | Puerto TCP del servidor |
| Archivo de logs | Arg CLI: `./server <p> <archivo>` | — | Ruta del log |
| Host de identidad | Env: `IDENTITY_HOST` | `localhost` | Hostname del servicio de identidad |
| Puerto de identidad | Env: `IDENTITY_PORT` | `9090` | Puerto del servicio de identidad |

---

## 7. Mapa del Juego

| Parámetro | Valor |
|-----------|-------|
| Dimensiones | 100 × 100 unidades |
| Recursos críticos | 2 (posición aleatoria al crear sala) |
| Radio de detección (SCAN) | 5 unidades |
| Tiempo límite de defensa | 30 segundos |
| Máx. jugadores por sala | 10 |
| Máx. salas simultáneas | 20 |

---

*Fin del documento RFC-CGSP-1*

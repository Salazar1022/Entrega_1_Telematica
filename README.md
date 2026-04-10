# Entrega_1_Telematica — CyberGame (CGSP)

Simulador multijugador de ciberseguridad sobre protocolo TCP propio (**CGSP v2.0**).

## Cierre de Entregable

- Checklist de cumplimiento final: `Documentacion/checklist_entregable_final.md`
- Guía de despliegue en nube (AWS): `Documentacion/despliegue_nube_aws.md`
- Especificación canónica del protocolo: `Documentacion/RFC_v2.tex`
- Nota: archivos `RFC_CGSP.*` y `rfc.tex` se conservan como histórico de versión previa.

---

## Arquitectura del Sistema

```
Navegador Web
    ↕ HTTP (puerto 8080)
Servidor HTTP Python  ──proxy CGSP──►  Servidor Principal C  ──TCP──►  Servicio de Identidad
                                             (puerto 8081)                   (puerto 9090)
                                                   ▲
                              Cliente Atacante (Python/Tkinter)
                              Cliente Defensor (Java/Swing)
```

---

## Inicio Rápido (Windows PowerShell)

### Opción A — Script automático (abre 3 terminales)

```powershell
cd Entrega_1_Telematica
.\start_all.ps1
```

### Opción B — Inicio manual paso a paso

**Terminal 1 — Servicio de Identidad:**
```powershell
cd Protocol_ServiceID
python identity_server.py 9090 identity.log
```

**Terminal 2 — Servidor CGSP (C) via WSL/Linux:**
```bash
cd server
export IDENTITY_HOST=localhost
export IDENTITY_PORT=9090
export CGSP_IDLE_TIMEOUT=900   # opcional: permite 15 min de inactividad
make run          # compila y ejecuta en puerto 8081
```

> Si el binario ya está compilado en Windows (WSL): `.\server 8081 server.log`

**Terminal 3 — Servidor HTTP / Lobby:**
```powershell
cd cliente_web_y_juego\servidor_http
$env:GAME_HOST = "localhost"
$env:GAME_PORT = "8081"
python server_http.py
```

Abre `http://127.0.0.1:8080` en el navegador.

---

## Usuarios de Prueba

| Usuario     | Password   | Rol      |
|-------------|-----------|----------|
| `atacante1` | `pass123` | ATTACKER |
| `hacker`    | `hack2026`| ATTACKER |
| `admin`     | `admin`   | ATTACKER |
| `defensor1` | `pass123` | DEFENDER |
| `seguridad` | `seg2026` | DEFENDER |
| `guardia`   | `guardia` | DEFENDER |

---

## Flujo Completo de una Partida

1. Abrir `http://127.0.0.1:8080` → hacer login con un usuario atacante
2. En el lobby → crear una sala o unirse a una existente
3. Abrir otra sesión del lobby → hacer login con un usuario defensor → unirse a la misma sala
4. Lanzar los clientes desktop (ver abajo)
5. Una vez que ambos jugadores están en la sala, inicia manualmente con `START` desde el cliente atacante (tecla `T`)

---

## Lanzar Clientes Desktop

### Cliente Atacante (Python / Tkinter)

```powershell
$env:CGSP_HOST = "localhost"
$env:CGSP_PORT = "8081"
$env:CGSP_USER = "atacante1"
$env:CGSP_PASS = "pass123"
$env:CGSP_ROOM = "1"
python cliente_web_y_juego\cliente_atacante\cliente_atacante.py
```

```powershell
$env:CGSP_HOST = "localhost"
$env:CGSP_PORT = "8081"
$env:CGSP_USER = "hacker"
$env:CGSP_PASS = "hack2026"
$env:CGSP_ROOM = "1"
python cliente_web_y_juego\cliente_atacante\cliente_atacante.py
```

**Controles:** `W/A/S/D` (mover) · `V` (SCAN) · `X` (ATTACK 0) · `T` (START manual)

### Cliente Defensor (Java / Swing)

```powershell
$env:CGSP_HOST = "localhost"
$env:CGSP_PORT = "8081"
$env:CGSP_USER = "defensor1"
$env:CGSP_PASS = "pass123"
$env:CGSP_ROOM = "1"
cd cliente_web_y_juego\cliente_defensor
javac ClienteDefensor.java
java ClienteDefensor
```

```powershell
$env:CGSP_HOST = "localhost"
$env:CGSP_PORT = "8081"
$env:CGSP_USER = "seguridad"
$env:CGSP_PASS = "seg2026"
$env:CGSP_ROOM = "1"
cd cliente_web_y_juego\cliente_defensor
javac ClienteDefensor.java
java ClienteDefensor
```

**Controles:** `W/A/S/D` (mover) · `F` (DEFEND 0)

> **Nota:** El lobby muestra la configuración exacta de variables de entorno tras unirte a una sala.

---

## Protocolo CGSP — Resumen de Comandos

### Cliente → Servidor
| Comando              | Estado requerido | Descripción |
|---------------------|-----------------|-------------|
| `AUTH user pass`    | Conectado        | Autenticarse |
| `LIST_ROOMS`        | Autenticado      | Listar salas activas |
| `CREATE_ROOM`       | Autenticado      | Crear sala nueva |
| `JOIN <room_id>`    | Autenticado      | Unirse a sala |
| `START`             | En sala          | Intentar iniciar partida |
| `MOVE <dx> <dy>`    | En partida       | Mover jugador |
| `SCAN`              | En partida (ATK) | Detectar recursos cercanos |
| `ATTACK <id>`       | En partida (ATK) | Atacar recurso |
| `DEFEND <id>`       | En partida (DEF) | Defender recurso |
| `STATUS`            | Cualquiera       | Consultar estado |
| `QUIT`              | Cualquiera       | Cerrar sesión |

### Servidor → Cliente (eventos asíncronos)
```
OK <mensaje>
ROLE <ATTACKER|DEFENDER>
ERR <code> <descripcion>
ROOM_CREATED <room_id>
ROOM_LIST <n>
ROOM <id> <WAITING|RUNNING|FINISHED> <atacantes>/<defensores>/<max>
EVENT PLAYER_JOINED <usuario> <rol>
EVENT PLAYER_LEFT <usuario>
EVENT GAME_STARTED
EVENT RESOURCE_INFO <id> <x> <y>       ← solo defensores
EVENT RESOURCE_FOUND <id> <x> <y>      ← atacantes de la sala (via SCAN)
EVENT ATTACK <resource_id> <atacante>
EVENT DEFENDED <resource_id> <defensor>
EVENT ATTACK_TIMEOUT <resource_id>
EVENT GAME_OVER <ATTACKER|DEFENDER>
```

---

## Variables de Entorno

| Variable        | Default     | Descripción |
|----------------|-------------|-------------|
| `IDENTITY_HOST`| `localhost` | Host del servicio de identidad |
| `IDENTITY_PORT`| `9090`      | Puerto del servicio de identidad |
| `IDENTITY_IDLE_TIMEOUT` | `120` | Timeout de inactividad del servicio de identidad (segundos) |
| `IDENTITY_USERS_FILE` | `users.json` | Archivo JSON de usuarios para el servicio de identidad |
| `GAME_HOST`    | `localhost` | Host del servidor CGSP (usado por HTTP server) |
| `GAME_PORT`    | `8081`      | Puerto del servidor CGSP |
| `CGSP_HOST`    | `localhost` | Host del servidor CGSP (usado por clientes) |
| `CGSP_PORT`    | `8081`      | Puerto del servidor CGSP (clientes) |
| `CGSP_USER`    | `atacante1` | Usuario para autenticación en clientes |
| `CGSP_PASS`    | `pass123`   | Contraseña para autenticación en clientes |
| `CGSP_ROOM`    | `1`         | Sala a la que se une el cliente automáticamente |
| `CGSP_IDLE_TIMEOUT` | `600`   | Timeout de inactividad de cliente en el servidor (segundos) |
| `CGSP_GAME_TIMEOUT` | `600`   | Tiempo máximo de partida antes de victoria defensora (segundos) |
| `HTTP_HOST`    | `127.0.0.1` | Interfaz en la que escucha el servidor HTTP |
| `HTTP_PORT`    | `8080`      | Puerto del servidor HTTP |

---

## Estructura del Proyecto

```
Entrega_1_Telematica/
├── start_all.ps1                     ← Script de inicio rápido (Windows)
├── README.md
├── Documentacion/RFC_v2.tex          ← Especificación base del protocolo (v2)
│
├── server/                           ← Servidor principal (C)
│   ├── Makefile
│   ├── identity_stub.py              ← Stub legacy (opcional para pruebas)
│   ├── server.log                    ← Archivo de logs (generado al ejecutar)
│   ├── include/
│   │   ├── game_logic.h
│   │   ├── identity.h
│   │   ├── logger.h
│   │   ├── net_utils.h
│   │   └── protocol.h
│   └── src/
│       ├── server.c                  ← main() — accept loop + hilos
│       ├── protocol.c                ← parser + dispatcher CGSP
│       ├── game_logic.c              ← lógica del juego (salas, recursos)
│       ├── identity.c                ← cliente del servicio de identidad
│       ├── logger.c                  ← logging thread-safe
│       └── net_utils.c               ← herramientas de red y DNS
│
├── Protocol_ServiceID/               ← Servicio de identidad principal (Python)
│   ├── identity_server.py            ← Auth por AUTH_CHECK + roles
│   ├── users.json                    ← Base de usuarios (password_hash SHA-256)
│   ├── generate_users.py             ← Alta de usuarios para users.json
│   └── identity.log                  ← Log del servicio
│
└── cliente_web_y_juego/
    ├── servidor_http/                ← Servidor HTTP + Lobby web
    │   ├── server_http.py            ← HTTP server (hace proxy a CGSP)
    │   └── static/
    │       ├── index.html            ← Página de login
    │       ├── lobby.html            ← Lobby de partidas
    │       └── style.css             ← Estilos compartidos
    │
    ├── cliente_atacante/
    │   └── cliente_atacante.py       ← Cliente Python con Tkinter
    │
    └── cliente_defensor/
        └── ClienteDefensor.java      ← Cliente Java con Swing
```

---

## Compilación del Servidor (Linux/WSL)

```bash
cd server
make          # Solo compilar
make run      # Compilar y ejecutar (puerto 8081, log en server.log)
make clean    # Limpiar archivos compilados
```

El servidor requiere **gcc** y la flag `-pthread` (ya incluida en el Makefile).

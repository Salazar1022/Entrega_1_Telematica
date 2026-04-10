# Entrega_1_Telematica — CyberGame (CGSP)

Simulador multijugador de ciberseguridad sobre protocolo TCP propio (**CGSP v1.0**).

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
cd server
python identity_stub.py 9090
```

**Terminal 2 — Servidor CGSP (C) via WSL/Linux:**
```bash
cd server
export IDENTITY_HOST=localhost
export IDENTITY_PORT=9090
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
5. Una vez que ambos jugadores están en la sala, la partida inicia automáticamente

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

**Controles:** `W/A/S/D` (mover) · `V` (SCAN) · `X` (ATTACK 0)

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
ROOM <id> <WAITING|RUNNING|FINISHED> <players>/<max>
EVENT PLAYER_JOINED <usuario> <rol>
EVENT PLAYER_LEFT <usuario>
EVENT GAME_STARTED
EVENT RESOURCE_INFO <id> <x> <y>       ← solo defensores
EVENT RESOURCE_FOUND <id> <x> <y>      ← solo atacantes (via SCAN)
EVENT ATTACK <resource_id> <atacante>
EVENT DEFENDED <resource_id> <defensor>
EVENT GAME_OVER <ATTACKER|DEFENDER>
```

---

## Variables de Entorno

| Variable        | Default     | Descripción |
|----------------|-------------|-------------|
| `IDENTITY_HOST`| `localhost` | Host del servicio de identidad |
| `IDENTITY_PORT`| `9090`      | Puerto del servicio de identidad |
| `GAME_HOST`    | `localhost` | Host del servidor CGSP (usado por HTTP server) |
| `GAME_PORT`    | `8081`      | Puerto del servidor CGSP |
| `CGSP_HOST`    | `localhost` | Host del servidor CGSP (usado por clientes) |
| `CGSP_PORT`    | `8081`      | Puerto del servidor CGSP (clientes) |
| `CGSP_USER`    | `atacante1` | Usuario para autenticación en clientes |
| `CGSP_PASS`    | `pass123`   | Contraseña para autenticación en clientes |
| `CGSP_ROOM`    | `1`         | Sala a la que se une el cliente automáticamente |
| `HTTP_HOST`    | `127.0.0.1` | Interfaz en la que escucha el servidor HTTP |
| `HTTP_PORT`    | `8080`      | Puerto del servidor HTTP |

---

## Estructura del Proyecto

```
Entrega_1_Telematica/
├── start_all.ps1                     ← Script de inicio rápido (Windows)
├── README.md
├── RFC_CGSP.md / RFC_CGSP.tex        ← Especificación del protocolo
│
├── server/                           ← Servidor principal (C)
│   ├── Makefile
│   ├── identity_stub.py              ← Servicio de identidad (Python stub)
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

# CyberGame — Simulador Multijugador de Ciberseguridad

Simulador multijugador de operaciones en centros de datos, construido sobre el protocolo de aplicación propio **CGSP v2.0** (CyberGame Session Protocol) sobre TCP.

---

## Arquitectura del Sistema

```
Navegador Web
    ↕ HTTP (puerto 8080)
Servidor HTTP Python  ──proxy CGSP──►  Servidor Principal C  ──TCP──►  Servicio de Identidad
                                             (puerto 8081)                   (puerto 9090)
                                                   ▲
                              Cliente Atacante (Python / Tkinter)
                              Cliente Defensor (Java / Swing)
```

| Componente | Lenguaje | Puerto | Responsabilidad |
|---|---|---|---|
| Servidor CGSP | C | 8081 | Lógica del juego, salas, protocolo |
| Servicio de Identidad | Python | 9090 | Autenticación y roles de usuarios |
| Servidor HTTP / Lobby | Python | 8080 | Interfaz web y proxy hacia CGSP |
| Cliente Atacante | Python (Tkinter) | — | Interfaz gráfica del atacante |
| Cliente Defensor | Java (Swing) | — | Interfaz gráfica del defensor |

---

## Requisitos

- **gcc** y **make** — para compilar el servidor C (en Linux, WSL o EC2)
- **Python 3.x** — con `tkinter` incluido (viene en la mayoría de instalaciones)
- **Java JDK 11+** — para compilar y ejecutar el cliente defensor

---

## Servidor en la Nube (AWS EC2)

El servidor ya está desplegado y accesible públicamente:

| Servicio | URL |
|---|---|
| Lobby web | `http://cybergame.neomagno.com:8080` |
| Servidor CGSP | `cybergame.neomagno.com:8081` |

Para correr los clientes apuntando al servidor en la nube, usar `CGSP_HOST=cybergame.neomagno.com` en las variables de entorno (ver sección [Lanzar Clientes Desktop](#lanzar-clientes-desktop)).

---

## Inicio Rápido — Ejecución Local (Windows)

### Opción A — Script automático (recomendada)

Abre PowerShell como administrador en la raíz del proyecto:

```powershell
.\start_all.ps1
```

El script detecta automáticamente si el servidor C ya está compilado como `server.exe`. Si no lo encuentra, intenta compilarlo y ejecutarlo via **WSL**. Abre 3 ventanas separadas de PowerShell y al finalizar abre el navegador en `http://127.0.0.1:8080`.

> **Requisito:** tener WSL instalado si el servidor C no está precompilado como `server.exe`.

### Opción B — Inicio manual paso a paso

**Terminal 1 — Servicio de Identidad:**
```powershell
cd Protocol_ServiceID
python identity_server.py 9090 identity.log
```

**Terminal 2 — Servidor CGSP (C) via WSL:**
```bash
cd server
export IDENTITY_HOST=localhost
export IDENTITY_PORT=9090
make run
```

> Si el binario ya está compilado: `./server 8081 server.log`

**Terminal 3 — Servidor HTTP / Lobby:**
```powershell
cd cliente_web_y_juego\servidor_http
$env:GAME_HOST = "localhost"
$env:GAME_PORT = "8081"
python server_http.py
```

Abrir `http://127.0.0.1:8080` en el navegador.

---

## Compilación del Servidor C (Linux / WSL / EC2)

```bash
cd server
make          # Solo compilar
make run      # Compilar y ejecutar en puerto 8081 con log en server.log
make clean    # Limpiar archivos compilados (.o y ejecutable)
```

Requiere `gcc` con soporte `-pthread` (ya incluido en el Makefile). El ejecutable recibe los parámetros por consola:

```bash
./server <puerto> <archivoDeLogs>
# Ejemplo:
./server 8081 server.log
```

---

## Usuarios de Prueba

| Usuario | Contraseña | Rol |
|---|---|---|
| `atacante1` | `pass123` | ATTACKER |
| `hacker` | `hack2026` | ATTACKER |
| `admin` | `admin` | ATTACKER |
| `defensor1` | `pass123` | DEFENDER |
| `seguridad` | `seg2026` | DEFENDER |
| `guardia` | `guardia` | DEFENDER |

> Las contraseñas se almacenan como hashes SHA-256 en `Protocol_ServiceID/users.json`. Para agregar usuarios nuevos usar `generate_users.py`.

---

## Flujo Completo de una Partida

1. Abrir el lobby → `http://cybergame.neomagno.com:8080` (o `http://127.0.0.1:8080` en local)
2. Hacer login con un usuario **atacante** → crear una sala
3. Abrir otra sesión → hacer login con un usuario **defensor** → unirse a la misma sala
4. Lanzar los clientes desktop (ver sección siguiente)
5. Una vez ambos jugadores estén en la sala, iniciar con `START` desde el cliente atacante (tecla `T`)

> Debe haber **al menos un atacante y un defensor** en la sala para que la partida pueda iniciar.

---

## Lanzar Clientes Desktop

### Cliente Atacante (Python / Tkinter)

**Contra el servidor en la nube:**
```powershell
$env:CGSP_HOST = "cybergame.neomagno.com"
$env:CGSP_PORT = "8081"
$env:CGSP_USER = "atacante1"
$env:CGSP_PASS = "pass123"
$env:CGSP_ROOM = "1"
python cliente_web_y_juego\cliente_atacante\cliente_atacante.py
```

**Contra servidor local:**
```powershell
$env:CGSP_HOST = "localhost"
$env:CGSP_PORT = "8081"
$env:CGSP_USER = "hacker"
$env:CGSP_PASS = "hack2026"
$env:CGSP_ROOM = "1"
python cliente_web_y_juego\cliente_atacante\cliente_atacante.py
```

**Controles:** `W/A/S/D` mover · `V` SCAN · `X` ATTACK · `T` START manual

**Variable adicional:**

| Variable | Valores | Descripción |
|---|---|---|
| `CGSP_AUTO_START` | `1` / `0` | Si es `1`, envía START automáticamente al unirse a la sala |

---

### Cliente Defensor (Java / Swing)

**Contra el servidor en la nube:**
```powershell
$env:CGSP_HOST = "cybergame.neomagno.com"
$env:CGSP_PORT = "8081"
$env:CGSP_USER = "defensor1"
$env:CGSP_PASS = "pass123"
$env:CGSP_ROOM = "1"
cd cliente_web_y_juego\cliente_defensor
javac ClienteDefensor.java
java ClienteDefensor
```

**Contra servidor local:**
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

**Controles:** `W/A/S/D` mover · `F` DEFEND

> El lobby muestra la configuración exacta de variables de entorno tras unirte a una sala.

---

## Protocolo CGSP v2.0 — Referencia de Comandos

### Cliente → Servidor

| Comando | Estado requerido | Descripción |
|---|---|---|
| `AUTH <user> <pass>` | Conectado | Autenticarse |
| `LIST_ROOMS` | Autenticado | Listar salas activas |
| `CREATE_ROOM` | Autenticado | Crear sala nueva |
| `JOIN <room_id>` | Autenticado | Unirse a sala existente |
| `START` | En sala | Intentar iniciar partida |
| `MOVE <dx> <dy>` | En partida | Mover jugador |
| `SCAN` | En partida (ATK) | Detectar recursos cercanos |
| `ATTACK <id>` | En partida (ATK) | Atacar recurso crítico |
| `DEFEND <id>` | En partida (DEF) | Defender recurso bajo ataque |
| `STATUS` | Cualquiera | Consultar estado actual |
| `QUIT` | Cualquiera | Cerrar sesión |

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
EVENT RESOURCE_INFO <id> <x> <y>       ← solo defensores al iniciar partida
EVENT RESOURCE_FOUND <id> <x> <y>      ← atacantes al hacer SCAN exitoso
EVENT ATTACK <resource_id> <atacante>
EVENT DEFENDED <resource_id> <defensor>
EVENT ATTACK_TIMEOUT <resource_id>
EVENT GAME_OVER <ATTACKER|DEFENDER>
```

La especificación completa del protocolo está en `RFC_v2.tex`.

---

## Variables de Entorno

| Variable | Default | Descripción |
|---|---|---|
| `IDENTITY_HOST` | `localhost` | Host del servicio de identidad |
| `IDENTITY_PORT` | `9090` | Puerto del servicio de identidad |
| `IDENTITY_IDLE_TIMEOUT` | `120` | Timeout de inactividad del servicio de identidad (s) |
| `IDENTITY_USERS_FILE` | `users.json` | Archivo JSON de usuarios |
| `GAME_HOST` | `localhost` | Host del servidor CGSP (usado por servidor HTTP) |
| `GAME_PORT` | `8081` | Puerto del servidor CGSP |
| `CGSP_HOST` | `localhost` | Host del servidor CGSP (usado por clientes) |
| `CGSP_PORT` | `8081` | Puerto del servidor CGSP |
| `CGSP_USER` | `atacante1` | Usuario para autenticación automática |
| `CGSP_PASS` | `pass123` | Contraseña para autenticación automática |
| `CGSP_ROOM` | `1` | Sala a la que se une el cliente automáticamente |
| `CGSP_AUTO_START` | `0` | Si es `1`, el cliente atacante envía START al unirse |
| `CGSP_IDLE_TIMEOUT` | `600` | Timeout de inactividad de cliente en el servidor (s) |
| `CGSP_GAME_TIMEOUT` | `600` | Tiempo máximo de partida antes de victoria defensora (s) |
| `HTTP_HOST` | `127.0.0.1` | Interfaz en la que escucha el servidor HTTP |
| `HTTP_PORT` | `8080` | Puerto del servidor HTTP |

---

## Estructura del Proyecto

```
Entrega_1_Telematica/
├── start_all.ps1                          ← Script de inicio rápido (Windows)
├── README.md
├── RFC_v2.tex                             ← Especificación del protocolo CGSP v2.0
│
├── server/                                ← Servidor principal (C)
│   ├── Makefile
│   ├── server.log                         ← Log generado en ejecución
│   ├── include/
│   │   ├── game_logic.h
│   │   ├── identity.h
│   │   ├── logger.h
│   │   ├── net_utils.h
│   │   └── protocol.h
│   └── src/
│       ├── server.c                       ← main() — accept loop + hilos
│       ├── protocol.c                     ← parser y dispatcher CGSP
│       ├── game_logic.c                   ← lógica del juego (salas, recursos)
│       ├── identity.c                     ← cliente del servicio de identidad
│       ├── logger.c                       ← logging thread-safe
│       └── net_utils.c                    ← utilidades de red y DNS
│
├── Protocol_ServiceID/                    ← Servicio de identidad (Python)
│   ├── identity_server.py                 ← Autenticación AUTH_CHECK + roles
│   ├── users.json                         ← Usuarios con hash SHA-256
│   ├── generate_users.py                  ← Alta de nuevos usuarios
│   └── identity.log                       ← Log del servicio
│
└── cliente_web_y_juego/
    ├── servidor_http/                     ← Servidor HTTP + Lobby web
    │   ├── server_http.py
    │   └── static/
    │       ├── index.html                 ← Página de login
    │       ├── lobby.html                 ← Lobby de partidas
    │       └── style.css
    ├── cliente_atacante/
    │   └── cliente_atacante.py            ← Cliente Python con Tkinter
    └── cliente_defensor/
        └── ClienteDefensor.java           ← Cliente Java con Swing
```

---

## Despliegue en AWS

### Infraestructura

- **Proveedor:** AWS Educate (EC2)
- **Instancia:** t3.micro — Ubuntu Server 24.04 LTS
- **DNS:** `cybergame.neomagno.com` → IP pública de la instancia (registro A en Porkbun)
- **Puertos abiertos en Security Group:** 22 (SSH), 8080 (HTTP), 8081 (CGSP)

### Encender el servidor

1. AWS Academy → **Start Lab** → esperar círculo verde → clic en **AWS**
2. EC2 → **Instances** → **Start instance**
3. Copiar la nueva **Public IPv4 address**
4. Si la IP cambió, actualizar el registro A en Porkbun
5. Conectarse por SSH:

```bash
ssh -i ~/cybergame-key.pem ubuntu@<IP-EC2>
```

6. Lanzar todos los servicios:

```bash
~/start.sh
```

El script levanta los 3 servicios automáticamente en sesiones tmux independientes.

### Apagar el servidor

1. EC2 Console → **Stop instance**
2. AWS Academy → **End Lab**

> Si la IP pública cambia al reiniciar, actualizar el registro A en Porkbun con la nueva IP.

### Comandos tmux útiles

| Acción | Comando |
|---|---|
| Ver sesiones activas | `tmux ls` |
| Reconectarse a la sesión | `tmux attach -t cybergame` |
| Cambiar de ventana | `Ctrl+B` luego `N` |
| Nueva ventana | `Ctrl+B` luego `C` |
| Detener proceso | `Ctrl+C` |
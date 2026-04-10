# identity_server.py
import socket, threading, hashlib, json, os, sys, logging

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

# Logging
def setup_logging(log_path):
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(message)s",
        handlers=[
            logging.FileHandler(log_path, encoding="utf-8"),
            logging.StreamHandler()
        ]
    )

# Cargar usuarios
def load_users(path=None):
    users_path = path or os.environ.get("IDENTITY_USERS_FILE", "users.json")
    if not os.path.isabs(users_path):
        users_path = os.path.join(BASE_DIR, users_path)
    with open(users_path, encoding="utf-8") as f:
        return {u["usuario"]: u for u in json.load(f)}

# Hasheo de contraseña
def hash_password(pw):
    return hashlib.sha256(pw.encode()).hexdigest()

# Manejo de cliente 
def handle_client(conn, addr, users):
    with conn:
        timeout = int(os.environ.get("IDENTITY_IDLE_TIMEOUT", 120))
        conn.settimeout(timeout)
        client_id = f"{addr[0]}:{addr[1]}"
        logging.info(f"[{client_id}] Conexion recibida")

        # Recepción con manejo de errores
        try:
            raw = conn.recv(2048)
        except socket.timeout:
            conn.sendall(b"ERR 408 Timeout de inactividad\n")
            logging.info(f"[{client_id}] Timeout de inactividad")
            return
        except Exception as e:
            logging.info(f"[{client_id}] ERROR en recv: {e}")
            conn.sendall(b"ERR 500 Error interno\n")
            return

        # Validar tamaño máximo (>= porque 2048 exactos puede indicar truncamiento)
        if len(raw) >= 2048:
            conn.sendall(b"ERR 400 Mensaje demasiado largo\n")
            logging.info(f"[{client_id}] Mensaje demasiado largo ({len(raw)} bytes)")
            return

        # Decodificación segura
        data = raw.decode(errors="ignore").strip()

        if not data:
            conn.sendall(b"ERR 400 Mensaje vacio\n")
            logging.info(f"[{client_id}] Mensaje vacio")
            return

        logging.info(f"[{client_id}] >> {data}")
        parts = data.split()

        try:
            if len(parts) == 3 and parts[0] == "AUTH_CHECK":
                _, usuario, password = parts

                # Validación de longitud de campos
                if len(usuario) > 63 or len(password) > 63:
                    conn.sendall(b"ERR 400 Parametros invalidos\n")
                    logging.info(f"[{client_id}] Parametros invalidos (longitud)")
                    return

                user = users.get(usuario)
                if user and user["password_hash"] == hash_password(password):
                    response = f"OK {user['rol']}\n"
                    conn.sendall(response.encode())
                    logging.info(f"[{client_id}] << AUTH OK -> {user['rol']}")
                else:
                    conn.sendall(b"ERR 401 Usuario o contrasena invalidos\n")
                    logging.info(f"[{client_id}] << AUTH FAIL -> {usuario}")
            else:
                conn.sendall(b"ERR 400 Formato invalido. Uso: AUTH_CHECK <usuario> <contrasena>\n")
                logging.info(f"[{client_id}] << Formato invalido: {data[:50]}")

        except Exception as e:
            logging.info(f"[{client_id}] ERROR interno: {e}")
            conn.sendall(b"ERR 500 Error interno\n")

# ── Servidor principal ────────────────────────────────────────────────────────
def main():
    # Parámetros por consola 
    # Uso: python3 identity_server.py <puerto> <archivoDeLogs>
    if len(sys.argv) >= 3:
        port     = int(sys.argv[1])
        log_path = sys.argv[2]
    elif len(sys.argv) == 2:
        port     = int(sys.argv[1])
        log_path = os.environ.get("IDENTITY_LOG", "identity.log")
    else:
        port     = int(os.environ.get("IDENTITY_PORT", 9090))
        log_path = os.environ.get("IDENTITY_LOG", "identity.log")

    if not os.path.isabs(log_path):
        log_path = os.path.join(BASE_DIR, log_path)

    setup_logging(log_path)
    logging.info(f"Iniciando Servicio de Identidad | puerto={port} | log={log_path}")

    # Cargar usuarios
    try:
        users = load_users()
        logging.info(f"Usuarios cargados: {list(users.keys())}")
    except FileNotFoundError:
        logging.error("ERROR: No se encontro users.json. Abortando.")
        sys.exit(1)
    except Exception as e:
        logging.error(f"ERROR al cargar usuarios: {e}. Abortando.")
        sys.exit(1)

    # Resolución DNS (sin IPs hardcodeadas)
    host = os.environ.get("IDENTITY_HOST", "localhost")
    try:
        socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_STREAM)
        logging.info(f"Hostname '{host}' resuelto correctamente")
    except socket.gaierror:
        logging.warning(f"WARN: No se pudo resolver '{host}', escuchando en 0.0.0.0")

    # Socket servidor
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("0.0.0.0", port))
        s.listen(10)
        logging.info(f"Servicio de Identidad listo y escuchando en puerto {port}")

        while True:
            conn, addr = s.accept()
            t = threading.Thread(
                target=handle_client,
                args=(conn, addr, users)
            )
            t.daemon = True
            t.start()

if __name__ == "__main__":
    main()

    #carlos: hack2026
    #maria: seg2026
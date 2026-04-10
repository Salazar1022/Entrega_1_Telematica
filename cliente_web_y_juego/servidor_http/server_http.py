import json
import os
import secrets
import socket
import threading
import time
from urllib.parse import parse_qs

HTTP_HOST = os.getenv("HTTP_HOST", "127.0.0.1")
HTTP_PORT = int(os.getenv("HTTP_PORT", "8080"))
GAME_HOST = os.getenv("GAME_HOST", "localhost")
GAME_PORT = int(os.getenv("GAME_PORT", "8081"))
STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")

SESSIONS_LOCK = threading.Lock()
SESSIONS = {}


def send_response(client_socket, code, reason, body=b"", content_type="text/plain; charset=UTF-8"):
    if isinstance(body, str):
        body = body.encode("utf-8")

    headers = (
        f"HTTP/1.1 {code} {reason}\r\n"
        f"Content-Type: {content_type}\r\n"
        f"Content-Length: {len(body)}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("utf-8")
    client_socket.sendall(headers + body)


def send_json(client_socket, code, reason, payload):
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    send_response(client_socket, code, reason, body, "application/json; charset=UTF-8")


def read_http_request(client_socket):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = client_socket.recv(4096)
        if not chunk:
            break
        data += chunk
        if len(data) > 65536:
            raise ValueError("Request headers too large")

    if not data:
        return None

    header_blob, _, body = data.partition(b"\r\n\r\n")
    header_text = header_blob.decode("utf-8", errors="ignore")
    header_lines = header_text.split("\r\n")
    if not header_lines:
        raise ValueError("Malformed request")

    method_line = header_lines[0].split(" ")
    if len(method_line) < 3:
        raise ValueError("Malformed request line")

    method, path, version = method_line[0], method_line[1], method_line[2]

    headers = {}
    for line in header_lines[1:]:
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        headers[key.strip().lower()] = value.strip()

    content_length = int(headers.get("content-length", "0") or "0")
    while len(body) < content_length:
        chunk = client_socket.recv(4096)
        if not chunk:
            break
        body += chunk

    return {
        "method": method,
        "path": path,
        "version": version,
        "headers": headers,
        "body": body[:content_length],
    }


def parse_form_body(body_bytes):
    body_text = body_bytes.decode("utf-8", errors="ignore")
    raw_data = parse_qs(body_text, keep_blank_values=True)
    return {key: values[0] for key, values in raw_data.items()}


def _send_line(sock, line):
    if not line.endswith("\n"):
        line += "\n"
    sock.sendall(line.encode("utf-8"))


def _recv_lines(sock, idle_timeout=0.25, max_total=1.5):
    deadline = time.time() + max_total
    chunks = []
    sock.settimeout(idle_timeout)

    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
        except socket.timeout:
            break

    if not chunks:
        return []

    text = b"".join(chunks).decode("utf-8", errors="ignore")
    return [line.strip() for line in text.splitlines() if line.strip()]


def _parse_err(lines):
    for line in lines:
        if line.startswith("ERR "):
            parts = line.split(" ", 2)
            code = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 500
            message = parts[2] if len(parts) > 2 else "Error de protocolo"
            return code, message
    return None


def _parse_role(lines):
    for line in lines:
        if line.startswith("ROLE "):
            role = line.split(" ", 1)[1].strip().upper()
            return role
    return None


def _role_to_ui(role):
    return "atacante" if role == "ATTACKER" else "defensor"


def cgsp_session_commands(username, password, commands):
    if " " in username or " " in password:
        return {"ok": False, "code": 400, "error": "Usuario/password no deben contener espacios"}

    try:
        game_ip = socket.gethostbyname(GAME_HOST)
    except socket.gaierror:
        return {"ok": False, "code": 503, "error": f"No se pudo resolver {GAME_HOST}"}

    try:
        with socket.create_connection((game_ip, GAME_PORT), timeout=3) as sock:
            _recv_lines(sock, max_total=0.6)

            _send_line(sock, f"AUTH {username} {password}")
            auth_lines = _recv_lines(sock)
            auth_err = _parse_err(auth_lines)
            if auth_err:
                code, msg = auth_err
                return {"ok": False, "code": code, "error": msg}

            role = _parse_role(auth_lines)
            if role not in ("ATTACKER", "DEFENDER"):
                return {"ok": False, "code": 503, "error": "No se recibio ROLE del servidor"}

            results = []
            for command in commands:
                _send_line(sock, command)
                lines = _recv_lines(sock)
                results.append(lines)

            return {"ok": True, "role": role, "results": results}
    except (ConnectionRefusedError, TimeoutError, OSError) as exc:
        return {"ok": False, "code": 503, "error": f"Servidor de juego no disponible: {exc}"}


def _to_http_status(cgsp_code):
    if cgsp_code in (400, 401, 403, 404, 409, 503):
        return cgsp_code
    return 500


def _new_session(username, password, role):
    token = secrets.token_hex(16)
    with SESSIONS_LOCK:
        SESSIONS[token] = {
            "username": username,
            "password": password,
            "role": role,
            "created_at": int(time.time()),
        }
    return token


def _get_session(req, fields=None):
    token = req["headers"].get("x-session-token", "").strip()
    if not token and fields is not None:
        token = fields.get("session_token", "").strip()

    if not token:
        return None

    with SESSIONS_LOCK:
        return SESSIONS.get(token)


def _parse_rooms(lines):
    rooms = []
    for line in lines:
        if not line.startswith("ROOM "):
            continue
        parts = line.split()
        if len(parts) < 4:
            continue

        room_id = parts[1]
        cgsp_state = parts[2].upper()
        players_info = parts[3]

        players_connected = 0
        max_players = 10
        if "/" in players_info:
            p_now, p_max = players_info.split("/", 1)
            if p_now.isdigit():
                players_connected = int(p_now)
            if p_max.isdigit():
                max_players = int(p_max)

        if cgsp_state == "RUNNING":
            status = "in_progress"
        elif cgsp_state == "FINISHED":
            status = "finished"
        else:
            status = "waiting"

        rooms.append(
            {
                "id": room_id,
                "name": f"Sala {room_id}",
                "status": status,
                "max_players": max_players,
                "players_connected": players_connected,
                "players": [],
                "roles": {
                    "atacante": "open",
                    "defensor": "open",
                },
            }
        )
    return rooms


def handle_login(client_socket, req):
    fields = parse_form_body(req["body"])
    username = fields.get("username", "").strip()
    password = fields.get("password", "").strip()
    requested_role = fields.get("role", "").strip().lower()

    if not username or not password:
        send_json(client_socket, 400, "Bad Request", {"ok": False, "error": "Campos incompletos"})
        return

    response = cgsp_session_commands(username, password, commands=[])
    if not response["ok"]:
        code = _to_http_status(response["code"])
        send_json(client_socket, code, "Error", {"ok": False, "error": response["error"]})
        return

    role_ui = _role_to_ui(response["role"])
    if requested_role and requested_role in ("atacante", "defensor") and requested_role != role_ui:
        send_json(
            client_socket,
            403,
            "Forbidden",
            {"ok": False, "error": f"Tu usuario esta registrado como {role_ui}, no como {requested_role}"},
        )
        return

    token = _new_session(username, password, role_ui)
    send_json(
        client_socket,
        200,
        "OK",
        {
            "ok": True,
            "username": username,
            "role": role_ui,
            "session_token": token,
            "message": "Login exitoso",
        },
    )


def handle_get_rooms(client_socket, req):
    session = _get_session(req)
    if session is None:
        send_json(client_socket, 401, "Unauthorized", {"ok": False, "error": "Sesion invalida o expirada"})
        return

    response = cgsp_session_commands(session["username"], session["password"], ["LIST_ROOMS"])
    if not response["ok"]:
        code = _to_http_status(response["code"])
        send_json(client_socket, code, "Error", {"ok": False, "error": response["error"]})
        return

    lines = response["results"][0] if response["results"] else []
    cmd_err = _parse_err(lines)
    if cmd_err:
        code, message = cmd_err
        send_json(client_socket, _to_http_status(code), "Error", {"ok": False, "error": message})
        return

    rooms = _parse_rooms(lines)
    send_json(client_socket, 200, "OK", {"ok": True, "rooms": rooms})


def handle_create_room(client_socket, req):
    fields = parse_form_body(req["body"])
    session = _get_session(req, fields)
    if session is None:
        send_json(client_socket, 401, "Unauthorized", {"ok": False, "error": "Sesion invalida o expirada"})
        return

    response = cgsp_session_commands(session["username"], session["password"], ["CREATE_ROOM"])
    if not response["ok"]:
        code = _to_http_status(response["code"])
        send_json(client_socket, code, "Error", {"ok": False, "error": response["error"]})
        return

    lines = response["results"][0] if response["results"] else []
    cmd_err = _parse_err(lines)
    if cmd_err:
        code, message = cmd_err
        send_json(client_socket, _to_http_status(code), "Error", {"ok": False, "error": message})
        return

    room_id = None
    for line in lines:
        if line.startswith("ROOM_CREATED "):
            room_id = line.split(" ", 1)[1].strip()
            break

    if room_id is None:
        send_json(client_socket, 500, "Internal Server Error", {"ok": False, "error": "Respuesta invalida de CREATE_ROOM"})
        return

    room_payload = {
        "id": room_id,
        "name": f"Sala {room_id}",
        "status": "waiting",
        "max_players": 10,
        "players_connected": 0,
        "players": [],
        "roles": {"atacante": "open", "defensor": "open"},
    }
    send_json(client_socket, 200, "OK", {"ok": True, "room": room_payload})


def handle_join_room(client_socket, req):
    fields = parse_form_body(req["body"])
    session = _get_session(req, fields)
    if session is None:
        send_json(client_socket, 401, "Unauthorized", {"ok": False, "error": "Sesion invalida o expirada"})
        return

    room_id = fields.get("room_id", "").strip()
    if not room_id:
        send_json(client_socket, 400, "Bad Request", {"ok": False, "error": "Debes enviar room_id"})
        return

    commands = [f"JOIN {room_id}", "START", "LIST_ROOMS"]
    response = cgsp_session_commands(session["username"], session["password"], commands)
    if not response["ok"]:
        code = _to_http_status(response["code"])
        send_json(client_socket, code, "Error", {"ok": False, "error": response["error"]})
        return

    join_lines = response["results"][0] if len(response["results"]) > 0 else []
    start_lines = response["results"][1] if len(response["results"]) > 1 else []
    list_lines = response["results"][2] if len(response["results"]) > 2 else []

    join_err = _parse_err(join_lines)
    if join_err:
        code, message = join_err
        send_json(client_socket, _to_http_status(code), "Error", {"ok": False, "error": message})
        return

    started = False
    start_message = "Esperando mas jugadores para iniciar"
    start_err = _parse_err(start_lines)
    if start_err is None:
        for line in start_lines:
            if line.startswith("OK "):
                started = True
                start_message = line[3:]
                break
    else:
        start_message = start_err[1]

    rooms = _parse_rooms(list_lines)
    room_payload = next((room for room in rooms if str(room["id"]) == str(room_id)), None)
    if room_payload is None:
        room_payload = {
            "id": room_id,
            "name": f"Sala {room_id}",
            "status": "waiting",
            "max_players": 10,
            "players_connected": 0,
            "players": [],
            "roles": {"atacante": "open", "defensor": "open"},
        }

    send_json(
        client_socket,
        200,
        "OK",
        {
            "ok": True,
            "room": room_payload,
            "started": started,
            "start_message": start_message,
            "desktop_config": {
                "host": GAME_HOST,
                "port": GAME_PORT,
                "username": session["username"],
                "password": session["password"],
                "room_id": room_id,
                "role": session["role"],
            },
        },
    )


def serve_static(client_socket, path):
    if path == "/":
        path = "/index.html"

    clean_path = os.path.normpath(path.lstrip("/"))
    if clean_path.startswith(".."):
        send_response(client_socket, 404, "Not Found", "<h1>404 Not Found</h1>", "text/html; charset=UTF-8")
        return

    file_path = os.path.join(STATIC_DIR, clean_path)
    if not os.path.exists(file_path) or not os.path.isfile(file_path):
        send_response(client_socket, 404, "Not Found", "<h1>404 Not Found</h1>", "text/html; charset=UTF-8")
        return

    content_type = "text/html; charset=UTF-8"
    if file_path.endswith(".css"):
        content_type = "text/css; charset=UTF-8"
    elif file_path.endswith(".js"):
        content_type = "application/javascript; charset=UTF-8"
    elif file_path.endswith(".json"):
        content_type = "application/json; charset=UTF-8"

    with open(file_path, "rb") as f:
        content = f.read()

    send_response(client_socket, 200, "OK", content, content_type)


def handle_client(client_socket, addr):
    print(f"[+] Nueva conexion HTTP: {addr}")
    try:
        request = read_http_request(client_socket)
        if not request:
            return

        method = request["method"].upper()
        path = request["path"].split("?", 1)[0]
        print(f"{method} {path}")

        if method == "GET":
            if path == "/api/rooms":
                handle_get_rooms(client_socket, request)
            else:
                serve_static(client_socket, path)
        elif method == "POST":
            if path == "/login":
                handle_login(client_socket, request)
            elif path == "/api/create-room":
                handle_create_room(client_socket, request)
            elif path == "/api/join-room":
                handle_join_room(client_socket, request)
            else:
                send_response(client_socket, 404, "Not Found", "<h1>404 Not Found</h1>", "text/html; charset=UTF-8")
        else:
            send_response(client_socket, 400, "Bad Request", "<h1>400 Bad Request</h1>", "text/html; charset=UTF-8")
    except ValueError as e:
        print(f"Error en peticion HTTP: {e}")
        send_response(client_socket, 400, "Bad Request", "<h1>400 Bad Request</h1>", "text/html; charset=UTF-8")
    except Exception as e:
        print(f"Error procesando peticion: {e}")
        send_response(client_socket, 500, "Internal Server Error", "<h1>500 Internal Server Error</h1>", "text/html; charset=UTF-8")
    finally:
        client_socket.close()


def start_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    server_socket.bind((HTTP_HOST, HTTP_PORT))
    server_socket.listen(5)
    print(f"[*] Servidor HTTP iniciado en http://{HTTP_HOST}:{HTTP_PORT}")
    print(f"[*] Proxy CGSP apuntando a {GAME_HOST}:{GAME_PORT}")

    try:
        while True:
            client_socket, addr = server_socket.accept()
            client_thread = threading.Thread(target=handle_client, args=(client_socket, addr), daemon=True)
            client_thread.start()
    except KeyboardInterrupt:
        print("\n[!] Apagando servidor HTTP...")
    finally:
        server_socket.close()


if __name__ == "__main__":
    if not os.path.exists(STATIC_DIR):
        os.makedirs(STATIC_DIR)
    start_server()

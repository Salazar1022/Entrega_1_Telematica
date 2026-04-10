import socket
import os
import threading
import json
from urllib.parse import parse_qs

HOST = '127.0.0.1'
PORT = 8080
STATIC_DIR = os.path.join(os.path.dirname(__file__), 'static')

USERS = {
    'atacante1': 'hack2026',
    'defensor1': 'seg2026',
    'demo': 'demo123',
}

ROOMS_LOCK = threading.Lock()
ROOMS = [
    {
        'id': '001',
        'name': 'Bosque Oscuro',
        'status': 'waiting',
        'max_players': 2,
        'players': [{'username': 'npc_atk', 'role': 'atacante'}],
    },
    {
        'id': '002',
        'name': 'Base Alpha',
        'status': 'in_progress',
        'max_players': 2,
        'players': [
            {'username': 'npc_atk2', 'role': 'atacante'},
            {'username': 'npc_def2', 'role': 'defensor'},
        ],
    },
    {
        'id': '003',
        'name': 'Nodo Central',
        'status': 'waiting',
        'max_players': 2,
        'players': [],
    },
]

"""
Enviar respuestas HTTP simples con cuerpo opcional y tipo de contenido configurable
"""
def send_response(client_socket, code, reason, body=b'', content_type='text/plain; charset=UTF-8'):
    if isinstance(body, str):
        body = body.encode('utf-8')

    headers = (
        f'HTTP/1.1 {code} {reason}\r\n'
        f'Content-Type: {content_type}\r\n'
        f'Content-Length: {len(body)}\r\n'
        'Connection: close\r\n'
        '\r\n'
    ).encode('utf-8')
    client_socket.sendall(headers + body)

""" 
Enviar respuestas JSON con estructura de payload y codificación UTF-8
"""
def send_json(client_socket, code, reason, payload):
    body = json.dumps(payload, ensure_ascii=False).encode('utf-8')
    send_response(client_socket, code, reason, body, 'application/json; charset=UTF-8')

"""
Leer y parsear una petición HTTP completa, incluyendo headers y body, con manejo de errores básicos y 
límites de tamaño para evitar abusos
"""
def read_http_request(client_socket):
    data = b''
    while b'\r\n\r\n' not in data:
        chunk = client_socket.recv(4096)
        if not chunk:
            break
        data += chunk
        if len(data) > 65536:
            raise ValueError('Request headers too large')

    if not data:
        return None

    header_blob, _, body = data.partition(b'\r\n\r\n')
    header_text = header_blob.decode('utf-8', errors='ignore')
    header_lines = header_text.split('\r\n')
    if not header_lines:
        raise ValueError('Malformed request')

    method_line = header_lines[0].split(' ')
    if len(method_line) < 3:
        raise ValueError('Malformed request line')

    method, path, version = method_line[0], method_line[1], method_line[2]

    headers = {}
    for line in header_lines[1:]:
        if ':' not in line:
            continue
        key, value = line.split(':', 1)
        headers[key.strip().lower()] = value.strip()

    content_length = int(headers.get('content-length', '0') or '0')
    while len(body) < content_length:
        chunk = client_socket.recv(4096)
        if not chunk:
            break
        body += chunk

    return {
        'method': method,
        'path': path,
        'version': version,
        'headers': headers,
        'body': body[:content_length],
    }

"""
Funcion para parsear el cuerpo de una petición POST con formato application/x-www-form-urlencoded, 
decodificando UTF-8 y manejando valores vacíos
"""
def parse_form_body(body_bytes):
    body_text = body_bytes.decode('utf-8', errors='ignore')
    raw_data = parse_qs(body_text, keep_blank_values=True)
    return {key: values[0] for key, values in raw_data.items()}

"""
Convertir la estructura interna de una sala a un diccionario con información relevante para el cliente, 
incluyendo estado de roles y jugadores conectados
"""
def room_to_dict(room):
    roles_taken = {player['role'] for player in room['players']}
    return {
        'id': room['id'],
        'name': room['name'],
        'status': room['status'],
        'max_players': room['max_players'],
        'players_connected': len(room['players']),
        'players': room['players'],
        'roles': {
            'atacante': 'taken' if 'atacante' in roles_taken else 'open',
            'defensor': 'taken' if 'defensor' in roles_taken else 'open',
        },
    }

"""
Generar un nuevo ID de sala secuencial basado en las salas existentes, con formato de 3 dígitos y manejo de caso sin salas
"""
def next_room_id():
    if not ROOMS:
        return '001'
    max_id = max(int(room['id']) for room in ROOMS)
    return str(max_id + 1).zfill(3)

"""
Manejadores de rutas específicas para la API, incluyendo validación de datos, manejo de errores y 
respuestas JSON estructuradas para cada acción (login, obtener salas, crear sala, unirse a sala)
"""
def handle_login(client_socket, req):
    fields = parse_form_body(req['body'])
    username = fields.get('username', '').strip()
    password = fields.get('password', '').strip()
    role = fields.get('role', '').strip().lower()

    if not username or not password or not role:
        send_json(client_socket, 400, 'Bad Request', {'ok': False, 'error': 'Campos incompletos'})
        return

    if role not in ('atacante', 'defensor'):
        send_json(client_socket, 400, 'Bad Request', {'ok': False, 'error': 'Rol invalido'})
        return

    if USERS.get(username) != password:
        send_json(client_socket, 401, 'Unauthorized', {'ok': False, 'error': 'Credenciales invalidas'})
        return

    send_json(client_socket, 200, 'OK', {
        'ok': True,
        'username': username,
        'role': role,
        'message': 'Login exitoso',
    })

"""
Manejador para obtener la lista de salas disponibles, con información detallada sobre cada sala y su estado actual, 
retornando un JSON con la estructura adecuada para el cliente
"""
def handle_get_rooms(client_socket):
    with ROOMS_LOCK:
        payload = {'rooms': [room_to_dict(room) for room in ROOMS]}
    send_json(client_socket, 200, 'OK', payload)

"""
Manejador para crear una nueva sala, validando el nombre proporcionado o asignando uno por defecto, 
generando un ID único y retornando la información de la sala creada en formato JSON
"""
def handle_create_room(client_socket, req):
    fields = parse_form_body(req['body'])
    room_name = fields.get('name', '').strip()

    with ROOMS_LOCK:
        room_id = next_room_id()
        if not room_name:
            room_name = f'Sala {room_id}'

        room = {
            'id': room_id,
            'name': room_name,
            'status': 'waiting',
            'max_players': 2,
            'players': [],
        }
        ROOMS.append(room)

    send_json(client_socket, 200, 'OK', {'ok': True, 'room': room_to_dict(room)})

"""
Manejador para unirse a una sala existente, validando el ID de sala, el nombre de usuario, el rol solicitado y 
el estado de la sala, retornando errores específicos para cada caso o la información actualizada de la sala en formato JSON
"""
def handle_join_room(client_socket, req):
    fields = parse_form_body(req['body'])
    room_id = fields.get('room_id', '').strip()
    username = fields.get('username', '').strip()
    role = fields.get('role', '').strip().lower()

    if not room_id or not username or role not in ('atacante', 'defensor'):
        send_json(client_socket, 400, 'Bad Request', {'ok': False, 'error': 'Datos invalidos para unirse'})
        return

    with ROOMS_LOCK:
        room = next((r for r in ROOMS if r['id'] == room_id), None)
        if room is None:
            send_json(client_socket, 404, 'Not Found', {'ok': False, 'error': 'Sala no encontrada'})
            return

        role_taken = any(player['role'] == role and player['username'] != username for player in room['players'])
        if role_taken:
            send_json(client_socket, 409, 'Conflict', {'ok': False, 'error': f'Rol {role} ya ocupado en la sala'})
            return

        if room['status'] == 'in_progress':
            send_json(client_socket, 409, 'Conflict', {'ok': False, 'error': 'La partida ya inicio'})
            return

        player = next((p for p in room['players'] if p['username'] == username), None)
        if player is None:
            if len(room['players']) >= room['max_players']:
                send_json(client_socket, 409, 'Conflict', {'ok': False, 'error': 'La sala esta llena'})
                return
            room['players'].append({'username': username, 'role': role})
        else:
            player['role'] = role

        if len(room['players']) >= room['max_players']:
            room['status'] = 'in_progress'

        payload_room = room_to_dict(room)

    send_json(client_socket, 200, 'OK', {'ok': True, 'room': payload_room})

"""
Función para servir archivos estáticos desde el directorio definido, con manejo de rutas seguras y 
tipos de contenido adecuados según la extensión del archivo
"""
def serve_static(client_socket, path):
    if path == '/':
        path = '/index.html'

    clean_path = os.path.normpath(path.lstrip('/'))
    if clean_path.startswith('..'):
        send_response(client_socket, 404, 'Not Found', '<h1>404 Not Found</h1>', 'text/html; charset=UTF-8')
        return

    file_path = os.path.join(STATIC_DIR, clean_path)
    if not os.path.exists(file_path) or not os.path.isfile(file_path):
        send_response(client_socket, 404, 'Not Found', '<h1>404 Not Found</h1>', 'text/html; charset=UTF-8')
        return

    content_type = 'text/html; charset=UTF-8'
    if file_path.endswith('.css'):
        content_type = 'text/css; charset=UTF-8'
    elif file_path.endswith('.js'):
        content_type = 'application/javascript; charset=UTF-8'
    elif file_path.endswith('.json'):
        content_type = 'application/json; charset=UTF-8'

    with open(file_path, 'rb') as f:
        content = f.read()

    send_response(client_socket, 200, 'OK', content, content_type)

"""
Manejador principal para cada conexión entrante, leyendo la petición HTTP, determinando la ruta y método, y delegando 
a los manejadores específicos o sirviendo archivos estáticos según corresponda, con manejo de errores y cierre adecuado del socket
"""
def handle_client(client_socket, addr):
    print(f"[+] Nueva conexion HTTP: {addr}")
    try:
        request = read_http_request(client_socket)
        if not request:
            return

        method = request['method'].upper()
        path = request['path'].split('?', 1)[0]
        print(f"{method} {path}")

        if method == 'GET':
            if path == '/api/rooms':
                handle_get_rooms(client_socket)
            else:
                serve_static(client_socket, path)
        elif method == 'POST':
            if path == '/login':
                handle_login(client_socket, request)
            elif path == '/api/create-room':
                handle_create_room(client_socket, request)
            elif path == '/api/join-room':
                handle_join_room(client_socket, request)
            else:
                send_response(client_socket, 404, 'Not Found', '<h1>404 Not Found</h1>', 'text/html; charset=UTF-8')
        else:
            send_response(client_socket, 400, 'Bad Request', '<h1>400 Bad Request</h1>', 'text/html; charset=UTF-8')
    except ValueError as e:
        print(f"Error en peticion HTTP: {e}")
        send_response(client_socket, 400, 'Bad Request', '<h1>400 Bad Request</h1>', 'text/html; charset=UTF-8')
    except Exception as e:
        print(f"Error procesando peticion: {e}")
        send_response(client_socket, 500, 'Internal Server Error', '<h1>500 Internal Server Error</h1>', 'text/html; charset=UTF-8')
    finally:
        client_socket.close()

"""
Función principal para iniciar el servidor HTTP, creando el socket, vinculándolo a la dirección y puerto definidos, 
y aceptando conexiones entrantes en un bucle infinito, delegando cada conexión a un hilo separado para manejo concurrente, 
con manejo de interrupción por teclado para cierre ordenado del servidor
"""
def start_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    server_socket.bind((HOST, PORT))
    server_socket.listen(5)
    print(f"[*] Servidor HTTP Iniciado en http://{HOST}:{PORT}")
    
    try:
        while True:
            client_socket, addr = server_socket.accept()
            client_thread = threading.Thread(target=handle_client, args=(client_socket, addr))
            client_thread.daemon = True
            client_thread.start()
    except KeyboardInterrupt:
        print("\n[!] Shutting down server...")
    finally:
        server_socket.close()

if __name__ == "__main__":
    if not os.path.exists(STATIC_DIR):
        os.makedirs(STATIC_DIR)
    start_server()

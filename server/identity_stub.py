#!/usr/bin/env python3
"""
identity_stub.py — Servicio de Identidad STUB para pruebas

Este es un servicio de identidad TEMPORAL para que puedas probar el
servidor mientras el servicio real no está disponible.

Usuarios de prueba:
  atacante1 / pass123  → ROL: ATTACKER
  defensor1 / pass123  → ROL: DEFENDER
  admin     / admin    → ROL: ATTACKER
  guardia   / guardia  → ROL: DEFENDER

USO:
  python3 identity_stub.py [puerto]
  python3 identity_stub.py 9090   (por defecto)

El servidor C lo contacta usando las variables de entorno:
  export IDENTITY_HOST=localhost
  export IDENTITY_PORT=9090
"""

import socket
import threading
import sys

# Base de datos simulada de usuarios
USERS = {
    "atacante1": ("pass123",  "ATTACKER"),
    "defensor1": ("pass123",  "DEFENDER"),
    "admin":     ("admin",    "ATTACKER"),
    "guardia":   ("guardia",  "DEFENDER"),
    "hacker":    ("hack2026", "ATTACKER"),
    "seguridad": ("seg2026",  "DEFENDER"),
}

def handle_client(conn, addr):
    print(f"[IDENTITY] Conexión de {addr[0]}:{addr[1]}")
    try:
        data = conn.recv(512).decode("utf-8").strip()
        print(f"[IDENTITY] Recibido: '{data}'")

        # Parsear: AUTH <usuario> <password>
        parts = data.split(" ")
        if len(parts) == 3 and parts[0] == "AUTH":
            username = parts[1]
            password = parts[2]

            if username in USERS:
                stored_pass, role = USERS[username]
                if password == stored_pass:
                    response = f"OK {role}\n"
                    print(f"[IDENTITY] AUTH OK: {username} → {role}")
                else:
                    response = "ERR 401 Contraseña incorrecta\n"
                    print(f"[IDENTITY] AUTH FAIL: {username} (contraseña incorrecta)")
            else:
                response = "ERR 401 Usuario no encontrado\n"
                print(f"[IDENTITY] AUTH FAIL: {username} (usuario no existe)")
        else:
            response = "ERR 400 Formato invalido. Usa: AUTH <usuario> <password>\n"
            print(f"[IDENTITY] Formato inválido: '{data}'")

        conn.send(response.encode("utf-8"))
    except Exception as e:
        print(f"[IDENTITY] Error: {e}")
    finally:
        conn.close()

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9090

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", port))
    server.listen(10)

    print(f"[IDENTITY] Servicio de identidad STUB escuchando en puerto {port}")
    print(f"[IDENTITY] Usuarios disponibles: {list(USERS.keys())}")
    print(f"[IDENTITY] Presiona Ctrl+C para detener\n")

    try:
        while True:
            conn, addr = server.accept()
            t = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
            t.start()
    except KeyboardInterrupt:
        print("\n[IDENTITY] Servicio detenido.")
    finally:
        server.close()

if __name__ == "__main__":
    main()

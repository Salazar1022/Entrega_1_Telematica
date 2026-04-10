import socket
import threading
import tkinter as tk
import os
from tkinter import scrolledtext

# ──────────────────────────────────────────────────────────────────────────────
# Configuración del servidor de juego (via variables de entorno o defaults)
#
# Usuarios de prueba (deben coincidir con identity_stub.py):
#   atacante1 / pass123  → ATTACKER
#   hacker    / hack2026 → ATTACKER
#   admin     / admin    → ATTACKER
#
# Uso:
#   $env:CGSP_USER="atacante1"; $env:CGSP_PASS="pass123"; python cliente_atacante.py
# ──────────────────────────────────────────────────────────────────────────────
HOST      = os.getenv('CGSP_HOST', 'localhost')
PORT      = int(os.getenv('CGSP_PORT', '8081'))
CGSP_USER = os.getenv('CGSP_USER', 'atacante1')
CGSP_PASS = os.getenv('CGSP_PASS', 'pass123')
CGSP_ROOM = os.getenv('CGSP_ROOM', '1')

MAP_WIDTH = 100
MAP_HEIGHT = 100
CANVAS_SIZE = 500

class AtacanteCLI:
    def __init__(self, root):
        self.root = root
        self.root.title("CyberDef - Cliente Atacante")
        self.root.geometry("700x760")
        self.root.resizable(False, False)
        
        # Coordenadas lógicas del servidor (0..99)
        self.x = 0
        self.y = 0
        self.step = 10
        self.player_size = 15
        self.resource_size = 8
        self.resources = {}
        self.resource_canvas_items = {}
        self.selected_resource_id = 0

        # Socket setup
        self.client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.connected = False
        self.estado = "CONECTADO"
        self.buffer = ""
        
        self.setup_ui()
        self.conectar_servidor()

    def setup_ui(self):
        self.status_var = tk.StringVar(value="Desconectado")
        self.status_label = tk.Label(self.root, textvariable=self.status_var, font=("Arial", 12, "bold"), fg="red")
        self.status_label.pack(pady=10)

        self.canvas = tk.Canvas(self.root, width=CANVAS_SIZE, height=CANVAS_SIZE, bg="#1e1e1e", highlightbackground="red", highlightthickness=2)
        self.canvas.pack()

        self.canvas.create_rectangle(0, 0, CANVAS_SIZE - 1, CANVAS_SIZE - 1, outline="#7f1d1d")

        px, py = self._world_to_canvas(self.x, self.y)

        self.player_id = self.canvas.create_oval(
            px - self.player_size, py - self.player_size,
            px + self.player_size, py + self.player_size,
            fill="red", outline="white"
        )

        tk.Label(
            self.root,
            text="Controles: W,A,S,D (Mover), V (SCAN), X (ATTACK 0)",
            font=("Arial", 10)
        ).pack(pady=8)

        controls = tk.Frame(self.root)
        controls.pack(pady=6)

        tk.Button(controls, text="W", width=6, command=lambda: self.move_player(0, -self.step)).grid(row=0, column=1, padx=4, pady=2)
        tk.Button(controls, text="A", width=6, command=lambda: self.move_player(-self.step, 0)).grid(row=1, column=0, padx=4, pady=2)
        tk.Button(controls, text="S", width=6, command=lambda: self.move_player(0, self.step)).grid(row=1, column=1, padx=4, pady=2)
        tk.Button(controls, text="D", width=6, command=lambda: self.move_player(self.step, 0)).grid(row=1, column=2, padx=4, pady=2)
        tk.Button(controls, text="SCAN", width=10, command=self.action_scan).grid(row=0, column=3, padx=10, pady=2)
        tk.Button(controls, text="ATTACK 0", width=10, command=self.action_attack).grid(row=1, column=3, padx=10, pady=2)

        tk.Label(self.root, text="Log de eventos:", font=("Arial", 9, "bold")).pack(pady=(8, 2))
        self.log_box = scrolledtext.ScrolledText(self.root, width=80, height=8, state="disabled", font=("Consolas", 9))
        self.log_box.pack(padx=10, pady=(0, 10))

        self.root.bind("<KeyPress>", self.handle_keypress)
        self.root.protocol("WM_DELETE_WINDOW", self.cerrar_conexion)

    def conectar_servidor(self):
        try:
            ip_servidor = socket.gethostbyname(HOST)
            self.client_socket.connect((ip_servidor, PORT))
            self.connected = True

            self.status_var.set(f"CGSP Socket IP: {ip_servidor}:{PORT} | usuario={CGSP_USER} sala={CGSP_ROOM}")
            self.status_label.config(fg="green")

            t1 = threading.Thread(target=self.recibir_eventos, daemon=True)
            t1.start()

            self.log("Conectado al servidor. Iniciando AUTH...")
            self.enviar_comando(f"AUTH {CGSP_USER} {CGSP_PASS}")
        except socket.gaierror:
            self.status_var.set(f"Fallo resolución DNS de {HOST}")
            self.log(f"No se pudo resolver DNS para {HOST}")
        except ConnectionRefusedError:
            self.status_var.set(f"Servidor CGSP caído en {HOST}:{PORT}")
            self.log("Conexion rechazada: el servidor de juego no esta activo")
        except Exception as e:
            self.status_var.set("Error de red al conectar")
            self.log(f"Error de conexion: {e}")

    def handle_keypress(self, event):
        if not self.connected:
            return
        char = event.char.lower()
        if char == 'w':
            self.move_player(0, -self.step)
        elif char == 's':
            self.move_player(0, self.step)
        elif char == 'a':
            self.move_player(-self.step, 0)
        elif char == 'd':
            self.move_player(self.step, 0)
        elif char == 'v':
            self.action_scan()
        elif char == 'x':
            self.action_attack()

    def move_player(self, dx, dy):
        if not self.connected:
            return

        new_x = max(0, min(MAP_WIDTH - 1, self.x + dx))
        new_y = max(0, min(MAP_HEIGHT - 1, self.y + dy))
        if new_x == self.x and new_y == self.y:
            return

        self.x = new_x
        self.y = new_y
        self.actualizar_render()
        self.enviar_comando(f"MOVE {dx} {dy}")

    def action_scan(self):
        if self.connected:
            self.enviar_comando("SCAN")

    def action_attack(self):
        if self.connected:
            self.enviar_comando(f"ATTACK {self.selected_resource_id}")

    def _world_to_canvas(self, x, y):
        scale_x = (CANVAS_SIZE - 1) / (MAP_WIDTH - 1)
        scale_y = (CANVAS_SIZE - 1) / (MAP_HEIGHT - 1)
        return int(round(x * scale_x)), int(round(y * scale_y))

    def actualizar_render(self):
        px, py = self._world_to_canvas(self.x, self.y)
        self.canvas.coords(
            self.player_id,
            px - self.player_size, py - self.player_size,
            px + self.player_size, py + self.player_size
        )

    def _parse_int(self, value, default=None):
        try:
            return int(value)
        except (TypeError, ValueError):
            return default

    def _fallback_resource_xy(self, resource_id):
        # Si el evento no trae coordenadas, distribuimos puntos en el mapa lógico 0..99.
        base = abs(hash(resource_id))
        x = 5 + (base % (MAP_WIDTH - 10))
        y = 5 + ((base // 101) % (MAP_HEIGHT - 10))
        return x, y

    def _upsert_resource(self, resource_id, x=None, y=None, attacked=None):
        resource = self.resources.get(resource_id, {})

        if x is not None and y is not None:
            resource['x'] = max(0, min(MAP_WIDTH - 1, x))
            resource['y'] = max(0, min(MAP_HEIGHT - 1, y))
        elif 'x' not in resource or 'y' not in resource:
            fx, fy = self._fallback_resource_xy(resource_id)
            resource['x'] = fx
            resource['y'] = fy

        if attacked is not None:
            resource['attacked'] = attacked
        else:
            resource.setdefault('attacked', False)

        self.resources[resource_id] = resource
        self._draw_resource(resource_id)

    def _draw_resource(self, resource_id):
        resource = self.resources.get(resource_id)
        if not resource:
            return

        x, y = self._world_to_canvas(resource['x'], resource['y'])
        attacked = resource.get('attacked', False)
        fill = '#ef4444' if attacked else '#facc15'
        outline = '#7f1d1d' if attacked else '#a16207'

        shape_id, label_id = self.resource_canvas_items.get(resource_id, (None, None))
        if shape_id is None or label_id is None:
            shape_id = self.canvas.create_rectangle(
                x - self.resource_size,
                y - self.resource_size,
                x + self.resource_size,
                y + self.resource_size,
                fill=fill,
                outline=outline,
                width=2,
            )
            label_id = self.canvas.create_text(
                x,
                y - (self.resource_size + 10),
                text=f"R{resource_id}",
                fill='white',
                font=('Consolas', 8, 'bold'),
            )
            self.resource_canvas_items[resource_id] = (shape_id, label_id)
        else:
            self.canvas.coords(
                shape_id,
                x - self.resource_size,
                y - self.resource_size,
                x + self.resource_size,
                y + self.resource_size,
            )
            self.canvas.coords(label_id, x, y - (self.resource_size + 10))
            self.canvas.itemconfig(shape_id, fill=fill, outline=outline)
            self.canvas.itemconfig(label_id, text=f"R{resource_id}")

    def enviar_comando(self, command):
        if not command.endswith("\n"):
            command += "\n"

        try:
            self.client_socket.sendall(command.encode('utf-8'))
            self.log(f"C->S: {command.strip()}")
        except Exception as e:
            self.log(f"Error enviando datos: {e}")

    def recibir_eventos(self):
        while self.connected:
            try:
                data = self.client_socket.recv(1024)
                if not data:
                    self.log("Servidor cerro la conexion")
                    break

                self.buffer += data.decode('utf-8', errors='ignore')
                while '\n' in self.buffer:
                    linea, self.buffer = self.buffer.split('\n', 1)
                    if not linea.strip():
                        continue
                    self.procesar_comando_cgsp(linea)
            except Exception as e:
                self.log(f"Socket cerrado o error en recepcion: {e}")
                break

        self.connected = False
        self.root.after(0, lambda: self.status_var.set("Host Desconectado"))
        self.root.after(0, lambda: self.status_label.config(fg="red"))

    def procesar_comando_cgsp(self, comando: str):
        self.log(f"S->C: {comando}")
        partes = comando.split()
        if not partes:
            return

        c = partes[0].upper()

        if c == 'OK':
            if "Bienvenido" in comando:
                self.estado = "AUTENTICADO"
                self.log("Autenticado correctamente")
            elif len(partes) >= 4 and partes[1].startswith('Posicion'):
                px = self._parse_int(partes[2], self.x)
                py = self._parse_int(partes[3], self.y)
                self.x = max(0, min(MAP_WIDTH - 1, px))
                self.y = max(0, min(MAP_HEIGHT - 1, py))
                self.root.after(0, self.actualizar_render)
        elif c == 'ROLE':
            self.estado = "AUTENTICADO"
            role = partes[1] if len(partes) > 1 else "DESCONOCIDO"
            self.log(f"Rol asignado: {role}")
            threading.Timer(0.5, lambda: self.enviar_comando(f"JOIN {CGSP_ROOM}")).start()
        elif c in ('ROOM_CREATED', 'ROOM_LIST', 'ROOM'):
            return
        elif c == 'EVENT':
            if len(partes) < 2:
                return
            event_type = partes[1]
            if event_type == 'GAME_STARTED':
                self.estado = "EN_PARTIDA"
                self.root.after(0, lambda: self.status_var.set("PARTIDA EN MARCHA (ATACANTE)"))
            elif event_type == 'RESOURCE_FOUND':
                resource_id = partes[2] if len(partes) > 2 else "?"
                x = self._parse_int(partes[3] if len(partes) > 3 else None)
                y = self._parse_int(partes[4] if len(partes) > 4 else None)
                parsed_id = self._parse_int(resource_id, self.selected_resource_id)
                self.selected_resource_id = parsed_id
                self.root.after(0, lambda rid=resource_id, rx=x, ry=y: self._upsert_resource(rid, rx, ry, attacked=False))
                self.log(f"Recurso encontrado: {resource_id} ({x},{y})")
            elif event_type == 'ATTACK':
                resource_id = partes[2] if len(partes) > 2 else "?"
                self.root.after(0, lambda rid=resource_id: self._upsert_resource(rid, attacked=True))
                self.log(f"Ataque sobre recurso {resource_id}")
            elif event_type == 'DEFENDED':
                resource_id = partes[2] if len(partes) > 2 else None
                if resource_id:
                    self.root.after(0, lambda rid=resource_id: self._upsert_resource(rid, attacked=False))
                actor = partes[3] if len(partes) > 3 else "defensor"
                self.log(f"Defensa aplicada por {actor}")
            elif event_type == 'GAME_OVER':
                self.root.after(0, lambda: self.status_var.set("PARTIDA FINALIZADA"))
        elif c == 'ERR':
            code = partes[1] if len(partes) > 1 else "?"
            desc = " ".join(partes[2:]) if len(partes) > 2 else "Error de protocolo"
            self.log(f"[SERVER-ERR {code}] {desc}")

            if code == '401':
                self.root.after(0, lambda: self.status_var.set("No autorizado (ERR 401)"))
            elif code == '403':
                self.root.after(0, lambda: self.status_var.set("Accion prohibida (ERR 403)"))
            elif code == '409':
                self.root.after(0, lambda: self.status_var.set("Conflicto de estado (ERR 409)"))
            elif code == '503':
                self.root.after(0, lambda: self.status_var.set("Servidor no disponible (ERR 503)"))

    def log(self, message):
        print(message)
        self.root.after(0, lambda: self._append_log(message))

    def _append_log(self, message):
        self.log_box.configure(state="normal")
        self.log_box.insert(tk.END, message + "\n")
        self.log_box.see(tk.END)
        self.log_box.configure(state="disabled")

    def cerrar_conexion(self):
        if self.connected:
            try:
                self.enviar_comando("QUIT")
                self.client_socket.close()
            except OSError:
                pass
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = AtacanteCLI(root)
    root.mainloop()
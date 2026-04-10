import hashlib, json, os, getpass

USERS_FILE = "users.json"

def hash_password(pw):
    return hashlib.sha256(pw.encode()).hexdigest()

def load_existing():
    if os.path.exists(USERS_FILE):
        with open(USERS_FILE, encoding="utf-8") as f:
            return json.load(f)
    return []

def save_users(users):
    with open(USERS_FILE, "w", encoding="utf-8") as f:
        json.dump(users, f, indent=2, ensure_ascii=False)

def main():
    users = load_existing()
    existing_names = {u["usuario"] for u in users}
    print(f"Usuarios existentes: {list(existing_names) or 'ninguno'}")
    print("Ingresa los datos del nuevo usuario (Ctrl+C para terminar)\n")

    while True:
        try:
            usuario = input("Usuario: ").strip()
            if not usuario:
                print("El nombre no puede estar vacío.")
                continue
            if len(usuario) > 63:
                print("Máximo 63 caracteres.")
                continue
            if usuario in existing_names:
                print(f"El usuario '{usuario}' ya existe.")
                continue

            password = getpass.getpass("Contraseña: ")
            if not password or len(password) > 63:
                print("Contraseña inválida (1-63 caracteres).")
                continue

            rol = input("Rol (ATTACKER / DEFENDER): ").strip().upper()
            if rol not in ("ATTACKER", "DEFENDER"):
                print("Rol inválido. Debe ser ATTACKER o DEFENDER.")
                continue

            users.append({
                "usuario": usuario,
                "password_hash": hash_password(password),
                "rol": rol
            })
            existing_names.add(usuario)
            save_users(users)
            print(f"✓ Usuario '{usuario}' agregado como {rol}\n")

        except KeyboardInterrupt:
            print(f"\nArchivo guardado: {USERS_FILE} ({len(users)} usuarios)")
            break

if __name__ == "__main__":
    main()
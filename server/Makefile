# ═══════════════════════════════════════════════════════════════════════════
# Makefile — Servidor Central CyberGame (CGSP)
#
# CÓMO USAR:
#   make          → Compila el servidor
#   make clean    → Elimina los archivos compilados
#   make run      → Compila y ejecuta en el puerto 8081 con logs en server.log
#   make help     → Muestra este mensaje
#
# COMPILADOR Y FLAGS:
#   CC = gcc                → GNU C Compiler
#   -Wall                   → Activar TODOS los warnings
#   -Wextra                 → Warnings adicionales (variables sin usar, etc.)
#   -g                      → Incluir información de depuración (para gdb)
#   -pthread                → Habilitar soporte de pthreads (hilos POSIX)
#   -lm                     → Enlazar con la librería matemática (sqrt en game_logic)
#
# ESTRUCTURA:
#   src/server.c            → main() — entrada del programa
#   src/game_logic.c        → lógica del juego
#   src/protocol.c          → parser y dispatcher CGSP
#   src/logger.c            → sistema de logging thread-safe
#   src/net_utils.c         → utilidades de red y DNS
#   src/identity.c          → cliente del servicio de identidad
# ═══════════════════════════════════════════════════════════════════════════

# Compilador y flags
CC      = gcc
CFLAGS  = -Wall -Wextra -g -pthread
LDFLAGS = -pthread -lm

# Directorios
SRC_DIR = src
INC_DIR = include

# Nombre del ejecutable final
TARGET  = server

# ── Archivos fuente (.c) y objetos (.o) ─────────────────────────────────────
#
# wildcard busca todos los .c en src/
# patsubst reemplaza src/%.c → src/%.o para obtener la lista de .o
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(SRC_DIR)/%.o, $(SRCS))

# ── Regla principal: compilar el ejecutable ──────────────────────────────────
#
# $@ = nombre del target (server)
# $^ = todos los prerequisitos (todos los .o)
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "✓ Compilación exitosa → ejecutable: ./$(TARGET)"
	@echo "  Uso: ./$(TARGET) <puerto> <archivoDeLogs>"
	@echo "  Ejemplo: ./$(TARGET) 8080 server.log"
	@echo ""

# ── Regla para compilar cada .c en su .o ────────────────────────────────────
#
# $< = el primer prerequisito (el .c correspondiente)
# $@ = el objetivo (el .o)
# -I$(INC_DIR) → decirle al compilador dónde buscar los .h
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# ── Limpiar archivos compilados ──────────────────────────────────────────────
.PHONY: clean
clean:
	rm -f $(SRC_DIR)/*.o $(TARGET)
	@echo "✓ Limpieza completada"

# ── Compilar y ejecutar directamente ────────────────────────────────────────
.PHONY: run
run: $(TARGET)
	./$(TARGET) 8081 server.log

# ── Mostrar ayuda ────────────────────────────────────────────────────────────
.PHONY: help
help:
	@echo "Comandos disponibles:"
	@echo "  make        → Compila el servidor"
	@echo "  make clean  → Elimina archivos compilados (.o y ejecutable)"
	@echo "  make run    → Compila y ejecuta en puerto 8081"
	@echo "  make help   → Muestra esta ayuda"

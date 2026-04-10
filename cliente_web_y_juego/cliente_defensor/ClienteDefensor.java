import javax.swing.*;
import java.awt.*;
import java.awt.event.KeyAdapter;
import java.awt.event.KeyEvent;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public class ClienteDefensor extends JFrame {
    private static final int MAP_WIDTH = 100;
    private static final int MAP_HEIGHT = 100;
    private static final int CANVAS_SIZE = 500;

    private static final String HOST = envOrDefault("CGSP_HOST", "localhost");
    private static final int PORT = parseIntEnv("CGSP_PORT", 8081);
    private static final String CGSP_USER = envOrDefault("CGSP_USER", "defensor1");
    private static final String CGSP_PASS = envOrDefault("CGSP_PASS", "pass123");
    private static final String CGSP_ROOM = envOrDefault("CGSP_ROOM", "1");

    private Socket socket;
    private PrintWriter out;
    private BufferedReader in;
    private boolean connected = false;
    private String estado = "CONECTADO";

    // Variables del jugador
    private int x = 0; // Coordenadas lógicas del servidor (0..99)
    private int y = 0;
    private int step = 10;
    private final int playerSize = 15;
    private final int resourceSize = 8;
    private final Map<String, Point> resourcePositions = new HashMap<>();
    private final Set<String> attackedResources = new HashSet<>();

    // Componentes de la UI
    private JPanel canvas;
    private JLabel statusLabel;
    private DefaultListModel<String> logModel;

    public ClienteDefensor() {
        super("CyberDef - Cliente Defensor");
        setSize(700, 760);
        setDefaultCloseOperation(JFrame.DO_NOTHING_ON_CLOSE);
        setResizable(false);
        setLayout(new BorderLayout());

        setupUI();
        conectarServidor();

        addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosing(WindowEvent e) {
                cerrarConexion();
            }
        });
    }

    private void setupUI() {
        // Status header
        statusLabel = new JLabel("Desconectado", SwingConstants.CENTER);
        statusLabel.setFont(new Font("Arial", Font.BOLD, 14));
        statusLabel.setForeground(Color.RED);
        add(statusLabel, BorderLayout.NORTH);

        // Plano del juego
        canvas = new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                // Fondo oscuro
                g.setColor(new Color(30, 30, 30));
                g.fillRect(0, 0, getWidth(), getHeight());

                // Dibujar límite (simulando las distancias de 0 a 100 x escala)
                g.setColor(Color.BLUE);
                g.drawRect(0, 0, CANVAS_SIZE - 1, CANVAS_SIZE - 1);

                // Dibujar al defensor (Círculo Azul)
                Point playerPixel = worldToCanvas(x, y);
                g.setColor(Color.CYAN);
                g.fillOval(playerPixel.x - playerSize, playerPixel.y - playerSize, playerSize * 2, playerSize * 2);
                g.setColor(Color.WHITE);
                g.drawOval(playerPixel.x - playerSize, playerPixel.y - playerSize, playerSize * 2, playerSize * 2);

                // Dibujar recursos críticos y marcar los que están bajo ataque.
                for (Map.Entry<String, Point> entry : resourcePositions.entrySet()) {
                    String resourceId = entry.getKey();
                    Point p = entry.getValue();
                    Point resourcePixel = worldToCanvas(p.x, p.y);
                    boolean attacked = attackedResources.contains(resourceId);

                    Color fill = attacked ? new Color(220, 38, 38) : new Color(34, 197, 94);
                    Color outline = attacked ? new Color(127, 29, 29) : new Color(20, 83, 45);

                    g.setColor(fill);
                    g.fillRect(resourcePixel.x - resourceSize, resourcePixel.y - resourceSize, resourceSize * 2,
                            resourceSize * 2);
                    g.setColor(outline);
                    g.drawRect(resourcePixel.x - resourceSize, resourcePixel.y - resourceSize, resourceSize * 2,
                            resourceSize * 2);

                    g.setColor(Color.WHITE);
                    g.setFont(new Font("Consolas", Font.BOLD, 11));
                    g.drawString("R" + resourceId, resourcePixel.x - resourceSize,
                            resourcePixel.y - (resourceSize + 4));
                }
            }
        };
        canvas.setPreferredSize(new Dimension(CANVAS_SIZE, CANVAS_SIZE));
        add(canvas, BorderLayout.CENTER);

        // Controles e info
        JPanel bottomPanel = new JPanel(new BorderLayout());
        JLabel tutorial = new JLabel("Controles: W,A,S,D (Mover), F (Defender recurso atacado mas cercano)");
        tutorial.setHorizontalAlignment(SwingConstants.CENTER);
        bottomPanel.add(tutorial, BorderLayout.NORTH);

        JPanel controlsPanel = new JPanel(new FlowLayout(FlowLayout.CENTER, 8, 8));
        JButton upBtn = new JButton("W");
        JButton leftBtn = new JButton("A");
        JButton downBtn = new JButton("S");
        JButton rightBtn = new JButton("D");
        JButton defendBtn = new JButton("DEFEND AUTO");

        upBtn.addActionListener(e -> moverJugador(0, -step));
        leftBtn.addActionListener(e -> moverJugador(-step, 0));
        downBtn.addActionListener(e -> moverJugador(0, step));
        rightBtn.addActionListener(e -> moverJugador(step, 0));
        defendBtn.addActionListener(e -> defenderRecursoObjetivo());

        controlsPanel.add(upBtn);
        controlsPanel.add(leftBtn);
        controlsPanel.add(downBtn);
        controlsPanel.add(rightBtn);
        controlsPanel.add(defendBtn);
        bottomPanel.add(controlsPanel, BorderLayout.CENTER);

        logModel = new DefaultListModel<>();
        JList<String> logList = new JList<>(logModel);
        JScrollPane scrollPane = new JScrollPane(logList);
        scrollPane.setPreferredSize(new Dimension(600, 100));
        bottomPanel.add(scrollPane, BorderLayout.SOUTH);

        add(bottomPanel, BorderLayout.SOUTH);

        // Manejo de teclado
        addKeyListener(new KeyAdapter() {
            @Override
            public void keyPressed(KeyEvent e) {
                if (!connected)
                    return;

                char c = Character.toLowerCase(e.getKeyChar());
                if (c == 'w') {
                    moverJugador(0, -step);
                } else if (c == 's') {
                    moverJugador(0, step);
                } else if (c == 'a') {
                    moverJugador(-step, 0);
                } else if (c == 'd') {
                    moverJugador(step, 0);
                } else if (c == 'f') {
                    defenderRecursoObjetivo();
                    return;
                } // Botón DEFEND para CGSP
            }
        });

        // Asegurarse de enfocar el frame para el teclado
        setFocusable(true);
        requestFocusInWindow();
    }

    private void moverJugador(int dx, int dy) {
        if (!connected)
            return;

        int newX = Math.max(0, Math.min(MAP_WIDTH - 1, x + dx));
        int newY = Math.max(0, Math.min(MAP_HEIGHT - 1, y + dy));

        if (newX == x && newY == y) {
            return;
        }

        x = newX;
        y = newY;
        canvas.repaint();
        enviarEstado("MOVE " + dx + " " + dy + "\n");
    }

    private Point worldToCanvas(int worldX, int worldY) {
        double scaleX = (double) (CANVAS_SIZE - 1) / (MAP_WIDTH - 1);
        double scaleY = (double) (CANVAS_SIZE - 1) / (MAP_HEIGHT - 1);
        int canvasX = (int) Math.round(worldX * scaleX);
        int canvasY = (int) Math.round(worldY * scaleY);
        return new Point(canvasX, canvasY);
    }

    private void defenderRecursoObjetivo() {
        String selected = null;
        int bestDist2 = Integer.MAX_VALUE;

        // Priorizar recursos actualmente bajo ataque.
        for (String resourceId : attackedResources) {
            Point p = resourcePositions.get(resourceId);
            if (p == null) {
                continue;
            }

            int dx = p.x - x;
            int dy = p.y - y;
            int dist2 = dx * dx + dy * dy;
            if (dist2 < bestDist2) {
                bestDist2 = dist2;
                selected = resourceId;
            }
        }

        // Fallback: si no hay eventos ATTACK, usar el recurso conocido mas cercano.
        if (selected == null) {
            for (Map.Entry<String, Point> entry : resourcePositions.entrySet()) {
                Point p = entry.getValue();
                int dx = p.x - x;
                int dy = p.y - y;
                int dist2 = dx * dx + dy * dy;
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    selected = entry.getKey();
                }
            }
        }

        if (selected == null) {
            selected = "0";
        }

        enviarEstado("DEFEND " + selected + "\n");
        logModel.add(0, "C->S: DEFEND " + selected);
    }

    private void conectarServidor() {
        try {
            // Resolver DNS como exige la rúbrica
            InetAddress address = InetAddress.getByName(HOST);
            socket = new Socket(address, PORT);
            out = new PrintWriter(socket.getOutputStream(), true);
            in = new BufferedReader(new InputStreamReader(socket.getInputStream(), "UTF-8"));
            connected = true;

            statusLabel.setText("CGSP Socket IP: " + address.getHostAddress() + ":" + PORT + " | usuario=" + CGSP_USER
                    + " sala=" + CGSP_ROOM);
            statusLabel.setForeground(new Color(0, 150, 0));

            // Loggeo en consola visual
            logModel.addElement("Sistema conectado. Iniciando Handshake CGSP...");

            // Hilo de recepción asíncrona
            Thread rxThread = new Thread(this::recibirEventos);
            rxThread.setDaemon(true); // Termina cuando cerramos la ventana
            rxThread.start();

            // Máquina de estados: Enviar autenticación
            enviarEstado("AUTH " + CGSP_USER + " " + CGSP_PASS + "\n");

        } catch (UnknownHostException e) {
            statusLabel.setText("Fallo resolución DNS de " + HOST);
            logModel.addElement("Error DNS: " + e.getMessage());
        } catch (Exception e) {
            statusLabel.setText("Servidor CGSP caído en " + HOST + ":" + PORT);
            logModel.addElement("Error TCP: El servidor de juego aún no está encendido.");
        }
    }

    private void enviarEstado(String mensaje) {
        if (out != null) {
            out.print(mensaje);
            out.flush();
        }
    }

    private void recibirEventos() {
        try {
            String linea;
            // Lee línea por línea gracias al \n en UTF-8 como requiere CGSP v1.0
            while (connected && (linea = in.readLine()) != null) {
                if (linea.trim().isEmpty())
                    continue;

                final String comando = linea;
                // Despachar a la interfaz gráfica en su hilo (EDT)
                SwingUtilities.invokeLater(() -> procesarComandoCGSP(comando));
            }
        } catch (Exception e) {
            SwingUtilities.invokeLater(() -> {
                logModel.addElement("Desconexión abrupta o IOException: " + e.getMessage());
                statusLabel.setText("Host Desconectado");
                statusLabel.setForeground(Color.RED);
            });
        }
        connected = false;
    }

    private void procesarComandoCGSP(String comando) {
        // Mostrar en nuestro log visible
        logModel.add(0, "S->C: " + comando);
        if (logModel.size() > 20)
            logModel.removeElementAt(20);

        String[] partes = comando.split(" ");
        String c = partes[0].toUpperCase();

        if (c.equals("OK")) {
            if (comando.contains("Bienvenido")) {
                estado = "AUTENTICADO";
            } else if (partes.length >= 4 && partes[1].startsWith("Posicion")) {
                int serverX = parseSafeInt(partes[2], x);
                int serverY = parseSafeInt(partes[3], y);
                x = Math.max(0, Math.min(MAP_WIDTH - 1, serverX));
                y = Math.max(0, Math.min(MAP_HEIGHT - 1, serverY));
                canvas.repaint();
            }
        } else if (c.equals("ROLE")) {
            estado = "AUTENTICADO";
            // Una vez logueado, unirse a la sala y luego intentar iniciar la partida
            // (esperando que ya haya al menos 1 atacante y 1 defensor)
            new Thread(() -> {
                try {
                    Thread.sleep(300);
                } catch (InterruptedException ignored) {
                }
                SwingUtilities.invokeLater(() -> enviarEstado("JOIN " + CGSP_ROOM + "\n"));
                try {
                    Thread.sleep(400);
                } catch (InterruptedException ignored) {
                }
                SwingUtilities.invokeLater(() -> enviarEstado("START\n"));
            }).start();
        } else if (c.equals("EVENT")) {
            if (partes.length < 2)
                return;
            String ev = partes[1];

            switch (ev) {
                case "GAME_STARTED":
                    estado = "EN_PARTIDA";
                    statusLabel.setText("PARTIDA EN MARCHA - ROL: DEFENSOR");
                    break;
                case "RESOURCE_INFO":
                    // El servidor nos da info de los recursos críticos al iniciar
                    if (partes.length >= 5) {
                        int rx = Math.max(0, Math.min(MAP_WIDTH - 1, parseSafeInt(partes[3], 0)));
                        int ry = Math.max(0, Math.min(MAP_HEIGHT - 1, parseSafeInt(partes[4], 0)));
                        resourcePositions.put(partes[2], new Point(rx, ry));
                        attackedResources.remove(partes[2]);
                        canvas.repaint();
                        logModel.add(0, "Recurso Critico Identificado en: " + partes[2] + " [" + partes[3] + ","
                                + partes[4] + "]");
                    } else {
                        logModel.add(0, "EVENT RESOURCE_INFO recibido con formato incompleto");
                    }
                    break;
                case "ATTACK":
                    if (partes.length >= 4) {
                        attackedResources.add(partes[2]);
                        canvas.repaint();
                        logModel.add(0, "ALERTA: Recurso " + partes[2] + " atacado por " + partes[3]);
                    } else {
                        logModel.add(0, "EVENT ATTACK recibido con formato incompleto");
                    }
                    break;
                case "DEFENDED":
                    if (partes.length >= 3) {
                        attackedResources.remove(partes[2]);
                        canvas.repaint();
                    }
                    logModel.add(0, "Exito: Defensa aplicada.");
                    break;
            }
        } else if (c.equals("ERR")) {
            String code = partes.length > 1 ? partes[1] : "?";
            logModel.add(0, "[SERVER-ERR " + code + "] " + comando);

            if (code.equals("401")) {
                statusLabel.setText("No autorizado (ERR 401)");
            } else if (code.equals("403")) {
                statusLabel.setText("Accion prohibida (ERR 403)");
            } else if (code.equals("409")) {
                statusLabel.setText("Conflicto de estado (ERR 409)");
            } else if (code.equals("503")) {
                statusLabel.setText("Servidor no disponible (ERR 503)");
            }
        }
    }

    private int parseSafeInt(String value, int fallback) {
        try {
            return Integer.parseInt(value);
        } catch (Exception e) {
            return fallback;
        }
    }

    private static String envOrDefault(String key, String fallback) {
        String value = System.getenv(key);
        if (value == null || value.trim().isEmpty()) {
            return fallback;
        }
        return value.trim();
    }

    private static int parseIntEnv(String key, int fallback) {
        String value = System.getenv(key);
        if (value == null || value.trim().isEmpty()) {
            return fallback;
        }
        try {
            return Integer.parseInt(value.trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private void cerrarConexion() {
        connected = false;
        try {
            if (out != null) {
                out.print("QUIT\n");
                out.flush();
            }
        } catch (Exception ignored) {
        }

        try {
            if (in != null)
                in.close();
        } catch (Exception ignored) {
        }

        try {
            if (out != null)
                out.close();
        } catch (Exception ignored) {
        }

        try {
            if (socket != null && !socket.isClosed())
                socket.close();
        } catch (Exception ignored) {
        }

        dispose();
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> {
            ClienteDefensor client = new ClienteDefensor();
            client.setVisible(true);
        });
    }
}
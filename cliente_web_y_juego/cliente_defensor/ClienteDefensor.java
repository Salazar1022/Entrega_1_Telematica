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
    private static final String HOST = "localhost";
    private static final int PORT = 8081;

    private Socket socket;
    private PrintWriter out;
    private BufferedReader in;
    private boolean connected = false;
    private String estado = "CONECTADO";

    // Variables del jugador
    private int x = 400; // Posición inicial
    private int y = 400;
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
        setSize(600, 650);
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
                g.drawRect(0, 0, 500 - 1, 500 - 1);

                // Dibujar al defensor (Círculo Azul)
                g.setColor(Color.CYAN);
                g.fillOval(x - playerSize, y - playerSize, playerSize * 2, playerSize * 2);
                g.setColor(Color.WHITE);
                g.drawOval(x - playerSize, y - playerSize, playerSize * 2, playerSize * 2);

                // Dibujar recursos críticos y marcar los que están bajo ataque.
                for (Map.Entry<String, Point> entry : resourcePositions.entrySet()) {
                    String resourceId = entry.getKey();
                    Point p = entry.getValue();
                    boolean attacked = attackedResources.contains(resourceId);

                    Color fill = attacked ? new Color(220, 38, 38) : new Color(34, 197, 94);
                    Color outline = attacked ? new Color(127, 29, 29) : new Color(20, 83, 45);

                    g.setColor(fill);
                    g.fillRect(p.x - resourceSize, p.y - resourceSize, resourceSize * 2, resourceSize * 2);
                    g.setColor(outline);
                    g.drawRect(p.x - resourceSize, p.y - resourceSize, resourceSize * 2, resourceSize * 2);

                    g.setColor(Color.WHITE);
                    g.setFont(new Font("Consolas", Font.BOLD, 11));
                    g.drawString("R" + resourceId, p.x - resourceSize, p.y - (resourceSize + 4));
                }
            }
        };
        canvas.setPreferredSize(new Dimension(500, 500));
        add(canvas, BorderLayout.CENTER);

        // Controles e info
        JPanel bottomPanel = new JPanel(new BorderLayout());
        JLabel tutorial = new JLabel("Controles: W,A,S,D (Mover), F (Defender Recurso 0)");
        tutorial.setHorizontalAlignment(SwingConstants.CENTER);
        bottomPanel.add(tutorial, BorderLayout.NORTH);

        JPanel controlsPanel = new JPanel(new FlowLayout(FlowLayout.CENTER, 8, 8));
        JButton upBtn = new JButton("W");
        JButton leftBtn = new JButton("A");
        JButton downBtn = new JButton("S");
        JButton rightBtn = new JButton("D");
        JButton defendBtn = new JButton("DEFEND 0");

        upBtn.addActionListener(e -> moverJugador(0, -step));
        leftBtn.addActionListener(e -> moverJugador(-step, 0));
        downBtn.addActionListener(e -> moverJugador(0, step));
        rightBtn.addActionListener(e -> moverJugador(step, 0));
        defendBtn.addActionListener(e -> enviarEstado("DEFEND 0\n"));

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
                    enviarEstado("DEFEND 0\n");
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

        // Prevenir salir del mapa (0-500 en la GUI)
        if (x + dx >= 0 && x + dx <= 500 && y + dy >= 0 && y + dy <= 500) {
            x += dx;
            y += dy;
            canvas.repaint();
            enviarEstado("MOVE " + dx + " " + dy + "\n");
        }
    }

    private void conectarServidor() {
        try {
            // Resolver DNS como exige la rúbrica
            InetAddress address = InetAddress.getByName(HOST);
            socket = new Socket(address, PORT);
            out = new PrintWriter(socket.getOutputStream(), true);
            in = new BufferedReader(new InputStreamReader(socket.getInputStream(), "UTF-8"));
            connected = true;

            statusLabel.setText("CGSP Socket IP: " + address.getHostAddress() + ":" + PORT);
            statusLabel.setForeground(new Color(0, 150, 0));

            // Loggeo en consola visual
            logModel.addElement("Sistema conectado. Iniciando Handshake CGSP...");

            // Hilo de recepción asíncrona
            Thread rxThread = new Thread(this::recibirEventos);
            rxThread.setDaemon(true); // Termina cuando cerramos la ventana
            rxThread.start();

            // Máquina de estados: Enviar autenticación
            enviarEstado("AUTH defensor1 seg2026\n");

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
            }
        } else if (c.equals("ROLE")) {
            estado = "AUTENTICADO";
            // Una vez logueado, simula unirse a la sala
            enviarEstado("JOIN 3\n");
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
                        int rx = parseSafeInt(partes[3], 0);
                        int ry = parseSafeInt(partes[4], 0);
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
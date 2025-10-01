// TelemetryClient.java
// Compilar: javac TelemetryClient.java
// Ejecutar: java TelemetryClient <host> <port>
// Ejemplo: java TelemetryClient 127.0.0.1 9000

import javax.swing.*;
import java.awt.*;
import java.io.*;
import java.net.*;
import java.util.*;

public class TelemetryClient {
    private JFrame frame;
    private JTextField hostField, portField, cmdField;
    private JButton btnConnect, btnSend, btnHello, btnAuth, btnBye;
    private JLabel lblStatus, lblRole;
    private JLabel lblSpeed, lblBattery, lblTemp, lblHeading;
    private JTextArea logArea;

    private Socket socket;
    private BufferedWriter writer;
    private BufferedReader reader;
    private Thread readerThread;
    private volatile boolean expectingUsers = false;

    private final String hostDefault;
    private final int portDefault;

    public TelemetryClient(String host, int port) {
        this.hostDefault = host;
        this.portDefault = port;
        initUI();
    }

    private void initUI() {
        frame = new JFrame("Telemetry Client - PROTO 1.0");
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        frame.setSize(760, 520);
        frame.setLocationRelativeTo(null);

        // Top panel: connection
        JPanel top = new JPanel(new FlowLayout(FlowLayout.LEFT));
        top.add(new JLabel("Host:"));
        hostField = new JTextField(hostDefault, 12);
        top.add(hostField);
        top.add(new JLabel("Port:"));
        portField = new JTextField(String.valueOf(portDefault), 6);
        top.add(portField);
        btnConnect = new JButton("Conectar");
        top.add(btnConnect);
        lblStatus = new JLabel("Desconectado");
        top.add(lblStatus);
        lblRole = new JLabel("Role: VIEWER");
        top.add(lblRole);

        // Middle left: telemetry panel
        JPanel telemetry = new JPanel(new GridLayout(4, 2, 8, 8));
        telemetry.setBorder(BorderFactory.createTitledBorder("Telemetria (ultimos valores)"));
        telemetry.add(new JLabel("Velocidad (km/h):"));
        lblSpeed = new JLabel("-");
        telemetry.add(lblSpeed);
        telemetry.add(new JLabel("Bateria (%):"));
        lblBattery = new JLabel("-");
        telemetry.add(lblBattery);
        telemetry.add(new JLabel("Temperatura (C):"));
        lblTemp = new JLabel("-");
        telemetry.add(lblTemp);
        telemetry.add(new JLabel("Rumbo (deg):"));
        lblHeading = new JLabel("-");
        telemetry.add(lblHeading);

        // Middle right: controls
        JPanel controls = new JPanel(new GridLayout(6, 1, 6, 6));
        controls.setBorder(BorderFactory.createTitledBorder("Controles"));
        btnHello = new JButton("HELLO");
        btnAuth = new JButton("AUTH");
        btnBye = new JButton("BYE");
        controls.add(btnHello);
        controls.add(btnAuth);
        controls.add(new JLabel("Comando libre (ej: CMD SPEED_UP)"));
        cmdField = new JTextField();
        controls.add(cmdField);
        btnSend = new JButton("Enviar");
        controls.add(btnSend);
        controls.add(btnBye);

        // Bottom: log
        logArea = new JTextArea();
        logArea.setEditable(false);
        JScrollPane logScroll = new JScrollPane(logArea);
        logScroll.setBorder(BorderFactory.createTitledBorder("Log"));

        // Layout assembly
        JPanel center = new JPanel(new BorderLayout(8, 8));
        JPanel leftBig = new JPanel(new BorderLayout(6, 6));
        leftBig.add(telemetry, BorderLayout.NORTH);
        leftBig.add(controls, BorderLayout.CENTER);
        center.add(leftBig, BorderLayout.WEST);
        center.add(logScroll, BorderLayout.CENTER);

        frame.getContentPane().add(top, BorderLayout.NORTH);
        frame.getContentPane().add(center, BorderLayout.CENTER);

        // Actions
        btnConnect.addActionListener(e -> {
            if (socket == null || socket.isClosed()) connect();
            else disconnect();
        });
        btnHello.addActionListener(e -> doHello());
        btnAuth.addActionListener(e -> doAuth());
        btnSend.addActionListener(e -> doSendManual());
        btnBye.addActionListener(e -> doBye());
        cmdField.addActionListener(e -> doSendManual());

        frame.setVisible(true);
    }

    private void appendLog(String s) {
        SwingUtilities.invokeLater(() -> {
            logArea.append(s + "\n");
            logArea.setCaretPosition(logArea.getDocument().getLength());
        });
        System.out.println("[CLIENT] " + s);
    }

    private void connect() {
        String host = hostField.getText().trim();
        int port = Integer.parseInt(portField.getText().trim());
        appendLog("Conectando a " + host + ":" + port + " ...");
        try {
            socket = new Socket();
            socket.connect(new InetSocketAddress(host, port), 5000);
            writer = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream(), "UTF-8"));
            reader = new BufferedReader(new InputStreamReader(socket.getInputStream(), "UTF-8"));
            startReaderThread();
            lblStatus.setText("Conectado a " + host + ":" + port);
            btnConnect.setText("Desconectar");
            appendLog("Conectado.");
        } catch (IOException ex) {
            appendLog("ERROR conectando: " + ex.getMessage());
        }
    }

    private void disconnect() {
        appendLog("Cerrando conexión...");
        closeResources();
        lblStatus.setText("Desconectado");
        btnConnect.setText("Conectar");
    }

    private void closeResources() {
        try {
            if (readerThread != null && readerThread.isAlive()) readerThread.interrupt();
            if (writer != null) {
                try { writer.flush(); } catch (Exception ignored){}
            }
            if (socket != null && !socket.isClosed()) socket.close();
        } catch (IOException e) {
            // ignore
        } finally {
            writer = null;
            reader = null;
            socket = null;
        }
    }

    private void startReaderThread() {
        readerThread = new Thread(() -> {
            try {
                String line;
                while ((line = reader.readLine()) != null) {
                    final String l = line;
                    handleLine(l);
                }
            } catch (IOException e) {
                appendLog("Conexión cerrada por error: " + e.getMessage());
            } finally {
                SwingUtilities.invokeLater(() -> {
                    lblStatus.setText("Desconectado");
                    btnConnect.setText("Conectar");
                });
            }
        }, "ReaderThread");
        readerThread.setDaemon(true);
        readerThread.start();
    }

    private void handleLine(String line) {
        appendLog("<< " + line);
        try {
            if (line.startsWith("DATA ")) {
                parseData(line.substring(5));
            } else if (line.startsWith("WELCOME")) {
                appendLog("[WELCOME] " + line);
            } else if (line.startsWith("ROLE ")) {
                final String role = line.substring(5).trim();
                SwingUtilities.invokeLater(() -> lblRole.setText("Role: " + role));
            } else if (line.startsWith("ACK ")) {
                appendLog("[ACK] " + line);
            } else if (line.startsWith("NACK ")) {
                appendLog("[NACK] " + line);
            } else if (line.startsWith("ERROR ")) {
                appendLog("[ERROR] " + line);
            } else if (line.startsWith("USERS count=")) {
                expectingUsers = true;
                appendLog("[USERS] " + line);
            } else if (expectingUsers && line.startsWith("USER ")) {
                appendLog("[USER] " + line);
            } else if (expectingUsers && line.startsWith("OK users")) {
                expectingUsers = false;
                appendLog("[USERS END] " + line);
            } else {
                appendLog("[UNKNOWN] " + line);
            }
        } catch (Exception ex) {
            appendLog("Error parseando línea: " + ex.getMessage());
        }
    }

    private void parseData(String payload) {
        // payload: speed=52.3 battery=89 temp=36.2 heading=175 ts=...
        Map<String, String> m = new HashMap<>();
        String[] parts = payload.split("\\s+");
        for (String p : parts) {
            int i = p.indexOf('=');
            if (i > 0) {
                String k = p.substring(0, i);
                String v = p.substring(i + 1);
                m.put(k, v);
            }
        }
        SwingUtilities.invokeLater(() -> {
            lblSpeed.setText(m.getOrDefault("speed", "-"));
            lblBattery.setText(m.getOrDefault("battery", "-"));
            lblTemp.setText(m.getOrDefault("temp", "-"));
            lblHeading.setText(m.getOrDefault("heading", "-"));
        });
    }

    private synchronized void sendRaw(String text) {
        if (writer == null) {
            appendLog("No conectado. Presione Conectar primero.");
            return;
        }
        try {
            // enviamos con CRLF explícito (el servidor acepta \n o \r\n)
            writer.write(text + "\r\n");
            writer.flush();
            appendLog(">> " + text);
        } catch (IOException ex) {
            appendLog("Error enviando: " + ex.getMessage());
        }
    }

    // UI helpers
    private void doHello() {
        String name = JOptionPane.showInputDialog(frame, "Nombre para HELLO:", "HELLO", JOptionPane.PLAIN_MESSAGE);
        if (name != null && !name.trim().isEmpty()) {
            sendRaw("HELLO " + name.trim());
        }
    }

    private void doAuth() {
        JPanel p = new JPanel(new GridLayout(2,2));
        JTextField user = new JTextField("admin");
        JPasswordField pass = new JPasswordField();
        p.add(new JLabel("User:")); p.add(user);
        p.add(new JLabel("Pass:")); p.add(pass);
        int ok = JOptionPane.showConfirmDialog(frame, p, "AUTH", JOptionPane.OK_CANCEL_OPTION);
        if (ok == JOptionPane.OK_OPTION) {
            String u = user.getText().trim();
            String pw = new String(pass.getPassword());
            if (!u.isEmpty()) {
                sendRaw("AUTH " + u + " " + pw);
            }
        }
    }

    private void doSendManual() {
        String cmd = cmdField.getText().trim();
        if (cmd.isEmpty()) return;
        sendRaw(cmd);
        cmdField.setText("");
    }

    private void doBye() {
        sendRaw("BYE");
        try { Thread.sleep(150); } catch (InterruptedException ignore) {}
        closeResources();
        lblRole.setText("Role: VIEWER");
    }

    // entry
    public static void main(String[] args) {
        String host = "127.0.0.1";
        int port = 9000;
        if (args.length >= 1) host = args[0];
        if (args.length >= 2) {
            try { port = Integer.parseInt(args[1]); } catch (Exception ignored) {}
        }
        TelemetryClient app = new TelemetryClient(host, port);
        SwingUtilities.invokeLater(() -> {
            // Already UI built in constructor
        });
    }
}

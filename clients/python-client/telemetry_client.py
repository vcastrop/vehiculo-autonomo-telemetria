#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Cliente de Telemetría para Vehículo Autónomo - PROTO 1.0
Ejecutar: python telemetry_client.py [host] [port]
Ejemplo: python telemetry_client.py 127.0.0.1 9000
"""

import socket
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, simpledialog
import sys
import time


class TelemetryClient:
    """Cliente de telemetría con interfaz gráfica para el protocolo PROTO 1.0"""
    
    def __init__(self, host="127.0.0.1", port=9000):
        self.default_host = host
        self.default_port = port
        
        # Socket y recursos de red
        self.socket = None
        self.reader_thread = None
        self.connected = False
        self.expecting_users = False
        
        # Configuración de la interfaz gráfica
        self.root = tk.Tk()
        self.root.title("Telemetry Client - PROTO 1.0")
        self.root.geometry("800x600")
        self.root.resizable(True, True)
        
        self.init_ui()
        
    def init_ui(self):
        """Inicializa todos los componentes de la interfaz gráfica"""
        
        # ===== Panel superior: Conexión =====
        top_frame = ttk.Frame(self.root, padding="10")
        top_frame.grid(row=0, column=0, columnspan=2, sticky=(tk.W, tk.E))
        
        ttk.Label(top_frame, text="Host:").grid(row=0, column=0, padx=5)
        self.host_entry = ttk.Entry(top_frame, width=15)
        self.host_entry.insert(0, self.default_host)
        self.host_entry.grid(row=0, column=1, padx=5)
        
        ttk.Label(top_frame, text="Port:").grid(row=0, column=2, padx=5)
        self.port_entry = ttk.Entry(top_frame, width=8)
        self.port_entry.insert(0, str(self.default_port))
        self.port_entry.grid(row=0, column=3, padx=5)
        
        self.btn_connect = ttk.Button(top_frame, text="Conectar", command=self.toggle_connection)
        self.btn_connect.grid(row=0, column=4, padx=10)
        
        self.lbl_status = ttk.Label(top_frame, text="Desconectado", foreground="red")
        self.lbl_status.grid(row=0, column=5, padx=10)
        
        self.lbl_role = ttk.Label(top_frame, text="Role: VIEWER", foreground="blue")
        self.lbl_role.grid(row=0, column=6, padx=10)
        
        # ===== Panel izquierdo: Telemetría =====
        left_frame = ttk.LabelFrame(self.root, text="Telemetría (últimos valores)", padding="10")
        left_frame.grid(row=1, column=0, padx=10, pady=10, sticky=(tk.N, tk.W, tk.E))
        
        # Labels para telemetría
        ttk.Label(left_frame, text="Velocidad (km/h):").grid(row=0, column=0, sticky=tk.W, pady=5)
        self.lbl_speed = ttk.Label(left_frame, text="-", font=("Arial", 12, "bold"))
        self.lbl_speed.grid(row=0, column=1, sticky=tk.W, pady=5, padx=10)
        
        ttk.Label(left_frame, text="Batería (%):").grid(row=1, column=0, sticky=tk.W, pady=5)
        self.lbl_battery = ttk.Label(left_frame, text="-", font=("Arial", 12, "bold"))
        self.lbl_battery.grid(row=1, column=1, sticky=tk.W, pady=5, padx=10)
        
        ttk.Label(left_frame, text="Temperatura (°C):").grid(row=2, column=0, sticky=tk.W, pady=5)
        self.lbl_temp = ttk.Label(left_frame, text="-", font=("Arial", 12, "bold"))
        self.lbl_temp.grid(row=2, column=1, sticky=tk.W, pady=5, padx=10)
        
        ttk.Label(left_frame, text="Rumbo (grados):").grid(row=3, column=0, sticky=tk.W, pady=5)
        self.lbl_heading = ttk.Label(left_frame, text="-", font=("Arial", 12, "bold"))
        self.lbl_heading.grid(row=3, column=1, sticky=tk.W, pady=5, padx=10)
        
        # ===== Panel de controles =====
        controls_frame = ttk.LabelFrame(left_frame, text="Controles", padding="10")
        controls_frame.grid(row=4, column=0, columnspan=2, pady=10, sticky=(tk.W, tk.E))
        
        ttk.Button(controls_frame, text="HELLO", command=self.do_hello).grid(row=0, column=0, pady=5, sticky=(tk.W, tk.E))
        ttk.Button(controls_frame, text="AUTH", command=self.do_auth).grid(row=1, column=0, pady=5, sticky=(tk.W, tk.E))
        
        ttk.Label(controls_frame, text="Comandos rápidos:").grid(row=2, column=0, pady=5)
        ttk.Button(controls_frame, text="SPEED UP", command=lambda: self.send_raw("CMD SPEED_UP")).grid(row=3, column=0, pady=2, sticky=(tk.W, tk.E))
        ttk.Button(controls_frame, text="SLOW DOWN", command=lambda: self.send_raw("CMD SLOW_DOWN")).grid(row=4, column=0, pady=2, sticky=(tk.W, tk.E))
        ttk.Button(controls_frame, text="TURN LEFT", command=lambda: self.send_raw("CMD TURN_LEFT")).grid(row=5, column=0, pady=2, sticky=(tk.W, tk.E))
        ttk.Button(controls_frame, text="TURN RIGHT", command=lambda: self.send_raw("CMD TURN_RIGHT")).grid(row=6, column=0, pady=2, sticky=(tk.W, tk.E))
        ttk.Button(controls_frame, text="USERS", command=lambda: self.send_raw("USERS")).grid(row=7, column=0, pady=2, sticky=(tk.W, tk.E))
        
        ttk.Label(controls_frame, text="Comando libre:").grid(row=8, column=0, pady=(10, 2))
        self.cmd_entry = ttk.Entry(controls_frame, width=25)
        self.cmd_entry.grid(row=9, column=0, pady=2)
        self.cmd_entry.bind('<Return>', lambda e: self.do_send_manual())
        ttk.Button(controls_frame, text="Enviar", command=self.do_send_manual).grid(row=10, column=0, pady=5, sticky=(tk.W, tk.E))
        
        ttk.Button(controls_frame, text="BYE", command=self.do_bye).grid(row=11, column=0, pady=10, sticky=(tk.W, tk.E))
        
        # ===== Panel derecho: Log =====
        right_frame = ttk.LabelFrame(self.root, text="Log de comunicación", padding="10")
        right_frame.grid(row=1, column=1, padx=10, pady=10, sticky=(tk.N, tk.S, tk.W, tk.E))
        
        self.log_area = scrolledtext.ScrolledText(right_frame, width=50, height=30, wrap=tk.WORD)
        self.log_area.pack(fill=tk.BOTH, expand=True)
        
        # Configurar expansión de filas y columnas
        self.root.grid_rowconfigure(1, weight=1)
        self.root.grid_columnconfigure(1, weight=1)
        
        # Configurar cierre de ventana
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
        
    def append_log(self, message):
        """Añade un mensaje al área de log"""
        timestamp = time.strftime("%H:%M:%S")
        self.log_area.insert(tk.END, f"[{timestamp}] {message}\n")
        self.log_area.see(tk.END)
        print(f"[CLIENT] {message}")
        
    def toggle_connection(self):
        """Conectar o desconectar del servidor"""
        if not self.connected:
            self.connect()
        else:
            self.disconnect()
            
    def connect(self):
        """Establece conexión con el servidor"""
        host = self.host_entry.get().strip()
        try:
            port = int(self.port_entry.get().strip())
        except ValueError:
            messagebox.showerror("Error", "Puerto inválido")
            return
            
        self.append_log(f"Conectando a {host}:{port}...")
        
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)
            self.socket.connect((host, port))
            self.socket.settimeout(None)  # Modo bloqueante para lectura
            
            self.connected = True
            self.lbl_status.config(text=f"Conectado a {host}:{port}", foreground="green")
            self.btn_connect.config(text="Desconectar")
            self.append_log("Conectado exitosamente")
            
            # Iniciar hilo de lectura
            self.reader_thread = threading.Thread(target=self.reader_loop, daemon=True)
            self.reader_thread.start()
            
        except socket.timeout:
            self.append_log("ERROR: Timeout al conectar")
            messagebox.showerror("Error", "Timeout al conectar al servidor")
        except ConnectionRefusedError:
            self.append_log("ERROR: Conexión rechazada")
            messagebox.showerror("Error", "Conexión rechazada. ¿Está el servidor corriendo?")
        except Exception as e:
            self.append_log(f"ERROR conectando: {e}")
            messagebox.showerror("Error", f"Error al conectar: {e}")
            
    def disconnect(self):
        """Cierra la conexión con el servidor"""
        self.append_log("Cerrando conexión...")
        self.close_resources()
        self.lbl_status.config(text="Desconectado", foreground="red")
        self.btn_connect.config(text="Conectar")
        self.lbl_role.config(text="Role: VIEWER")
        
    def close_resources(self):
        """Cierra el socket y recursos asociados"""
        self.connected = False
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            
    def reader_loop(self):
        """Hilo que lee mensajes del servidor continuamente"""
        try:
            buffer = ""
            while self.connected:
                try:
                    data = self.socket.recv(4096).decode('utf-8')
                    if not data:
                        self.append_log("Conexión cerrada por el servidor")
                        break
                    
                    buffer += data
                    # Procesar líneas completas
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            self.handle_line(line)
                            
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.connected:
                        self.append_log(f"Error en lectura: {e}")
                    break
        finally:
            self.root.after(0, lambda: self.lbl_status.config(text="Desconectado", foreground="red"))
            self.root.after(0, lambda: self.btn_connect.config(text="Conectar"))
            self.connected = False
            
    def handle_line(self, line):
        """Procesa una línea recibida del servidor"""
        self.append_log(f"<< {line}")
        
        try:
            if line.startswith("DATA "):
                self.parse_data(line[5:])
            elif line.startswith("WELCOME"):
                self.append_log(f"[WELCOME] {line}")
            elif line.startswith("ROLE "):
                role = line[5:].strip()
                self.root.after(0, lambda: self.lbl_role.config(text=f"Role: {role}"))
            elif line.startswith("ACK "):
                self.append_log(f"[ACK] {line}")
            elif line.startswith("NACK "):
                self.append_log(f"[NACK] {line}")
            elif line.startswith("ERROR "):
                self.append_log(f"[ERROR] {line}")
            elif line.startswith("USERS count="):
                self.expecting_users = True
                self.append_log(f"[USERS] {line}")
            elif self.expecting_users and line.startswith("USER "):
                self.append_log(f"[USER] {line}")
            elif self.expecting_users and line.startswith("OK users"):
                self.expecting_users = False
                self.append_log(f"[USERS END] {line}")
            elif line.startswith("OK "):
                self.append_log(f"[OK] {line}")
            else:
                self.append_log(f"[UNKNOWN] {line}")
        except Exception as e:
            self.append_log(f"Error parseando línea: {e}")
            
    def parse_data(self, payload):
        """Parsea el mensaje DATA y actualiza la interfaz"""
        # Formato: speed=52.3 battery=89 temp=36.2 heading=175 ts=...
        data = {}
        parts = payload.split()
        for part in parts:
            if '=' in part:
                key, value = part.split('=', 1)
                data[key] = value
                
        # Actualizar labels en el hilo principal
        self.root.after(0, lambda: self.lbl_speed.config(text=data.get('speed', '-')))
        self.root.after(0, lambda: self.lbl_battery.config(text=data.get('battery', '-')))
        self.root.after(0, lambda: self.lbl_temp.config(text=data.get('temp', '-')))
        self.root.after(0, lambda: self.lbl_heading.config(text=data.get('heading', '-')))
        
    def send_raw(self, message):
        """Envía un mensaje raw al servidor"""
        if not self.connected or not self.socket:
            self.append_log("No conectado. Presione Conectar primero.")
            messagebox.showwarning("Advertencia", "No está conectado al servidor")
            return
            
        try:
            self.socket.sendall((message + "\r\n").encode('utf-8'))
            self.append_log(f">> {message}")
        except Exception as e:
            self.append_log(f"Error enviando: {e}")
            messagebox.showerror("Error", f"Error al enviar mensaje: {e}")
            
    def do_hello(self):
        """Envía comando HELLO"""
        name = simpledialog.askstring("HELLO", "Ingrese su nombre:", parent=self.root)
        if name and name.strip():
            self.send_raw(f"HELLO {name.strip()}")
            
    def do_auth(self):
        """Envía comando AUTH para autenticación de administrador"""
        # Crear diálogo personalizado
        dialog = tk.Toplevel(self.root)
        dialog.title("AUTH - Autenticación")
        dialog.geometry("300x150")
        dialog.transient(self.root)
        dialog.grab_set()
        
        ttk.Label(dialog, text="Usuario:").grid(row=0, column=0, padx=10, pady=10, sticky=tk.W)
        user_entry = ttk.Entry(dialog, width=20)
        user_entry.insert(0, "admin")
        user_entry.grid(row=0, column=1, padx=10, pady=10)
        
        ttk.Label(dialog, text="Contraseña:").grid(row=1, column=0, padx=10, pady=10, sticky=tk.W)
        pass_entry = ttk.Entry(dialog, width=20, show="*")
        pass_entry.grid(row=1, column=1, padx=10, pady=10)
        
        def on_ok():
            user = user_entry.get().strip()
            password = pass_entry.get()
            if user:
                self.send_raw(f"AUTH {user} {password}")
            dialog.destroy()
            
        def on_cancel():
            dialog.destroy()
            
        btn_frame = ttk.Frame(dialog)
        btn_frame.grid(row=2, column=0, columnspan=2, pady=10)
        ttk.Button(btn_frame, text="OK", command=on_ok).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="Cancelar", command=on_cancel).pack(side=tk.LEFT, padx=5)
        
        user_entry.focus()
        dialog.bind('<Return>', lambda e: on_ok())
        
    def do_send_manual(self):
        """Envía comando manual desde el campo de entrada"""
        cmd = self.cmd_entry.get().strip()
        if cmd:
            self.send_raw(cmd)
            self.cmd_entry.delete(0, tk.END)
            
    def do_bye(self):
        """Envía comando BYE y cierra la conexión"""
        if self.connected:
            self.send_raw("BYE")
            time.sleep(0.15)
        self.close_resources()
        self.lbl_role.config(text="Role: VIEWER")
        
    def on_closing(self):
        """Maneja el cierre de la ventana"""
        if self.connected:
            self.do_bye()
        self.root.destroy()
        
    def run(self):
        """Inicia el cliente"""
        self.root.mainloop()


def main():
    """Función principal"""
    host = "127.0.0.1"
    port = 9000
    
    if len(sys.argv) >= 2:
        host = sys.argv[1]
    if len(sys.argv) >= 3:
        try:
            port = int(sys.argv[2])
        except ValueError:
            print(f"Puerto inválido: {sys.argv[2]}, usando {port}")
    
    print(f"Cliente de Telemetría - PROTO 1.0")
    print(f"Host por defecto: {host}")
    print(f"Puerto por defecto: {port}")
    
    client = TelemetryClient(host, port)
    client.run()


if __name__ == "__main__":
    main()

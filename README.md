# Proyecto: Vehículo Autónomo Telemetría

Simulación de un vehículo autónomo, se desarrolló un protocolo de telemetría que permite transmitir información en tiempo real y recibir comandos de control bajo un modelo cliente–servidor.

---

## Tabla de Contenidos
- [Descripción General](#descripción-general)
- [Guia de ejecución Servidor](#Guia-de-ejecución-Servidor)
- [Informe del Proyecto](#Informe-del-Proyecto)
- [Autoría](#autoría)

---

## Descripción General

El servidor se ejecuta sobre **Windows (Winsock2)** y permite la conexión de varios clientes mediante **sockets TCP**.  
Cada cliente puede autenticarse, enviar comandos o solo observar la telemetría del vehículo, dependiendo de su rol.

El sistema cumple con los requisitos del informe:
- Comunicación TCP tipo SOCK_STREAM.
- Envío de telemetría cada 10 segundos a todos los clientes.
- Roles ADMIN/VIEWER con autenticación (`AUTH admin 1234`).
- Comandos restringidos al rol ADMIN.
- Logging completo en consola y archivo opcional.
- Manejo concurrente de clientes mediante hilos (WinAPI).

---

## Guia de ejecución Servidor

---

### 1) Clonar el repositorio

**git clone https://github.com/vcastrop/vehiculo-autonomo-telemetria.git**

---

### 2) Compilar

**Opción A — GCC (MinGW)**

- gcc server.c -o server.exe -lws2_32

- gcc -Wall -O2 -o server src/server.c -lws2_32

**Opción B — Visual Studio (MSVC)**

- Compila server.c en un proyecto de consola. 

- No hace falta configurar librerías adicionales.

- Requisitos: Windows 7+; tener MinGW o Visual Studio instalados.

---

### 3) Iniciar el servidor

**server.exe puerto archivo_log**

Ejemplo:

**server.exe 8080 log.txt**

- El servidor escucha en 0.0.0.0:<puerto> y acepta múltiples clientes concurrentes.

- log.txt registra lo mismo que la consola (timestamp, IP:puerto, direcciones RX/TX y mensajes).

---

### 4) Conectar clientes usando netcat

**En Windows:**

- Windows incluye Ncat si tienes instalado Nmap (lo puedes descargar de: https://nmap.org/ncat/).

- Luego en PowerShell / CMD, ejecuta: ncat direccion_ip 8080

**En Linux:**

- sudo apt install ncat
- nc -v direccion_ip port 9000

**en MacOS:**

- brew install nmap
- ncat direccion_ip 8080

Nota: en direccion_ip escribe 127.0.0.1 si el cliente es en la misma maquina donde corre el servidor, si el cliente esta en otra maquina dentro de la misma red escribe la ip de la maquina donde corre el servidor. 

**Para ver la IP**
- ipconfig (windows)
- ifconfig  (linux/macOS)

**Al conectarte, el servidor envía:**

WELCOME TelemetryServer PROTO 1.0
ROLE VIEWER

El cliente empieza como VIEWER por defecto.

---

### 5) Telemetría automática

Cada ~10 s el servidor envía a todos los clientes una línea DATA:

**DATA speed=XX.X battery=XX.X temp=XX.X heading=XXX.X ts=XXXXXXXXXXXX**

Donde:

- speed km/h (0–120),

- battery % (baja 0.2 por ciclo),

- temp °C (20–60),

- heading grados normalizados (0–360),

- ts timestamp en milisegundos.

---

## Informe del Proyecto

Puedes consultar el informe completo aquí:  
[📘 Ver Informe del Proyecto (PDF)](./docs/informe%20(1).pdf)


---

## Autoría

**Proyecto académico:** Servidor de telemetría de vehículo autónomo

**Autores:** [jk, val, isa, j]

**Institución:** [Universidad EAFIT]

**Año:** 2025


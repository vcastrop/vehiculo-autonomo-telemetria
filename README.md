# Proyecto: Servidor TCP de Telemetría de Vehículo Autónomo

Este proyecto implementa un **servidor TCP en C** que simula la telemetría de un vehículo autónomo.  
Permite múltiples clientes concurrentes, manejo de roles (ADMIN y VIEWER), autenticación, comandos de control, transmisión periódica de datos y registro de eventos en consola y archivo de log.

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

### 1) Clonar el repositorio
git clone <URL_DEL_REPO>
cd <CARPETA_DEL_REPO>

### 2) Compilar
Opción A — GCC (MinGW)
gcc server.c -o server.exe -lws2_32
gcc -Wall -O2 -o server src/server.c -lws2_32

Opción B — Visual Studio (MSVC)

Compila server.c en un proyecto de consola. 

No hace falta configurar librerías adicionales.

Requisitos: Windows 7+; tener MinGW o Visual Studio instalados.

### 3) Iniciar el servidor
Sintaxis

server.exe <puerto> [archivo_log]

Ejemplos

Solo consola:

server.exe 8080


Consola + archivo de log:

server.exe 8080 log.txt


El servidor escucha en 0.0.0.0:<puerto> y acepta múltiples clientes concurrentes.

log.txt registra lo mismo que la consola (timestamp, IP:puerto, direcciones RX/TX y mensajes).

### 4) Conectar clientes

Puedes usar cualquier cliente TCP que envíe texto línea a línea.

Opción A — netcat (recomendado)

En PowerShell / Git Bash / MSYS2:

nc 127.0.0.1 8080

En Linux:

nc -v ip_windows_dispo port 9000

Opción B — telnet

Activa la característica “Cliente Telnet” en Windows (si no está).

telnet 127.0.0.1 8080

Opción C — otro programa TCP

Cualquier app que abra una conexión TCP y envíe líneas con \n funcionará.

Al conectarte, el servidor envía:

WELCOME TelemetryServer PROTO 1.0
ROLE VIEWER


El cliente empieza como VIEWER por defecto.

### 5) Telemetría automática

Cada ~10 s el servidor envía a todos los clientes una línea DATA:

DATA speed=XX.X battery=XX.X temp=XX.X heading=XXX.X ts=XXXXXXXXXXXX


Donde:

speed km/h (0–120),

battery % (baja 0.2 por ciclo),

temp °C (20–60),

heading grados normalizados (0–360),

ts timestamp en milisegundos.

## Informe del Proyecto

Puedes consultar el informe completo aquí:  
[Abrir Informe de Proyecto (PDF)](./informe.pdf)

---

## Autoría

**Proyecto académico:** Servidor de telemetría de vehículo autónomo
**Autores:** [jk, val, isa, j]
**Institución:** [Universidad EAFIT]
**Año:** 2025


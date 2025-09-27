# Proyecto de Servidor de Telemetría TCP en C

informe proyecto

https://docs.google.com/document/d/1ALgj_yQlZgh9OJZcsmzTHs214VE6zaSvpqmTf_rAIws/edit?usp=sharing

referencias útiles mientrass

- https://www.rfc-editor.org/rfc/rfc793.html
- https://man7.org/linux/man-pages/man7/socket.7.html
- https://man7.org/linux/man-pages/man7/socket.7.html
- https://stefanogassmann.github.io/BGNETSpanish/

Este proyecto implementa un **servidor de telemetría** en lenguaje C utilizando **sockets TCP (Winsock2 en Windows)** y **hilos (Win32 threads)**.  
El servidor simula un vehículo autónomo que transmite datos de **velocidad, batería, temperatura y rumbo** cada 10 segundos a todos los clientes conectados.

El sistema soporta múltiples clientes concurrentes, roles de **VIEWER** y **ADMIN**, autenticación, comandos de control remoto y logging de la actividad.

    Que hace este programa (resumen):
    ---------------------------------
    - Crea un servidor TCP (SOCK_STREAM) en el puerto dado por línea de comandos.
    - Acepta múltiples clientes concurrentes (un hilo por cliente).
    - Difunde cada 10 segundos una línea de telemetría a TODOS los clientes conectados:
          DATA speed=<x> battery=<y> temp=<z> heading=<w> ts=<ms>
    - Soporta un protocolo de texto simple con comandos:
          HELLO <name>
          AUTH <user> <pass>        # admin 1234 eleva el rol a ADMIN
          CMD <SPEED_UP|SLOW_DOWN|TURN_LEFT|TURN_RIGHT>   # solo ADMIN
          USERS                     # solo ADMIN, lista clientes
          BYE                       # cierre limpio
      Respuestas: OK, ERROR, ACK, NACK, WELCOME, ROLE ...

    - Roles: VIEWER (por defecto) y ADMIN (tras AUTH correcto).
      Si un VIEWER intenta mandar CMD → ERROR 403 not_admin

    - Logging: imprime a consola y opcionalmente a archivo (segundo argumento).
      Formato por línea: [TAG timestamp ip:puerto DIR] mensaje

    Cómo compilar (Windows con MinGW/MSYS2):
    ---------------------------------------
      gcc -Wall -O2 -o server src/server.c -lws2_32

    Cómo ejecutar:
    --------------
      ./server <puerto> [ruta_log]
      Ejemplo:
        ./server 9000 logs/server.log

    Cómo probar con netcat/ncat:
    ----------------------------
      ncat 127.0.0.1 9000
      (verás WELCOME y ROLE)
      AUTH admin 1234
      CMD SPEED_UP
      (cada 10 s verás DATA ... en cualquier cliente conectado)

    Notas técnicas:
    ---------------
    - Este código usa Win32/Winsock2 y CRITICAL_SECTION como mutex.
    - El servidor maneja líneas terminadas en \n o \r\n. Siempre envía con \r\n.
    - El hilo de telemetría recorre la lista enlazada de clientes (protegida por mutex)
      y envía DATA a todos los sockets conectados.


## Ejecutar el server en Windows

### 1) Requisitos (elige una opción)
- **Opción A – GCC (recomendado):** instalar **MSYS2/MinGW** o **Git Bash** con `gcc` en el PATH.
- **Opción B – MSVC:** usar **Developer Command Prompt for Visual Studio** (`cl` en el PATH).

> Si no quieres instalar nada extra, puedes compilar desde el **Developer Command Prompt** (Visual Studio Community es gratis).

### 2) Compilar y ejecutar (desde la carpeta `telemetria/`)
**Con GCC (MSYS2/MinGW/Git Bash):**
```bash
mkdir -p logs
gcc -Wall -O2 -D_WIN32_WINNT=0x0601 -o server src/server.c -lws2_32
./server 9000 logs/server.log
```
Con MSVC (Developer Command Prompt):

mkdir logs
cl /nologo /W3 /O2 /D_WIN32_WINNT=0x0601 src\server.c ws2_32.lib /Fe:server.exe
server.exe 9000 logs\server.log

Salida esperada al iniciar:

[INFO ... 0.0.0.0:9000 LISTEN] server_ready

## Probar como cliente en kali linux (o WSL)

### 1) Instalar netcat
sudo apt update
sudo apt install -y netcat-openbsd

### 2) conectarte al servidur usando la ipv4 de tu pc windows

nc -v <IP_DE_WINDOWS> 9000

## Probar como cliente en Windows (Git Bash o PowerShell)

### 1) en powershell

Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

### 2) cliente rapido  pega esto
$tcp=New-Object Net.Sockets.TcpClient('127.0.0.1',9000)
$ns=$tcp.GetStream();$r=New-Object IO.StreamReader($ns)
$w=New-Object IO.StreamWriter($ns);$w.NewLine="`n";$w.AutoFlush=$true
Start-Job {param($rr)while($true){$l=$rr.ReadLine();if($null-ne $l){Write-Host $l}else{break}}} -Arg $r | Out-Null
# Escribe tus comandos:
# HELLO Vale
# AUTH admin 1234
# CMD SPEED_UP
# USERS
# BYE




### Protocolo de aplicación

| Comando           | Descripción                                |
| ----------------- | ------------------------------------------ |
| `HELLO <name>`    | Identifica al cliente por nombre           |
| `AUTH admin 1234` | Autenticación para obtener rol ADMIN       |
| `CMD SPEED_UP`    | Incrementa la velocidad (solo ADMIN)       |
| `CMD SLOW_DOWN`   | Disminuye la velocidad (solo ADMIN)        |
| `CMD TURN_LEFT`   | Gira a la izquierda (solo ADMIN)           |
| `CMD TURN_RIGHT`  | Gira a la derecha (solo ADMIN)             |
| `USERS`           | Lista los clientes conectados (solo ADMIN) |
| `BYE`             | Cierra la conexión limpiamente             |

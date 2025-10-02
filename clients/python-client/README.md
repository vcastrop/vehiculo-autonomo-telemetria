# Cliente Python - Telemetría Vehículo Autónomo

Cliente de telemetría implementado en Python con interfaz gráfica usando Tkinter para el protocolo PROTO 1.0.

## Requisitos

- Python 3.6 o superior
- Tkinter (incluido por defecto en la mayoría de instalaciones de Python)

## Instalación

No requiere instalación de dependencias adicionales. Tkinter viene incluido con Python.

Para verificar que Tkinter está disponible:
```bash
python -c "import tkinter; print('Tkinter disponible')"
```

## Ejecución

### Uso básico:
```bash
python telemetry_client.py
```

### Especificar host y puerto:
```bash
python telemetry_client.py <host> <port>
```

### Ejemplos:
```bash
# Conectar a localhost en puerto 9000
python telemetry_client.py 127.0.0.1 9000

# Conectar a servidor remoto
python telemetry_client.py 192.168.1.100 9000
```

## Características

### Interfaz Gráfica
- **Panel de conexión**: Permite configurar host, puerto y conectar/desconectar
- **Panel de telemetría**: Muestra en tiempo real los datos del vehículo:
  - Velocidad (km/h)
  - Nivel de batería (%)
  - Temperatura (°C)
  - Rumbo (grados)
- **Panel de controles**: Botones para enviar comandos:
  - HELLO: Identificación del cliente
  - AUTH: Autenticación como administrador
  - Comandos rápidos: SPEED UP, SLOW DOWN, TURN LEFT, TURN RIGHT
  - USERS: Consultar usuarios conectados (solo ADMIN)
  - Campo libre para comandos personalizados
  - BYE: Cerrar conexión
- **Log de comunicación**: Muestra todos los mensajes enviados y recibidos

### Funcionalidades
- Conexión TCP al servidor de telemetría
- Recepción automática de datos de telemetría cada 10 segundos
- Envío de comandos de control (requiere rol ADMIN)
- Autenticación de administrador
- Consulta de usuarios conectados
- Manejo de errores y excepciones
- Interfaz intuitiva y fácil de usar

## Protocolo PROTO 1.0

### Comandos soportados:
- `HELLO <name>`: Identificarse con un nombre
- `AUTH <user> <password>`: Autenticarse como administrador
- `CMD <comando>`: Enviar comando de control (SPEED_UP, SLOW_DOWN, TURN_LEFT, TURN_RIGHT)
- `USERS`: Listar usuarios conectados (solo ADMIN)
- `BYE`: Cerrar conexión

### Mensajes del servidor:
- `WELCOME <server> PROTO 1.0`: Bienvenida al conectar
- `ROLE <ADMIN|VIEWER>`: Notificación de rol asignado
- `DATA speed=X battery=Y temp=Z heading=W ts=T`: Datos de telemetría
- `ACK <comando> accepted`: Comando aceptado
- `NACK <comando> reason=<razón>`: Comando rechazado
- `ERROR <código> <detalle>`: Error en el comando

## Roles

### VIEWER (por defecto)
- Recibe datos de telemetría
- Puede identificarse con HELLO
- No puede enviar comandos de control

### ADMIN (después de AUTH exitoso)
- Recibe datos de telemetría
- Puede enviar comandos de control (CMD)
- Puede consultar usuarios conectados (USERS)

## Estructura del código

```
telemetry_client.py
├── TelemetryClient (clase principal)
│   ├── init_ui(): Inicializa la interfaz gráfica
│   ├── connect(): Establece conexión con el servidor
│   ├── disconnect(): Cierra la conexión
│   ├── reader_loop(): Hilo que lee mensajes del servidor
│   ├── handle_line(): Procesa mensajes recibidos
│   ├── parse_data(): Extrae datos de telemetría
│   ├── send_raw(): Envía mensajes al servidor
│   └── Métodos de UI (do_hello, do_auth, etc.)
└── main(): Punto de entrada
```

## Ejemplos de uso

### 1. Conectar como observador (VIEWER)
1. Ejecutar el cliente
2. Clic en "Conectar"
3. Opcionalmente: Clic en "HELLO" e ingresar nombre
4. Observar datos de telemetría en tiempo real

### 2. Conectar como administrador (ADMIN)
1. Ejecutar el cliente
2. Clic en "Conectar"
3. Clic en "AUTH"
4. Ingresar usuario: `admin`, contraseña: `1234` (o las credenciales configuradas)
5. Ahora se pueden enviar comandos de control

### 3. Enviar comandos de control
1. Autenticarse como ADMIN
2. Usar los botones de comandos rápidos:
   - SPEED UP: Aumentar velocidad
   - SLOW DOWN: Reducir velocidad
   - TURN LEFT: Girar a la izquierda
   - TURN RIGHT: Girar a la derecha
3. O escribir comandos personalizados en el campo libre

### 4. Consultar usuarios conectados
1. Autenticarse como ADMIN
2. Clic en "USERS"
3. Ver la lista de usuarios en el log

## Características técnicas

- **Protocolo de transporte**: TCP (SOCK_STREAM)
- **Codificación**: UTF-8
- **Terminación de línea**: `\r\n`
- **Concurrencia**: Hilo separado para lectura de mensajes
- **Timeout de conexión**: 5 segundos
- **GUI Framework**: Tkinter (multiplataforma)

## Manejo de errores

El cliente maneja automáticamente:
- Conexiones rechazadas
- Timeouts
- Desconexiones inesperadas
- Mensajes malformados
- Errores del protocolo (ERROR 400, 401, 403)

Todos los errores se muestran en el log y en ventanas de diálogo cuando es necesario.

## Comparación con el cliente Java

Funcionalidades equivalentes:
-  Interfaz gráfica similar
-  Conexión TCP
-  Recepción de telemetría en tiempo real
-  Envío de todos los comandos del protocolo
-  Autenticación de administrador
-  Log de comunicación
-  Manejo de errores

Ventajas del cliente Python:
- Código más conciso y legible
- No requiere compilación
- Multiplataforma sin dependencias adicionales
- Diálogos nativos del sistema operativo

## Notas adicionales

- El cliente se desconecta automáticamente al cerrar la ventana
- Los datos de telemetría se actualizan automáticamente cuando llegan del servidor
- El log muestra marca de tiempo para cada mensaje
- Se puede cambiar el host y puerto sin reiniciar el cliente

## Solución de problemas

### Error: "No module named 'tkinter'"
En Linux, instalar:
```bash
sudo apt-get install python3-tk
```

### Error: "Connection refused"
- Verificar que el servidor esté corriendo
- Verificar que el puerto sea el correcto
- Verificar el firewall

### No se reciben datos de telemetría
- Verificar la conexión en el log
- Verificar que el servidor esté enviando datos cada 10 segundos
- Revisar el log del servidor

# Protocolo de Telemetría para Vehículo Autónomo — PROTO 1.0

## 1. Propósito y Alcance
Difundir telemetría cada 10 s a clientes y recibir comandos de control de un administrador autenticado (TCP, sockets Berkeley, servidor en C).

## 2. Transporte y Modelo
TCP (SOCK_STREAM) sobre IPv4/IPv6. Servidor central con múltiples clientes concurrentes (1 hilo/cliente + 1 hilo difusor).

## 3. Modelo y Roles
- Modelo: cliente–servidor sobre TCP.
- Roles: VIEWER (solo recibe), ADMIN (comandos y consultas).

## 4. Telemetría (DATA)
Formato: DATA speed=<kmh> battery=<pct> temp=<celsius> heading=<deg> ts=<ms_epoch>

Frecuencia: cada 10 s. 

Unidades: km/h, %, °C, grados (0–359), epoch ms.

## 5. Operaciones
- HELLO <name>
- AUTH <user> <pass>
- CMD <SPEED_UP|SLOW_DOWN|TURN_LEFT|TURN_RIGHT>
- USERS
- BYE
- DATA ... (servidor → clientes, cada 10 s)

## 4. Mensajes (sintaxis y ejemplos)
### Identidad y rol
**HELLO**

  C → S: HELLO <name>

  S → C:

    WELCOME <server_name> PROTO 1.0

    ROLE <ADMIN|VIEWER> (VIEWER por defecto)

    OK hello <name> (confirmación)

Errores: ERROR 400 missing_name

**AUTH**

  C → S: AUTH <user> <pass>

  S → C:

      ROLE ADMIN + OK auth (si credenciales correctas)

Errores: ERROR 401 invalid_credentials (si fallan)

**Nota: el rol ADMIN queda asociado a la sesión (no a la IP), cumpliendo el requisito de identificación incluso si el admin cambia de IP/cliente.*

### Telemetría (difusión)
**DATA** (enviado cada 10 s por el servidor a todos)

  S → C: DATA speed=<kmh> battery=<pct> temp=<celsius> heading=<deg> ts=<ms_epoch>

    Ej.: DATA speed=52.3 battery=89 temp=36.2 heading=175 ts=1737582012345

### Comandos (solo ADMIN)
**CMD**

  C (ADMIN) → S: CMD <SPEED_UP|SLOW_DOWN|TURN_LEFT|TURN_RIGHT>

  S → C:

    ACK <CMD> accepted

    NACK <CMD> reason=<LOW_BATTERY|SPEED_LIMIT|INVALID_STATE|UNSUPPORTED>

Errores: ERROR 400 invalid_cmd, ERROR 403 not_admin (si VIEWER intenta CMD)

### Consulta de usuarios (solo ADMIN)
**USERS**

  C (ADMIN) → S: USERS

  S → C:

    USERS count=<n>

    USER <i> name=<name> ip=<a.b.c.d> port=<p> role=<ADMIN|VIEWER> (una por usuario)

    OK users

Errores: ERROR 403 not_admin

###Terminación
**BYE**

  C → S: BYE

  S → C: 
  
    OK bye y cierra.

## 5. Reglas de Procedimiento
* Conexión TCP (3-way handshake) → servidor envía WELCOME y ROLE VIEWER.
* Identificación (opcional): cliente envía HELLO.
* Autenticación (opcional): AUTH para elevar a ADMIN.
* Difusión periódica: servidor envía DATA a todas las sesiones cada 10 s.
* Control: ADMIN envía CMD y recibe ACK/NACK.
* Monitoreo: ADMIN puede consultar USERS.
* Cierre: BYE o desconexión; el servidor quita al cliente de la lista.

## 6. Errores
- 400 (missing_name, invalid_cmd, too_long, unknown_cmd)
- 401 (invalid_credentials)
- 403 (not_admin)
- 501 (not_implemented)

## 7. Matriz de Pruebas
[tabla T1–T10]

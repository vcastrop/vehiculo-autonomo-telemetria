# PROTO 1.0 — Protocolo de Telemetría para Vehículo Autónomo

## 1. Transporte y Modelo
TCP (SOCK_STREAM) sobre IPv4/IPv6. Servidor central con múltiples clientes concurrentes (1 hilo/cliente + 1 hilo difusor).

## 2. Roles
- VIEWER: recibe DATA cada 10 s.
- ADMIN: AUTH exitoso → puede CMD y USERS. Rol asociado a la sesión.

## 3. Telemetría (DATA)
Formato: DATA speed=<kmh> battery=<pct> temp=<celsius> heading=<deg> ts=<ms_epoch>

Frecuencia: cada 10 s. 

Unidades: km/h, %, °C, grados (0–359), epoch ms.

## 4. Operaciones
HELLO <name> → WELCOME, ROLE VIEWER, OK hello <name> | ERROR 400 missing_name

AUTH <user> <pass> → ROLE ADMIN, OK auth | ERROR 401 invalid_credentials

CMD <SPEED_UP|SLOW_DOWN|TURN_LEFT|TURN_RIGHT> → ACK/NACK | ERROR 403 not_admin | ERROR 400 invalid_cmd

USERS → USERS count=n + USER… + OK users | ERROR 403 not_admin

BYE → OK bye y cierre

DATA … (servidor → clientes)

## 5. Reglas de Procedimiento
* Conexión TCP (3-way handshake) → servidor envía WELCOME y ROLE VIEWER.
* Identificación (opcional): cliente envía HELLO.
* Autenticación (opcional): AUTH para elevar a ADMIN.
* Difusión periódica: servidor envía DATA a todas las sesiones cada 10 s.
* Control: ADMIN envía CMD y recibe ACK/NACK.
* Monitoreo: ADMIN puede consultar USERS.
* Cierre: BYE o desconexión; el servidor quita al cliente de la lista.

## 6. Errores
400 (missing_name, invalid_cmd, too_long, unknown_cmd)
401 (invalid_credentials)
403 (not_admin)
501 (not_implemented)

## 7. Matriz de Pruebas
[tabla T1–T10]

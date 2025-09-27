// src/server.c
// Servidor TCP de telemetría con roles, AUTH, comandos y logging (Windows/Winsock2)
// Comentado "en estilo estudiante": cada parte explica qué hace y por qué.

// --- Plataforma mínima de Windows (opcional, 0x0601 = Windows 7 o superior)
#define _WIN32_WINNT 0x0601

// --- Cabeceras de Windows para sockets y utilidades
#include <winsock2.h>     // socket(), bind(), listen(), accept(), send(), recv()
#include <ws2tcpip.h>     // inet_ntop / InetNtopA, estructuras extendidas
#include <windows.h>      // CreateThread, HANDLE, Sleep, CRITICAL_SECTION

// --- Cabeceras estándar de C
#include <stdio.h>        // printf, fprintf
#include <stdlib.h>       // atoi, malloc, free, rand, srand
#include <string.h>       // memset, memcpy, strcmp, strncpy
#include <time.h>         // time() para rand()

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif


/* =============================== Configuración general =============================== */

// Tamaños y constantes del protocolo
#define MAX_LINE            1024           // tamaño máximo de una línea de texto
#define BACKLOG             16             // cola de listen()
#define TELEMETRY_PERIOD_MS (10*1000)      // periodo de telemetría en milisegundos
#define PROTO_VERSION       "1.0"          // versión textual del protocolo

// Roles de cliente
typedef enum {
    ROLE_VIEWER = 0,       // rol por defecto: solo ve telemetría
    ROLE_ADMIN  = 1        // rol tras AUTH admin 1234: puede enviar CMD y USERS
} role_t;

// Estructura de cliente conectado (lista enlazada)
typedef struct client_s {
    SOCKET             sock;               // socket del cliente
    char               ip[64];             // ip en texto (ej: "127.0.0.1")
    int                port;               // puerto remoto
    role_t             role;               // rol actual (VIEWER/ADMIN)
    char               name[64];           // nombre opcional (HELLO <name>)
    int                alive;              // flag vivo (1=activo)
    struct client_s   *next;               // siguiente en la lista
} client_t;

// Estado simulado del vehículo (para DATA ...)
typedef struct {
    double speed_kmh;       // velocidad en km/h
    double battery_pc;      // batería en %
    double temp_c;          // temperatura en °C
    double heading_deg;     // rumbo en grados [0,360)
} vehicle_state_t;

/* =============================== Globales =============================== */

// Lista de clientes conectados + mutex
static client_t *clients = NULL;           // cabeza de la lista enlazada
static CRITICAL_SECTION cs;                // sección crítica para proteger la lista

// Estado global del vehículo + mutex propio (podríamos reutilizar cs, pero mejor separado)
static vehicle_state_t vstate = { 50.0, 100.0, 35.0, 90.0 };
static CRITICAL_SECTION cs_state;

// Bandera global de ejecución del servidor
static volatile int running = 1;

// Handler del hilo de telemetría (para esperarlo al cerrar)
static HANDLE hTel = NULL;

// Soporte Winsock
static WSADATA wsa;

// --- Logging a archivo opcional + consola (FASE 7)
static FILE *g_logfp = NULL;               // si argv[2] trae ruta, escribimos aquí también

/* =============================== Utilidades =============================== */

// Tiempo actual en milisegundos desde época Unix (aproximado en Windows)
static long long now_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);                 // 100-ns desde 1601
    ULARGE_INTEGER uli;                           // lo convertimos a entero
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    unsigned long long ms_1601 = uli.QuadPart / 10000ULL;     // 100-ns -> ms
    const unsigned long long EPOCH_DIFF = 11644473600000ULL;  // ms entre 1601 y 1970
    if (ms_1601 < EPOCH_DIFF) return 0;
    return (long long)(ms_1601 - EPOCH_DIFF);     // ms desde 1970
}

// Quita \r y \n del final de un string (para limpiar líneas)
static void rstrip_newline(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[n-1] = '\0';
        n--;
    }
}

// Recorta espacios al inicio y fin
static void trim(char *s) {
    if (!s) return;
    // recorta inicio
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    // recorta final
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) { s[n-1]='\0'; n--; }
}

// Envía una línea agregando \n (para netcat) o \r\n (si prefieres). Aquí uso \n.
static int send_line(SOCKET s, const char *line) {
    char buf[MAX_LINE + 4];
    int n = _snprintf(buf, sizeof(buf), "%s\n", line ? line : "");
    if (n < 0) return -1;
    int sent = 0;
    while (sent < n) {
        int r = send(s, buf + sent, n - sent, 0);
        if (r == SOCKET_ERROR) return -1;
        sent += r;
    }
    return 0;
}

// LOG unificado a consola y, si corresponde, a archivo (FASE 7)
static void log_line(const char *tag, const char *ip, int port, const char *dir, const char *msg) {
    long long ts = now_ms();                    // timestamp en ms
    if (!tag) tag = "LOG";
    if (!ip)  ip  = "-";
    if (!dir) dir = "-";
    if (!msg) msg = "";
    // sale por consola
    fprintf(stdout, "[%s %lld %s:%d %s] %s\n", tag, ts, ip, port, dir, msg);
    fflush(stdout);
    // y opcionalmente a archivo
    if (g_logfp) {
        fprintf(g_logfp, "[%s %lld %s:%d %s] %s\n", tag, ts, ip, port, dir, msg);
        fflush(g_logfp);
    }
}

/* =============================== Lista de clientes =============================== */

// Inserta cliente al inicio (O(1))
static void add_client(client_t *c) {
    EnterCriticalSection(&cs);        // bloqueo de lista
    c->next = clients;                // enlazo al principio
    clients = c;                      // muevo cabeza
    LeaveCriticalSection(&cs);        // libero
}

// Elimina por socket (cuando falla hilo o desconecta)
static void remove_client_by_sock(SOCKET s) {
    EnterCriticalSection(&cs);
    client_t **pp = &clients;
    while (*pp) {
        if ((*pp)->sock == s) {
            client_t *dead = *pp;
            *pp = (*pp)->next;        // saco de la lista
            LeaveCriticalSection(&cs);
            closesocket(dead->sock);  // cierro socket
            free(dead);               // libero memoria
            return;
        }
        pp = &((*pp)->next);          // avanzo
    }
    LeaveCriticalSection(&cs);
}

// Recorre todos los clientes y envía una línea (DATA o broadcast). Agrega logs TX por cliente.
static void broadcast_line(const char *line) {
    EnterCriticalSection(&cs);
    for (client_t *c = clients; c; c = c->next) {
        if (c->alive && c->sock != INVALID_SOCKET) {
            int r = send_line(c->sock, line);              // envío a este cliente
            if (r == 0) {
                char dbg[200]; _snprintf(dbg, sizeof(dbg), "%.180s", line);
                log_line("TX", c->ip, c->port, "TX", dbg); // LOG TX por cliente (FASE 7)
            }
        }
    }
    LeaveCriticalSection(&cs);
}

/* =============================== Telemetría periódica =============================== */

// Hilo que cada 10s simula y difunde "DATA speed=... battery=... temp=... heading=... ts=..."
static DWORD WINAPI telemetry_thread(LPVOID param) {
    (void)param; // no lo usamos
    while (running) {
        Sleep(TELEMETRY_PERIOD_MS);  // esperamos el periodo

        // Modificamos el estado un poquito para que cambie en el tiempo
        EnterCriticalSection(&cs_state);
        vstate.speed_kmh   += ((rand()%11) - 5) * 0.5;  // -2.5..+2.5
        if (vstate.speed_kmh < 0)   vstate.speed_kmh = 0;
        if (vstate.speed_kmh > 120) vstate.speed_kmh = 120;

        vstate.battery_pc  -= 0.2;                      // baja despacio
        if (vstate.battery_pc < 0) vstate.battery_pc = 0;

        vstate.temp_c      += ((rand()%7) - 3) * 0.2;   // -0.6..+0.6
        if (vstate.temp_c < 20) vstate.temp_c = 20;
        if (vstate.temp_c > 60) vstate.temp_c = 60;

        vstate.heading_deg += ((rand()%21) - 10);       // -10..+10
        while (vstate.heading_deg < 0)    vstate.heading_deg += 360.0;
        while (vstate.heading_deg >= 360) vstate.heading_deg -= 360.0;

        // Tomamos una foto del estado y liberamos el candado
        double spd = vstate.speed_kmh;
        double bat = vstate.battery_pc;
        double tmp = vstate.temp_c;
        double hdg = vstate.heading_deg;
        LeaveCriticalSection(&cs_state);

        // Construimos la línea DATA con timestamp en ms
        long long ts = now_ms();
        char line[256];
        _snprintf(line, sizeof(line),
            "DATA speed=%.1f battery=%.1f temp=%.1f heading=%.1f ts=%lld",
            spd, bat, tmp, hdg, ts);

        // Difundimos a todos los clientes conectados
        broadcast_line(line);
    }
    return 0;
}

/* =============================== Lógica de comandos por cliente =============================== */

// Ejecuta un CMD ... (solo ADMIN). Envía ACK/NACK y deja logs TX.
static void handle_cmd(client_t *cli, const char *rest) {
    if (!rest || !*rest) {                                // si no vino argumento
        send_line(cli->sock, "ERROR 400 invalid_cmd");    // error genérico
        log_line("TX", cli->ip, cli->port, "TX", "ERROR 400 invalid_cmd");
        return;
    }
    if (cli->role != ROLE_ADMIN) {                        // si no es admin, prohibido
        send_line(cli->sock, "ERROR 403 not_admin");
        log_line("TX", cli->ip, cli->port, "TX", "ERROR 403 not_admin");
        return;
    }

    // Copiamos el comando y lo normalizamos
    char cmd[64]; _snprintf(cmd, sizeof(cmd), "%.63s", rest);
    trim(cmd);
    for (char *p = cmd; *p; ++p) *p = (char)toupper(*p);

    // Si la batería está <10%, rechazamos o
    EnterCriticalSection(&cs_state);
    double bat = vstate.battery_pc;
    LeaveCriticalSection(&cs_state);
    if (bat < 10.0) {
        send_line(cli->sock, "NACK low_battery");
        log_line("TX", cli->ip, cli->port, "TX", "NACK low_battery");
        return;
    }

    // Aplicamos cambios al estado según el comando
    int ok = 1;
    EnterCriticalSection(&cs_state);
    if      (strcmp(cmd, "SPEED_UP")   == 0) vstate.speed_kmh   += 5.0;
    else if (strcmp(cmd, "SLOW_DOWN")  == 0) vstate.speed_kmh   -= 5.0;
    else if (strcmp(cmd, "TURN_LEFT")  == 0) vstate.heading_deg -= 15.0;
    else if (strcmp(cmd, "TURN_RIGHT") == 0) vstate.heading_deg += 15.0;
    else ok = 0;

    if (vstate.speed_kmh < 0)   vstate.speed_kmh = 0;
    if (vstate.speed_kmh > 150) vstate.speed_kmh = 150;
    while (vstate.heading_deg < 0)    vstate.heading_deg += 360.0;
    while (vstate.heading_deg >= 360) vstate.heading_deg -= 360.0;
    LeaveCriticalSection(&cs_state);

    if (ok) {
        char out[64]; _snprintf(out, sizeof(out), "ACK %s accepted", cmd);
        send_line(cli->sock, out);
        log_line("TX", cli->ip, cli->port, "TX", out);
    } else {
        send_line(cli->sock, "NACK unknown_cmd");
        log_line("TX", cli->ip, cli->port, "TX", "NACK unknown_cmd");
    }
}

// Envía lista de usuarios (solo ADMIN)
static void handle_users(client_t *cli) {
    if (cli->role != ROLE_ADMIN) {
        send_line(cli->sock, "ERROR 403 not_admin");
        log_line("TX", cli->ip, cli->port, "TX", "ERROR 403 not_admin");
        return;
    }
    EnterCriticalSection(&cs);
    int idx = 0;
    for (client_t *c = clients; c; c = c->next) {
        char line[256];
        _snprintf(line, sizeof(line),
            "USER %d ip=%s port=%d role=%s name=%s",
            idx++, c->ip, c->port, (c->role==ROLE_ADMIN?"ADMIN":"VIEWER"),
            (c->name[0]?c->name:"-"));
        send_line(cli->sock, line);
        log_line("TX", cli->ip, cli->port, "TX", line);
    }
    LeaveCriticalSection(&cs);
}

/* =============================== Hilo por cliente =============================== */

// Recibe del socket en bloques y retorna líneas completas (acumulando hasta '\n').
// Devuelve:
//   >0 = longitud de línea (out ya sin \r\n)
//    0 = peer cerró
//   -1 = error
static int recv_line_accum(SOCKET s, char *out, int outsz) {
    // buffers thread-local para cada hilo de cliente
    static __thread char acc[2048];
    static __thread int  acc_len = 0;

    // buscamos '\n' en lo acumulado
    for (;;) {
        for (int i = 0; i < acc_len; ++i) {
            if (acc[i] == '\n') {
                int len = i;
                if (len >= outsz) len = outsz - 1;
                memcpy(out, acc, len);
                out[len] = '\0';
                // corremos el resto hacia adelante
                int rest = acc_len - (i + 1);
                memmove(acc, acc + i + 1, rest);
                acc_len = rest;
                rstrip_newline(out);
                return len;
            }
        }
        // si no había '\n', leemos más del socket
        int r = recv(s, acc + acc_len, (int)sizeof(acc) - acc_len, 0);
        if (r == 0) return 0;                 // peer cerró
        if (r == SOCKET_ERROR) return -1;     // error
        acc_len += r;
        if (acc_len >= (int)sizeof(acc) - 1) { // línea larguísima: devolvemos lo que haya
            int len = (outsz - 1 < acc_len) ? (outsz - 1) : acc_len;
            memcpy(out, acc, len);
            out[len] = '\0';
            acc_len = 0;
            rstrip_newline(out);
            return len;
        }
    }
}

// Hilo dedicado a un cliente: parsea líneas y responde
static DWORD WINAPI client_thread(LPVOID param) {
    client_t *cli = (client_t*)param;          // el cliente asociado a este hilo
    SOCKET s = cli->sock;                      // socket del cliente

    // --- Envío de bienvenida inicial (Fase 2)
    {
        char banner[64];
        _snprintf(banner, sizeof(banner), "WELCOME TelemetryServer PROTO %s", PROTO_VERSION);
        send_line(s, banner);
        log_line("TX", cli->ip, cli->port, "TX", banner);   // LOG TX de banner
        // rol inicial
        send_line(s, "ROLE VIEWER");
        log_line("TX", cli->ip, cli->port, "TX", "ROLE VIEWER");
    }

    // --- Bucle principal: leer líneas y actuar
    char line[MAX_LINE];
    for (;;) {
        int r = recv_line_accum(s, line, sizeof(line));   // recibimos una línea
        if (r == 0) {                                     // peer cerró
            log_line("INFO", cli->ip, cli->port, "CLOSE", "peer_closed");
            break;
        } else if (r < 0) {                               // error de socket
            log_line("ERROR", cli->ip, cli->port, "RX", "recv_error");
            break;
        }

        // LOG RX de lo que llegó
        log_line("RX", cli->ip, cli->port, "RX", line);

        // limpiamos espacios
        trim(line);
        if (!*line) continue; // línea vacía → ignoramos

        // separamos comando y resto
        char *p = line;
        char *cmd = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        char *rest = NULL;
        if (*p) { *p = '\0'; rest = p + 1; } else { rest = p; }
        if (rest) trim(rest);
        for (char *q = cmd; *q; ++q) *q = (char)toupper(*q); // comando a MAYUS

        // --- Conmutador por comando
        if (strcmp(cmd, "HELLO") == 0) {
            // guardamos nombre si vino
            if (rest && *rest) {
                _snprintf(cli->name, sizeof(cli->name), "%.63s", rest);
                trim(cli->name);
            }
            char out[128];
            _snprintf(out, sizeof(out), "OK hello %s", (cli->name[0]?cli->name:"client"));
            send_line(s, out);
            log_line("TX", cli->ip, cli->port, "TX", out);
        }
        else if (strcmp(cmd, "AUTH") == 0) {
            // esperamos AUTH user pass  (para pruebas: admin 1234)
            char user[64] = {0}, pass[64] = {0};
            if (rest && *rest) {
                _snprintf(user, sizeof(user), "%.63s", rest);
                // separar user y pass
                char *sp = user;
                while (*sp && *sp != ' ' && *sp != '\t') sp++;
                if (*sp) { *sp = '\0'; _snprintf(pass, sizeof(pass), "%.63s", sp + 1); trim(pass); }
                trim(user);
            }
            if (_stricmp(user, "admin") == 0 && _stricmp(pass, "1234") == 0) {
                cli->role = ROLE_ADMIN;
                send_line(s, "ROLE ADMIN");
                log_line("TX", cli->ip, cli->port, "TX", "ROLE ADMIN");
                send_line(s, "OK auth");
                log_line("TX", cli->ip, cli->port, "TX", "OK auth");
            } else {
                send_line(s, "ERROR 401 invalid_credentials");
                log_line("TX", cli->ip, cli->port, "TX", "ERROR 401 invalid_credentials");
            }
        }
        else if (strcmp(cmd, "CMD") == 0) {
            handle_cmd(cli, rest);
        }
        else if (strcmp(cmd, "USERS") == 0) {
            handle_users(cli);
        }
        else if (strcmp(cmd, "BYE") == 0) {
            send_line(s, "OK bye");
            log_line("TX", cli->ip, cli->port, "TX", "OK bye");
            log_line("INFO", cli->ip, cli->port, "CLOSE", "peer_closed");
            break;
        }
        else {
            send_line(s, "ERROR 400 unknown_command");
            log_line("TX", cli->ip, cli->port, "TX", "ERROR 400 unknown_command");
        }
    }

    // Al salir, marcamos muerto y lo removemos de la lista
    cli->alive = 0;
    remove_client_by_sock(s);  // cierra y libera
    return 0;
}

/* =============================== main(): arranque del servidor =============================== */

int main(int argc, char **argv) {
    // --- Validamos argumentos
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <port> [logfile]\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Puerto invalido: %s\n", argv[1]);
        return 1;
    }

    // --- Abrimos el archivo de log si nos lo pasan (FASE 7)
    if (argc >= 3) {
        g_logfp = fopen(argv[2], "a");
        if (!g_logfp) {
            fprintf(stderr, "No pude abrir log '%s' (continuo sin archivo)\n", argv[2]);
        }
    }

    // --- Inicializamos Winsock2
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup fallo\n");
        return 1;
    }

    // --- Inicializamos mutex de lista y estado
    InitializeCriticalSection(&cs);
    InitializeCriticalSection(&cs_state);

    // --- Semilla para valores pseudoaleatorios de telemetría
    srand((unsigned)time(NULL));

    // --- Creamos socket servidor
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) {
        fprintf(stderr,"socket err %d\n", WSAGetLastError());
        return 1;
    }

    // --- Reutilización de puerto (no obligatorio, pero útil)
    BOOL opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // --- Dirección de escucha 0.0.0.0:port
    struct sockaddr_in addr; ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    // --- bind + listen
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr,"bind err %d\n", WSAGetLastError());
        return 1;
    }
    if (listen(srv, BACKLOG) == SOCKET_ERROR) {
        fprintf(stderr,"listen err %d\n", WSAGetLastError());
        return 1;
    }

    // --- LOG: servidor listo (FASE 7)
    log_line("INFO", "0.0.0.0", port, "LISTEN", "server_ready");

    // --- Lanzamos hilo de telemetría (FASE 3)
    hTel = CreateThread(NULL, 0, telemetry_thread, NULL, 0, NULL);
    if (!hTel) {
        fprintf(stderr,"CreateThread telemetry failed\n");
    }

    // --- Mensajes informativos
    printf("[INFO] Servidor escuchando en puerto %d (TCP)\n", port);
    printf("[INFO] Esperando conexiones...\n");

    // --- Bucle de aceptar clientes
    while (running) {
        struct sockaddr_in cli; int clen = sizeof(cli);
        SOCKET cfd = accept(srv, (struct sockaddr*)&cli, &clen);
        if (cfd == INVALID_SOCKET) {
            int e = WSAGetLastError();
            if (e == WSAEINTR) continue;           // interrupción
            fprintf(stderr,"accept err %d\n", e);
            break;                                  // salimos del while si es error grave
        }
        // Creamos objeto cliente y rellenamos datos
        client_t *cliobj = (client_t*)calloc(1, sizeof(client_t));
        cliobj->sock = cfd;
        InetNtopA(AF_INET, &cli.sin_addr, cliobj->ip, sizeof(cliobj->ip));
        cliobj->port = ntohs(cli.sin_port);
        cliobj->role = ROLE_VIEWER;                 // por defecto VIEWER
        strncpy(cliobj->name, "anon", sizeof(cliobj->name)-1);
        cliobj->alive = 1;

        // Añadimos a la lista global
        add_client(cliobj);

        // LOG de aceptación de cliente (FASE 7)
        log_line("INFO", cliobj->ip, cliobj->port, "ACCEPT", "connected");

        // Lanzamos hilo de cliente
        HANDLE h = CreateThread(NULL, 0, client_thread, cliobj, 0, NULL);
        if (!h) {
            // Si falla, lo eliminamos de la lista y cerramos su socket
            fprintf(stderr,"CreateThread client failed\n");
            remove_client_by_sock(cfd);
            closesocket(cfd);                       // cierre explícito (FASE 7 mejora)
        } else {
            CloseHandle(h);                         // no esperamos al hilo (no join)
        }
    }

    // --- Señalizamos parada y esperamos al hilo de telemetría (cierre limpio)
    running = 0;
    if (hTel) {
        WaitForSingleObject(hTel, 2000);           // esperamos hasta 2s
        CloseHandle(hTel);
        hTel = NULL;
    }

    // --- LOG de shutdown (FASE 7)
    log_line("INFO", "0.0.0.0", port, "SHUTDOWN", "stopping");

    // --- Cerramos socket servidor
    closesocket(srv);

    // --- Limpiamos todos los clientes que quedaran (por seguridad)
    EnterCriticalSection(&cs);
    client_t *it = clients;
    while (it) {
        client_t *next = it->next;
        if (it->sock != INVALID_SOCKET) closesocket(it->sock);
        free(it);
        it = next;
    }
    clients = NULL;
    LeaveCriticalSection(&cs);

    // --- Destruimos mutex
    DeleteCriticalSection(&cs_state);
    DeleteCriticalSection(&cs);

    // --- Cerramos archivo de log si estaba abierto
    if (g_logfp) { fclose(g_logfp); g_logfp = NULL; }

    // --- Cerramos Winsock
    WSACleanup();
    return 0;
}

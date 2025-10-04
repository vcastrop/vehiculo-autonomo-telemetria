// --- Plataforma mínima de Windows (opcional, 0x0601 = Windows 7 o superior)
#define _WIN32_WINNT 0x0601

// --- Cabeceras de Windows para sockets y utilidades
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// --- Cabeceras estándar de C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif

/* =============================== Configuración general =============================== */

#define MAX_LINE            1024
#define BACKLOG             16
#define TELEMETRY_PERIOD_MS (10*1000)
#define PROTO_VERSION       "1.0"

typedef enum {
    ROLE_VIEWER = 0,
    ROLE_ADMIN  = 1
} role_t;

typedef struct client_s {
    SOCKET             sock;
    char               ip[64];
    int                port;
    role_t             role;
    char               name[64];
    int                alive;
    struct client_s   *next;
} client_t;

typedef struct {
    double speed_kmh;
    double battery_pc;
    double temp_c;
    double heading_deg;
} vehicle_state_t;

/* =============================== Globales =============================== */

static client_t *clients = NULL;
static CRITICAL_SECTION cs;

static vehicle_state_t vstate = { 50.0, 100.0, 35.0, 90.0 };
static CRITICAL_SECTION cs_state;

static volatile int running = 1;

static HANDLE hTel = NULL;

static WSADATA wsa;

// --- Logging a archivo opcional + consola (FASE 7)
static FILE *g_logfp = NULL;

/* =============================== Utilidades =============================== */

static long long now_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    unsigned long long ms_1601 = uli.QuadPart / 10000ULL;
    const unsigned long long EPOCH_DIFF = 11644473600000ULL;
    if (ms_1601 < EPOCH_DIFF) return 0;
    return (long long)(ms_1601 - EPOCH_DIFF);
}

static void rstrip_newline(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[n-1] = '\0';
        n--;
    }
}

static void trim(char *s) {
    if (!s) return;

    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) { s[n-1]='\0'; n--; }
}

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

static void log_line(const char *tag, const char *ip, int port, const char *dir, const char *msg) {
    long long ts = now_ms();
    if (!tag) tag = "LOG";
    if (!ip)  ip  = "-";
    if (!dir) dir = "-";
    if (!msg) msg = "";

    fprintf(stdout, "[%s %lld %s:%d %s] %s\n", tag, ts, ip, port, dir, msg);
    fflush(stdout);

    if (g_logfp) {
        fprintf(g_logfp, "[%s %lld %s:%d %s] %s\n", tag, ts, ip, port, dir, msg);
        fflush(g_logfp);
    }
}

/* =============================== Lista de clientes =============================== */

static void add_client(client_t *c) {
    EnterCriticalSection(&cs);
    c->next = clients;
    clients = c;
    LeaveCriticalSection(&cs);
}

static void remove_client_by_sock(SOCKET s) {
    EnterCriticalSection(&cs);
    client_t **pp = &clients;
    while (*pp) {
        if ((*pp)->sock == s) {
            client_t *dead = *pp;
            *pp = (*pp)->next;
            LeaveCriticalSection(&cs);
            closesocket(dead->sock);
            free(dead);
            return;
        }
        pp = &((*pp)->next);
    }
    LeaveCriticalSection(&cs);
}

static void broadcast_line(const char *line) {
    EnterCriticalSection(&cs);
    for (client_t *c = clients; c; c = c->next) {
        if (c->alive && c->sock != INVALID_SOCKET) {
            int r = send_line(c->sock, line);
            if (r == 0) {
                char dbg[200]; _snprintf(dbg, sizeof(dbg), "%.180s", line);
                log_line("TX", c->ip, c->port, "TX", dbg);
            }
        }
    }
    LeaveCriticalSection(&cs);
}

/* =============================== Telemetría periódica =============================== */

static DWORD WINAPI telemetry_thread(LPVOID param) {
    (void)param;
    while (running) {
        Sleep(TELEMETRY_PERIOD_MS);

        EnterCriticalSection(&cs_state);
        vstate.speed_kmh   += ((rand()%11) - 5) * 0.5;
        if (vstate.speed_kmh < 0)   vstate.speed_kmh = 0;
        if (vstate.speed_kmh > 120) vstate.speed_kmh = 120;

        vstate.battery_pc  -= 0.2;
        if (vstate.battery_pc < 0) vstate.battery_pc = 0;

        vstate.temp_c      += ((rand()%7) - 3) * 0.2;
        if (vstate.temp_c < 20) vstate.temp_c = 20;
        if (vstate.temp_c > 60) vstate.temp_c = 60;

        vstate.heading_deg += ((rand()%21) - 10);
        while (vstate.heading_deg < 0)    vstate.heading_deg += 360.0;
        while (vstate.heading_deg >= 360) vstate.heading_deg -= 360.0;

        double spd = vstate.speed_kmh;
        double bat = vstate.battery_pc;
        double tmp = vstate.temp_c;
        double hdg = vstate.heading_deg;
        LeaveCriticalSection(&cs_state);

        long long ts = now_ms();
        char line[256];
        _snprintf(line, sizeof(line),
            "DATA speed=%.1f battery=%.1f temp=%.1f heading=%.1f ts=%lld",
            spd, bat, tmp, hdg, ts);

        broadcast_line(line);
    }
    return 0;
}

/* =============================== Lógica de comandos por cliente =============================== */

static void handle_cmd(client_t *cli, const char *rest) {
    if (!rest || !*rest) {
        send_line(cli->sock, "ERROR 400 invalid_cmd");
        log_line("TX", cli->ip, cli->port, "TX", "ERROR 400 invalid_cmd");
        return;
    }
    if (cli->role != ROLE_ADMIN) {
        send_line(cli->sock, "ERROR 403 not_admin");
        log_line("TX", cli->ip, cli->port, "TX", "ERROR 403 not_admin");
        return;
    }

    char cmd[64]; _snprintf(cmd, sizeof(cmd), "%.63s", rest);
    trim(cmd);
    for (char *p = cmd; *p; ++p) *p = (char)toupper(*p);

    EnterCriticalSection(&cs_state);
    double bat = vstate.battery_pc;
    LeaveCriticalSection(&cs_state);
    if (bat < 10.0) {
        send_line(cli->sock, "NACK low_battery");
        log_line("TX", cli->ip, cli->port, "TX", "NACK low_battery");
        return;
    }

    int ok = 1;
    EnterCriticalSection(&cs_state);
    if(strcmp(cmd, "SPEED_UP")    == 0) {
        if (vstate.speed_kmh + 5.0 > 120.0) {
            LeaveCriticalSection(&cs_state);
            send_line(cli->sock, "NACK speed_limit");
            log_line("TX", cli->ip, cli->port, "TX", "NACK speed_limit");
            return;
        }
        vstate.speed_kmh += 5.0;
    }
    else if (strcmp(cmd, "SLOW_DOWN") == 0) {
        double new_speed = vstate.speed_kmh - 5.0;
        if (new_speed < 0.0) {
            LeaveCriticalSection(&cs_state);
            send_line(cli->sock, "NACK min_speed");
            log_line("TX", cli->ip, cli->port, "TX", "NACK min_speed");
            return;
        }
        vstate.speed_kmh = new_speed;
    }
    else if (strcmp(cmd, "TURN_LEFT")  == 0) vstate.heading_deg -= 15.0;
    else if (strcmp(cmd, "TURN_RIGHT") == 0) vstate.heading_deg += 15.0;
    else ok = 0;

    if (vstate.speed_kmh < 0)   vstate.speed_kmh = 0;
    if (vstate.speed_kmh > 120) vstate.speed_kmh = 120;
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

static int recv_line_accum(SOCKET s, char *out, int outsz) {

    static __thread char acc[2048];
    static __thread int  acc_len = 0;

    for (;;) {
        for (int i = 0; i < acc_len; ++i) {
            if (acc[i] == '\n') {
                int len = i;
                if (len >= outsz) len = outsz - 1;
                memcpy(out, acc, len);
                out[len] = '\0';

                int rest = acc_len - (i + 1);
                memmove(acc, acc + i + 1, rest);
                acc_len = rest;
                rstrip_newline(out);
                return len;
            }
        }

        int r = recv(s, acc + acc_len, (int)sizeof(acc) - acc_len, 0);
        if (r == 0) return 0;
        if (r == SOCKET_ERROR) return -1;
        acc_len += r;
        if (acc_len >= (int)sizeof(acc) - 1) {
            int len = (outsz - 1 < acc_len) ? (outsz - 1) : acc_len;
            memcpy(out, acc, len);
            out[len] = '\0';
            acc_len = 0;
            rstrip_newline(out);
            return len;
        }
    }
}

static DWORD WINAPI client_thread(LPVOID param) {
    client_t *cli = (client_t*)param;
    SOCKET s = cli->sock;

    // --- Envío de bienvenida inicial (Fase 2)
    {
        char banner[64];
        _snprintf(banner, sizeof(banner), "WELCOME TelemetryServer PROTO %s", PROTO_VERSION);
        send_line(s, banner);
        log_line("TX", cli->ip, cli->port, "TX", banner);

        send_line(s, "ROLE VIEWER");
        log_line("TX", cli->ip, cli->port, "TX", "ROLE VIEWER");
    }

    // --- Bucle principal: leer líneas y actuar
    char line[MAX_LINE];
    for (;;) {
        int r = recv_line_accum(s, line, sizeof(line));
        if (r == 0) {
            log_line("INFO", cli->ip, cli->port, "CLOSE", "peer_closed");
            break;
        } else if (r < 0) {
            log_line("ERROR", cli->ip, cli->port, "RX", "recv_error");
            break;
        }

        log_line("RX", cli->ip, cli->port, "RX", line);

        trim(line);
        if (!*line) continue;

        char *p = line;
        char *cmd = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        char *rest = NULL;
        if (*p) { *p = '\0'; rest = p + 1; } else { rest = p; }
        if (rest) trim(rest);
        for (char *q = cmd; *q; ++q) *q = (char)toupper(*q);

        // --- Conmutador por comando
        if (strcmp(cmd, "HELLO") == 0) {

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

            char user[64] = {0}, pass[64] = {0};
            if (rest && *rest) {
                _snprintf(user, sizeof(user), "%.63s", rest);

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

    cli->alive = 0;
    remove_client_by_sock(s);
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
            if (e == WSAEINTR) continue;
            fprintf(stderr,"accept err %d\n", e);
            break;
        }

        client_t *cliobj = (client_t*)calloc(1, sizeof(client_t));
        cliobj->sock = cfd;
        InetNtopA(AF_INET, &cli.sin_addr, cliobj->ip, sizeof(cliobj->ip));
        cliobj->port = ntohs(cli.sin_port);
        cliobj->role = ROLE_VIEWER;
        strncpy(cliobj->name, "anon", sizeof(cliobj->name)-1);
        cliobj->alive = 1;

        add_client(cliobj);

        log_line("INFO", cliobj->ip, cliobj->port, "ACCEPT", "connected");

        HANDLE h = CreateThread(NULL, 0, client_thread, cliobj, 0, NULL);
        if (!h) {

            fprintf(stderr,"CreateThread client failed\n");
            remove_client_by_sock(cfd);
            closesocket(cfd);
        } else {
            CloseHandle(h);
        }
    }

    // --- Señalizamos parada y esperamos al hilo de telemetría (cierre limpio)
    running = 0;
    if (hTel) {
        WaitForSingleObject(hTel, 2000);
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

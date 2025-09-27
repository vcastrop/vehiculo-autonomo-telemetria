// src/server.c
// Servidor TCP con telemetría periódica (Winsock2)
// Fase 2 -> Fase 3: añade hilo broadcaster que envía DATA cada 10s
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#pragma comment(lib, "Ws2_32.lib")

typedef enum { ROLE_VIEWER = 0, ROLE_ADMIN = 1 } role_t;

typedef struct client_s {
    SOCKET sock;
    char ip[64];
    int port;
    char name[64];
    role_t role;
    int alive;
    struct client_s *next;
} client_t;

static volatile int running = 1;
static client_t *clients = NULL;
static CRITICAL_SECTION cs; // protege lista clients
static WSADATA wsa;

// ... lo que ya tienes arriba (enum, client_t, cs, wsa) ...

typedef struct {
    double speed;   // km/h
    int    batt;    // %
    double temp;    // °C
    int    heading; // 0..359
} vehicle_t;

static vehicle_t V = {50.0, 100, 35.0, 0};  // estado inicial


// util
// ms desde epoch UNIX usando FILETIME (Windows)
static long long now_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // FILETIME = 100 ns desde 1601-01-01
    const unsigned long long EPOCH_DIFF_100NS = 116444736000000000ULL; // 1601->1970
    unsigned long long t100ns = uli.QuadPart - EPOCH_DIFF_100NS;
    return (long long)(t100ns / 10000ULL); // a milisegundos
}


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
            *pp = dead->next;
            if (dead->sock != INVALID_SOCKET) closesocket(dead->sock);
            free(dead);
            break;
        }
        pp = &((*pp)->next);
    }
    LeaveCriticalSection(&cs);
}

static void broadcast_line(const char *line) {
    SOCKET to_remove[128]; int rm_count = 0;

    EnterCriticalSection(&cs);
    for (client_t *c = clients; c; c = c->next) {
        if (!c->alive || c->sock == INVALID_SOCKET) continue;   // <--- NUEVO
        int res = send(c->sock, line, (int)strlen(line), 0);
        if (res == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAECONNRESET || e == WSAECONNABORTED || e == WSAENETRESET || e == WSAENOTCONN) {
                if (rm_count < (int)(sizeof(to_remove)/sizeof(to_remove[0]))) {
                    to_remove[rm_count++] = c->sock;
                }
            }
        }
    }
    LeaveCriticalSection(&cs);

    for (int i = 0; i < rm_count; ++i) {
        remove_client_by_sock(to_remove[i]);
        printf("[INFO] Cliente eliminado por fallo de envio (sock=%lld)\n", (long long)to_remove[i]);
    }
}


static int is_admin_pass(const char *user, const char *pass) {
    // Credenciales fijas para pruebas; en producción leer config.
    return (strcmp(user, "admin") == 0 && strcmp(pass, "1234") == 0);
}

// hilo por cliente: buffer por línea, parsea HELLO/AUTH/CMD (básico)
// --- hilo por cliente: bufferiza por líneas y procesa HELLO/AUTH/CMD ---
static DWORD WINAPI client_thread(LPVOID arg) {
    client_t *cli = (client_t*)arg;
    SOCKET s = cli->sock;

    char recvbuf[512];
    char line[1024];
    size_t used = 0;

    for (;;) {
        int n = recv(s, recvbuf, sizeof(recvbuf), 0);
        if (n == 0) {
            printf("[INFO] Cliente %s:%d desconectado\n", cli->ip, cli->port);
            break;
        } else if (n == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEINTR) continue;
            fprintf(stderr, "[ERR] recv fallo: %d\n", e);
            break;
        }

        // procesar byte a byte, juntando hasta '\n'
        for (int i = 0; i < n; ++i) {
            char ch = recvbuf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                line[used] = '\0';
                used = 0;

                if (line[0] == '\0') continue; // línea vacía

                printf("[RX %s:%d] %s\n", cli->ip, cli->port, line);

                // ---- parseo simple por tokens ----
                char copy[1024];
                strncpy(copy, line, sizeof(copy));
                copy[sizeof(copy)-1] = 0;

                char *tok = strtok(copy, " ");
                if (!tok) continue;

                if (strcmp(tok, "HELLO") == 0) {
                    char *name = strtok(NULL, " ");
                    if (name && *name) {
                        strncpy(cli->name, name, sizeof(cli->name)-1);
                        cli->name[sizeof(cli->name)-1] = 0;
                        char out[128];
                        int m = snprintf(out, sizeof(out), "OK hello %s\n", cli->name);
                        send(s, out, m, 0);
                    } else {
                        send(s, "ERROR 400 missing_name\n", 23, 0);
                    }

                } else if (strcmp(tok, "AUTH") == 0) {
                    char *user = strtok(NULL, " ");
                    char *pass = strtok(NULL, " ");
                    if (user && pass && is_admin_pass(user, pass)) {
                        cli->role = ROLE_ADMIN;
                        send(s, "ROLE ADMIN\n", 11, 0);
                        send(s, "OK auth\n", 8, 0);
                    } else {
                        send(s, "ERROR 401 invalid_credentials\n", 30, 0);
                    }

                } else if (strcmp(tok, "CMD") == 0) {
                    char *cmd = strtok(NULL, " ");
                    if (cli->role != ROLE_ADMIN) {
                        send(s, "ERROR 403 not_admin\n", 21, 0);
                    } else if (cmd) {
                        int handled = 0;

                        EnterCriticalSection(&cs);
                        if (strcmp(cmd, "SPEED_UP") == 0) {
                            if (V.batt < 10) {
                                LeaveCriticalSection(&cs);
                                send(s, "NACK SPEED_UP reason=LOW_BATTERY\n", 33, 0);
                                handled = 1;
                            } else if (V.speed >= 120.0) {
                                LeaveCriticalSection(&cs);
                                send(s, "NACK SPEED_UP reason=SPEED_LIMIT\n", 33, 0);
                                handled = 1;
                            } else {
                                V.speed += 5.0;
                                if (V.speed > 120.0) V.speed = 120.0;
                                LeaveCriticalSection(&cs);
                                handled = 1;
                                char out[64]; int m = snprintf(out, sizeof(out), "ACK %s accepted\n", cmd);
                                send(s, out, m, 0);
                            }

                        } else if (strcmp(cmd, "SLOW_DOWN") == 0) {
                            if (V.speed <= 0.0) {
                                LeaveCriticalSection(&cs);
                                send(s, "NACK SLOW_DOWN reason=INVALID_STATE\n", 36, 0);
                                handled = 1;
                            } else {
                                V.speed -= 5.0;
                                if (V.speed < 0.0) V.speed = 0.0;
                                LeaveCriticalSection(&cs);
                                handled = 1;
                                char out[64]; int m = snprintf(out, sizeof(out), "ACK %s accepted\n", cmd);
                                send(s, out, m, 0);
                            }

                        } else if (strcmp(cmd, "TURN_LEFT") == 0) {
                            V.heading = (V.heading + 360 - 15) % 360;
                            LeaveCriticalSection(&cs);
                            handled = 1;
                            char out[64]; int m = snprintf(out, sizeof(out), "ACK %s accepted\n", cmd);
                            send(s, out, m, 0);

                        } else if (strcmp(cmd, "TURN_RIGHT") == 0) {
                            V.heading = (V.heading + 15) % 360;
                            LeaveCriticalSection(&cs);
                            handled = 1;
                            char out[64]; int m = snprintf(out, sizeof(out), "ACK %s accepted\n", cmd);
                            send(s, out, m, 0);

                        } else {
                            LeaveCriticalSection(&cs);
                        }

                        if (!handled) {
                            send(s, "ERROR 400 invalid_cmd\n", 22, 0);
                        }
                    } else {
                        send(s, "ERROR 400 missing_cmd\n", 22, 0);
                    }
                } else if (strcmp(tok, "BYE") == 0) {
                    send(s, "OK bye\n", 7, 0);

                    // marcar "no vivo" bajo lock para que el broadcaster ya no lo elija
                    EnterCriticalSection(&cs);
                    cli->alive = 0;
                    SOCKET ss = cli->sock;
                    LeaveCriticalSection(&cs);

                    shutdown(ss, SD_BOTH);
                    remove_client_by_sock(ss);
                    return 0;
                } else if (strcmp(tok, "USERS") == 0){
                    if (cli->role != ROLE_ADMIN) {
                        send(s, "ERROR 403 not_admin\n", 21, 0);
                    } else {
                        EnterCriticalSection(&cs);
                        int count = 0;
                        for (client_t *c = clients; c; c = c->next) count++;
                        char head[64]; int m = snprintf(head, sizeof(head), "USERS count=%d\n", count);
                        send(s, head, m, 0);
                        int idx = 1;
                        for (client_t *c = clients; c; c = c->next, ++idx) {
                            char line[256];
                            int n = snprintf(line, sizeof(line),
                                "USER %d name=%s ip=%s port=%d role=%s\n",
                                idx, c->name[0]?c->name:"anon", c->ip, c->port,
                                (c->role==ROLE_ADMIN)?"ADMIN":"VIEWER");
                            send(s, line, n, 0);
                        }
                        LeaveCriticalSection(&cs);
                        send(s, "OK users\n", 9, 0);
                    }
                } else {
                    send(s, "ERROR 400 unknown_cmd\n", 22, 0);
                }

            } else {
                // acumular hasta completar línea
                if (used < sizeof(line) - 1) {
                    line[used++] = ch;
                } else {
                    // overflow: descartar línea actual
                    used = 0;
                    fprintf(stderr, "[WARN] linea demasiado larga, descartada\n");
                }
            }
        } // fin for bytes
    }     // fin for recv

    remove_client_by_sock(s);
    return 0;
}


// hilo difusor de telemetría
static DWORD WINAPI telemetry_thread(LPVOID arg) {
    (void)arg;

    while (running) {
        // --- actualizar estado con "drift" leve ---
        EnterCriticalSection(&cs);
        // drift aleatorio en speed: ±0.2 km/h
        double drift = ((rand() % 401) - 200) / 1000.0;  // -0.200 .. +0.200
        V.speed += drift;
        if (V.speed < 0.0) V.speed = 0.0;
        if (V.speed > 120.0) V.speed = 120.0;

        // heading deriva muy leve: ±3°
        int hdiff = (rand() % 7) - 3;                    // -3..+3
        V.heading = (V.heading + 360 + hdiff) % 360;

        // temperatura sube/baja suavemente hacia ~35–45 °C
        double target = 40.0;
        V.temp += (target - V.temp) * 0.02;              // relajación
        // batería baja muy lento: 1% cada 3 ciclos aprox
        static int cyc = 0;
        if ((++cyc % 3) == 0 && V.batt > 0) V.batt -= 1;

        // tomar snapshot para enviar
        double speed = V.speed; int batt = V.batt; double temp = V.temp; int heading = V.heading;
        LeaveCriticalSection(&cs);

        // --- difundir DATA a todos ---
        long long ts = now_ms();
        char line[256];
        int len = snprintf(line, sizeof(line),
            "DATA speed=%.1f battery=%d temp=%.1f heading=%d ts=%lld\n",
            speed, batt, temp, heading, ts);
        if (len > 0) {
            broadcast_line(line);
            printf("[TX *] %s", line);
        }

        Sleep(10000); // cada 10 s
    }
    return 0;
}


int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Puerto invalido: %s\n", argv[1]);
        return 1;
    }

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup fallo\n"); return 1;
    }

    InitializeCriticalSection(&cs);

    srand((unsigned)time(NULL));

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) die:
    if (srv == INVALID_SOCKET) { fprintf(stderr,"socket err %d\n", WSAGetLastError()); return 1; }

    BOOL opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr; ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr,"bind err %d\n", WSAGetLastError()); return 1;
    }
    if (listen(srv, 16) == SOCKET_ERROR) {
        fprintf(stderr,"listen err %d\n", WSAGetLastError()); return 1;
    }

    // lanzar hilo de telemetría
    HANDLE hTel = CreateThread(NULL, 0, telemetry_thread, NULL, 0, NULL);
    if (!hTel) { fprintf(stderr,"CreateThread telemetry failed\n"); }

    printf("[INFO] Servidor escuchando en puerto %d (TCP)\n", port);
    printf("[INFO] Esperando conexiones...\n");

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

        // enviar saludo inicial
        send(cfd, "WELCOME TelemetryServer PROTO 1.0\n", 33, 0);
        send(cfd, "ROLE VIEWER\n", 12, 0);

        // crear hilo de cliente
        HANDLE h = CreateThread(NULL, 0, client_thread, cliobj, 0, NULL);
        if (!h) {
            fprintf(stderr,"CreateThread client failed\n");
            remove_client_by_sock(cfd);
        } else {
            CloseHandle(h); // no esperamos join
        }

        printf("[INFO] Cliente conectado desde %s:%d\n", cliobj->ip, cliobj->port);
    }

    // limpieza (no solemos llegar aquí en pruebas)
    running = 0;
    Sleep(100);
    DeleteCriticalSection(&cs);
    closesocket(srv);
    WSACleanup();
    return 0;
}

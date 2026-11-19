#define WIN32_LEAN_AND_MEAN
#include "../include/common.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <windows.h>

#ifndef PORT
#define PORT 8080
#endif

#ifndef BUFFER_SIZE
#define BUFFER_SIZE 1024
#endif

#define MAX_CLIENTS 16

// Control IDs
#define ID_BTN_START 101
#define ID_BTN_STOP 102
#define ID_EDIT_PORT 103
#define ID_LIST_CLIENTS 104
#define ID_EDIT_LOG 105
#define ID_EDIT_INPUT 106
#define ID_BTN_SEND 107

// Custom thread-safe UI update messages
#define WM_APP_LOG (WM_APP + 1)
#define WM_APP_CLIENTS (WM_APP + 2)

HWND hMainWnd, hBtnStart, hBtnStop, hEditPort, hListClients, hEditLog, hStatusLabel;
HWND hEditInput, hBtnSend;
HFONT hFontUI, hFontMono;
WNDPROC origInputProc;

int server_fd = -1;
volatile int is_running = 0;
int client_fds[MAX_CLIENTS];
char client_addrs[MAX_CLIENTS][64];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void ui_log(const char *fmt, ...) {
    char *msg = (char *)malloc(512);
    if (!msg)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, 512, fmt, args);
    va_end(args);
    PostMessage(hMainWnd, WM_APP_LOG, 0, (LPARAM)msg);
}

void refresh_client_list_ui(void) {
    PostMessage(hMainWnd, WM_APP_CLIENTS, 0, 0);
}

int broadcast_message(const char *msg, int len, int sender_fd) {
    int count = 0;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1 && client_fds[i] != sender_fd) {
            if (send_all(client_fds[i], msg, (size_t)len) == 0) {
                count++;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return count;
}

void send_server_broadcast(void) {
    if (!is_running)
        return;

    char text[BUFFER_SIZE - 64];
    GetWindowTextA(hEditInput, text, sizeof(text));
    if (strlen(text) == 0)
        return;

    char packet[BUFFER_SIZE];
    snprintf(packet, sizeof(packet), "[SERVER]: %s\n", text);

    // sender_fd = -1 broadcasts to EVERY connected client
    int sent_count = broadcast_message(packet, (int)strlen(packet), -1);
    ui_log("[SERVER -> %d client(s)] %s", sent_count, text);
    SetWindowTextA(hEditInput, "");
}

LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        send_server_broadcast();
        return 0;
    }
    return CallWindowProcA(origInputProc, hwnd, msg, wParam, lParam);
}

void *handle_client(void *arg) {
    int idx = *(int *)arg;
    free(arg);

    pthread_mutex_lock(&clients_mutex);
    int cfd = client_fds[idx];
    char addr_copy[64];
    strcpy(addr_copy, client_addrs[idx]);
    pthread_mutex_unlock(&clients_mutex);

    char buffer[BUFFER_SIZE];
    while (is_running) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recv(cfd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            break;
        }
        char clean[BUFFER_SIZE];
        strncpy(clean, buffer, BUFFER_SIZE - 1);
        clean[strcspn(clean, "\r\n")] = 0;

        ui_log("[recv fd=%d | %s] %s", cfd, addr_copy, clean);
        int relayed = broadcast_message(buffer, bytes, cfd);
        ui_log("[relay] Broadcasted %d bytes to %d peer(s)", bytes, relayed);
    }

    pthread_mutex_lock(&clients_mutex);
    if (client_fds[idx] != -1) {
        ui_log("[close] Client disconnected (fd=%d, %s)", cfd, addr_copy);
        close(client_fds[idx]);
        client_fds[idx] = -1;
        client_addrs[idx][0] = '\0';
    }
    pthread_mutex_unlock(&clients_mutex);
    refresh_client_list_ui();
    return NULL;
}

void *accept_loop(void *arg) {
    int port = *(int *)arg;
    free(arg);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        ui_log("[ERROR] socket() failed");
        is_running = 0;
        return NULL;
    }
    ui_log("[socket()] Created listening socket fd=%d", server_fd);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        ui_log("[ERROR] bind() failed on port %d (port in use?)", port);
        close(server_fd);
        server_fd = -1;
        is_running = 0;
        return NULL;
    }
    ui_log("[bind()] Bound fd=%d to 0.0.0.0:%d", server_fd, port);

    if (listen(server_fd, 5) < 0) {
        ui_log("[ERROR] listen() failed");
        close(server_fd);
        server_fd = -1;
        is_running = 0;
        return NULL;
    }
    ui_log("[listen()] Server listening for incoming TCP connections...");

    while (is_running) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int new_fd = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (new_fd < 0) {
            if (is_running)
                ui_log("[accept()] Interrupted or closed");
            break;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, ip_str, sizeof(ip_str));
        int cli_port = ntohs(cli_addr.sin_port);

        pthread_mutex_lock(&clients_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] == -1) {
                slot = i;
                client_fds[i] = new_fd;
                snprintf(client_addrs[i], sizeof(client_addrs[i]), "%s:%d", ip_str, cli_port);
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (slot == -1) {
            ui_log("[WARN] Max clients reached; rejecting fd=%d", new_fd);
            close(new_fd);
            continue;
        }

        ui_log("[accept()] New connection -> fd=%d from %s:%d", new_fd, ip_str, cli_port);
        refresh_client_list_ui();

        int *arg_idx = (int *)malloc(sizeof(int));
        *arg_idx = slot;
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, arg_idx);
        pthread_detach(tid);
    }

    return NULL;
}

void start_server(void) {
    if (is_running)
        return;
    char port_buf[16];
    GetWindowTextA(hEditPort, port_buf, sizeof(port_buf));
    int port = atoi(port_buf);
    if (port <= 0 || port > 65535)
        port = PORT;

    is_running = 1;
    EnableWindow(hBtnStart, FALSE);
    EnableWindow(hBtnStop, TRUE);
    EnableWindow(hEditPort, FALSE);
    EnableWindow(hEditInput, TRUE);
    EnableWindow(hBtnSend, TRUE);
    SetWindowTextA(hStatusLabel, "Status: LISTENING");

    int *p_arg = (int *)malloc(sizeof(int));
    *p_arg = port;
    pthread_t tid;
    pthread_create(&tid, NULL, accept_loop, p_arg);
    pthread_detach(tid);
    SetFocus(hEditInput);
}

void stop_server(void) {
    if (!is_running)
        return;
    is_running = 0;

    if (server_fd != -1) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        server_fd = -1;
    }

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1) {
            shutdown(client_fds[i], SHUT_RDWR);
            close(client_fds[i]);
            client_fds[i] = -1;
            client_addrs[i][0] = '\0';
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    refresh_client_list_ui();
    ui_log("[close()] Server stopped and all sockets closed.");

    EnableWindow(hBtnStart, TRUE);
    EnableWindow(hBtnStop, FALSE);
    EnableWindow(hEditPort, TRUE);
    EnableWindow(hEditInput, FALSE);
    EnableWindow(hBtnSend, FALSE);
    SetWindowTextA(hStatusLabel, "Status: STOPPED");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hFontUI = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        hFontMono = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

        HWND hLblPort = CreateWindowA("STATIC", "Port:", WS_VISIBLE | WS_CHILD, 15, 18, 40, 22, hwnd, NULL, NULL, NULL);
        SendMessage(hLblPort, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        char default_port[16];
        snprintf(default_port, sizeof(default_port), "%d", PORT);
        hEditPort = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", default_port, WS_VISIBLE | WS_CHILD | ES_NUMBER, 55, 15,
                                    65, 25, hwnd, (HMENU)ID_EDIT_PORT, NULL, NULL);
        SendMessage(hEditPort, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hBtnStart = CreateWindowA("BUTTON", "Start Server", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 130, 14, 105, 27,
                                  hwnd, (HMENU)ID_BTN_START, NULL, NULL);
        SendMessage(hBtnStart, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hBtnStop = CreateWindowA("BUTTON", "Stop Server", WS_VISIBLE | WS_CHILD, 245, 14, 105, 27, hwnd,
                                 (HMENU)ID_BTN_STOP, NULL, NULL);
        SendMessage(hBtnStop, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        EnableWindow(hBtnStop, FALSE);

        hStatusLabel =
            CreateWindowA("STATIC", "Status: STOPPED", WS_VISIBLE | WS_CHILD, 370, 18, 200, 22, hwnd, NULL, NULL, NULL);
        SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        HWND hLblClients = CreateWindowA("STATIC", "Active File Descriptors (Clients):", WS_VISIBLE | WS_CHILD, 15, 52,
                                         230, 20, hwnd, NULL, NULL, NULL);
        SendMessage(hLblClients, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hListClients = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", NULL,
                                       WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 15, 75, 230, 315,
                                       hwnd, (HMENU)ID_LIST_CLIENTS, NULL, NULL);
        SendMessage(hListClients, WM_SETFONT, (WPARAM)hFontMono, TRUE);

        HWND hLblLog = CreateWindowA("STATIC", "Server Syscall & Relay Log:", WS_VISIBLE | WS_CHILD, 260, 52, 250, 20,
                                     hwnd, NULL, NULL, NULL);
        SendMessage(hLblLog, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hEditLog = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                   WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                   260, 75, 460, 315, hwnd, (HMENU)ID_EDIT_LOG, NULL, NULL);
        SendMessage(hEditLog, WM_SETFONT, (WPARAM)hFontMono, TRUE);

        // Bottom broadcast bar
        HWND hLblBroadcast =
            CreateWindowA("STATIC", "Broadcast:", WS_VISIBLE | WS_CHILD, 15, 405, 75, 22, hwnd, NULL, NULL, NULL);
        SendMessage(hLblBroadcast, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hEditInput = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 95, 402, 515,
                                     27, hwnd, (HMENU)ID_EDIT_INPUT, NULL, NULL);
        SendMessage(hEditInput, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        EnableWindow(hEditInput, FALSE);
        origInputProc = (WNDPROC)SetWindowLongPtrA(hEditInput, GWLP_WNDPROC, (LONG_PTR)InputSubclassProc);

        hBtnSend = CreateWindowA("BUTTON", "Broadcast", WS_VISIBLE | WS_CHILD, 620, 401, 100, 29, hwnd,
                                 (HMENU)ID_BTN_SEND, NULL, NULL);
        SendMessage(hBtnSend, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        EnableWindow(hBtnSend, FALSE);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_START)
            start_server();
        else if (LOWORD(wParam) == ID_BTN_STOP)
            stop_server();
        else if (LOWORD(wParam) == ID_BTN_SEND)
            send_server_broadcast();
        break;

    case WM_APP_LOG: {
        char *msg = (char *)lParam;
        int len = GetWindowTextLengthA(hEditLog);
        SendMessageA(hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageA(hEditLog, EM_REPLACESEL, FALSE, (LPARAM)msg);
        SendMessageA(hEditLog, EM_REPLACESEL, FALSE, (LPARAM) "\r\n");
        free(msg);
        break;
    }
    case WM_APP_CLIENTS: {
        SendMessageA(hListClients, LB_RESETCONTENT, 0, 0);
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] != -1) {
                char entry[128];
                snprintf(entry, sizeof(entry), "fd=%d  (%s)", client_fds[i], client_addrs[i]);
                SendMessageA(hListClients, LB_ADDSTRING, 0, (LPARAM)entry);
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        break;
    }
    case WM_DESTROY:
        stop_server();
        DeleteObject(hFontUI);
        DeleteObject(hFontMono);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)receive_messages;
    for (int i = 0; i < MAX_CLIENTS; i++)
        client_fds[i] = -1;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "SocketServerGUIClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hMainWnd = CreateWindowA("SocketServerGUIClass", "TCP Socket Chat | Server Monitor (Cygwin)",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 80, 120, 745, 480, NULL, NULL,
                             hInstance, NULL);

    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
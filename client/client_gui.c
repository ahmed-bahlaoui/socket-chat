#include "../include/common.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef PORT
#define PORT 8080
#endif

#ifndef BUFFER_SIZE
#define BUFFER_SIZE 1024
#endif

// Control IDs
#define ID_EDIT_IP 201
#define ID_EDIT_PORT 202
#define ID_EDIT_NAME 203
#define ID_BTN_CONNECT 204
#define ID_BTN_DISCONN 205
#define ID_EDIT_CHAT 206
#define ID_EDIT_INPUT 207
#define ID_BTN_SEND 208

#define WM_APP_CHAT_MSG (WM_APP + 1)
#define WM_APP_DISCONN (WM_APP + 2)

HWND hMainWnd, hEditIP, hEditPort, hEditName, hBtnConnect, hBtnDisconn;
HWND hEditChat, hEditInput, hBtnSend, hStatusLabel;
HFONT hFontUI, hFontMono;
WNDPROC origInputProc;

int sock_fd = -1;
volatile int is_connected = 0;

void append_chat(const char *fmt, ...) {
    char *msg = (char *)malloc(BUFFER_SIZE + 128);
    if (!msg)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, BUFFER_SIZE + 128, fmt, args);
    va_end(args);
    PostMessage(hMainWnd, WM_APP_CHAT_MSG, 0, (LPARAM)msg);
}

void *recv_loop(void *arg) {
    (void)arg;
    char buffer[BUFFER_SIZE];
    while (is_connected) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            if (is_connected) {
                append_chat("[System] Server closed the connection.");
                PostMessage(hMainWnd, WM_APP_DISCONN, 0, 0);
            }
            break;
        }
        buffer[strcspn(buffer, "\r\n")] = 0;
        append_chat("%s", buffer);
    }
    return NULL;
}

void disconnect_client(void) {
    if (!is_connected && sock_fd == -1)
        return;
    is_connected = 0;
    if (sock_fd != -1) {
        shutdown(sock_fd, SHUT_RDWR);
        close(sock_fd);
        sock_fd = -1;
    }
    EnableWindow(hBtnConnect, TRUE);
    EnableWindow(hBtnDisconn, FALSE);
    EnableWindow(hEditIP, TRUE);
    EnableWindow(hEditPort, TRUE);
    EnableWindow(hEditName, TRUE);
    EnableWindow(hEditInput, FALSE);
    EnableWindow(hBtnSend, FALSE);
    SetWindowTextA(hStatusLabel, "Status: DISCONNECTED");
}

void connect_client(void) {
    if (is_connected)
        return;

    char ip[64], port_str[16], name[32];
    GetWindowTextA(hEditIP, ip, sizeof(ip));
    GetWindowTextA(hEditPort, port_str, sizeof(port_str));
    GetWindowTextA(hEditName, name, sizeof(name));
    if (strlen(name) == 0)
        strcpy(name, "Client");

    int port = atoi(port_str);
    if (port <= 0 || port > 65535)
        port = PORT;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        append_chat("[Error] socket() failed.");
        return;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        append_chat("[Error] Invalid IPv4 address: %s", ip);
        close(sock_fd);
        sock_fd = -1;
        return;
    }

    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        append_chat("[Error] connect() failed to %s:%d. Is the server running?", ip, port);
        close(sock_fd);
        sock_fd = -1;
        return;
    }

    is_connected = 1;
    EnableWindow(hBtnConnect, FALSE);
    EnableWindow(hBtnDisconn, TRUE);
    EnableWindow(hEditIP, FALSE);
    EnableWindow(hEditPort, FALSE);
    EnableWindow(hEditName, FALSE);
    EnableWindow(hEditInput, TRUE);
    EnableWindow(hBtnSend, TRUE);

    char status[64];
    snprintf(status, sizeof(status), "Status: CONNECTED (fd=%d)", sock_fd);
    SetWindowTextA(hStatusLabel, status);
    append_chat("[System] Connected to %s:%d as '%s' (local fd=%d)", ip, port, name, sock_fd);

    pthread_t tid;
    pthread_create(&tid, NULL, recv_loop, NULL);
    pthread_detach(tid);
    SetFocus(hEditInput);
}

void send_chat_message(void) {
    if (!is_connected || sock_fd == -1)
        return;

    char text[BUFFER_SIZE - 64];
    char name[32];
    GetWindowTextA(hEditInput, text, sizeof(text));
    GetWindowTextA(hEditName, name, sizeof(name));
    if (strlen(text) == 0)
        return;

    char packet[BUFFER_SIZE];
    snprintf(packet, sizeof(packet), "[%s]: %s\n", name, text);

    if (send_all(sock_fd, packet, strlen(packet)) < 0) {
        append_chat("[Error] send() failed.");
        disconnect_client();
        return;
    }

    append_chat("[You (%s)]: %s", name, text);
    SetWindowTextA(hEditInput, "");
}

// Allow pressing Enter inside the message input box to send immediately
LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        send_chat_message();
        return 0;
    }
    return CallWindowProcA(origInputProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hFontUI = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        hFontMono = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

        HWND hLblIP = CreateWindowA("STATIC", "IP:", WS_VISIBLE | WS_CHILD, 15, 18, 22, 22, hwnd, NULL, NULL, NULL);
        SendMessage(hLblIP, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hEditIP = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "127.0.0.1", WS_VISIBLE | WS_CHILD, 38, 15, 95, 25, hwnd,
                                  (HMENU)ID_EDIT_IP, NULL, NULL);
        SendMessage(hEditIP, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        HWND hLblPort =
            CreateWindowA("STATIC", "Port:", WS_VISIBLE | WS_CHILD, 142, 18, 35, 22, hwnd, NULL, NULL, NULL);
        SendMessage(hLblPort, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        char default_port[16];
        snprintf(default_port, sizeof(default_port), "%d", PORT);
        hEditPort = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", default_port, WS_VISIBLE | WS_CHILD | ES_NUMBER, 178, 15,
                                    55, 25, hwnd, (HMENU)ID_EDIT_PORT, NULL, NULL);
        SendMessage(hEditPort, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        HWND hLblName =
            CreateWindowA("STATIC", "User:", WS_VISIBLE | WS_CHILD, 242, 18, 38, 22, hwnd, NULL, NULL, NULL);
        SendMessage(hLblName, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hEditName = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "Alice", WS_VISIBLE | WS_CHILD, 282, 15, 85, 25, hwnd,
                                    (HMENU)ID_EDIT_NAME, NULL, NULL);
        SendMessage(hEditName, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hBtnConnect = CreateWindowA("BUTTON", "Connect", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 378, 14, 85, 27,
                                    hwnd, (HMENU)ID_BTN_CONNECT, NULL, NULL);
        SendMessage(hBtnConnect, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hBtnDisconn = CreateWindowA("BUTTON", "Disconnect", WS_VISIBLE | WS_CHILD, 470, 14, 90, 27, hwnd,
                                    (HMENU)ID_BTN_DISCONN, NULL, NULL);
        SendMessage(hBtnDisconn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        EnableWindow(hBtnDisconn, FALSE);

        hStatusLabel = CreateWindowA("STATIC", "Status: DISCONNECTED", WS_VISIBLE | WS_CHILD, 15, 48, 300, 20, hwnd,
                                     NULL, NULL, NULL);
        SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

        hEditChat = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                    WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                    15, 72, 545, 275, hwnd, (HMENU)ID_EDIT_CHAT, NULL, NULL);
        SendMessage(hEditChat, WM_SETFONT, (WPARAM)hFontMono, TRUE);

        hEditInput = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 15, 358, 450,
                                     27, hwnd, (HMENU)ID_EDIT_INPUT, NULL, NULL);
        SendMessage(hEditInput, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        EnableWindow(hEditInput, FALSE);
        origInputProc = (WNDPROC)SetWindowLongPtrA(hEditInput, GWLP_WNDPROC, (LONG_PTR)InputSubclassProc);

        hBtnSend = CreateWindowA("BUTTON", "Send", WS_VISIBLE | WS_CHILD, 475, 357, 85, 29, hwnd, (HMENU)ID_BTN_SEND,
                                 NULL, NULL);
        SendMessage(hBtnSend, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        EnableWindow(hBtnSend, FALSE);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_CONNECT)
            connect_client();
        else if (LOWORD(wParam) == ID_BTN_DISCONN)
            disconnect_client();
        else if (LOWORD(wParam) == ID_BTN_SEND)
            send_chat_message();
        break;

    case WM_APP_CHAT_MSG: {
        char *msg = (char *)lParam;
        int len = GetWindowTextLengthA(hEditChat);
        SendMessageA(hEditChat, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageA(hEditChat, EM_REPLACESEL, FALSE, (LPARAM)msg);
        SendMessageA(hEditChat, EM_REPLACESEL, FALSE, (LPARAM) "\r\n");
        free(msg);
        break;
    }
    case WM_APP_DISCONN:
        disconnect_client();
        break;

    case WM_DESTROY:
        disconnect_client();
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

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "SocketClientGUIClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hMainWnd = CreateWindowA("SocketClientGUIClass", "TCP Socket Chat | Client Window (Cygwin)",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 850, 120, 585, 435, NULL, NULL,
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
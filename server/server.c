
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "common.h"

#define PORT 8080
#define BACKLOG 1

int main(void) {
    int server_fd;
    int client_fd;

    // Definition des addresses serveur et client
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    // Classification
    pthread_t receiver_thread;

    char buffer[BUFFER_SIZE];

    signal(SIGPIPE, SIG_IGN);

    // 1. Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // 2. Configure server address
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 3. Allow quick server restart
    int opt = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    // 4. Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    // 5. Listen for connections
    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[SERVER] Listening on port %d...\n", PORT);

    // 6. Accept one client
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd == -1) {
        perror("accept");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[SERVER] Client connected: %s\n", inet_ntoa(client_addr.sin_addr));

    printf("[CHAT] Type /quit to disconnect.\n\n");

    // 7. Start receiver thread
    if (pthread_create(&receiver_thread, NULL, receive_messages, &client_fd) != 0) {
        perror("pthread_create");
        close(client_fd);
        close(server_fd);
        return EXIT_FAILURE;
    }

    // 8. Read and send messages
    while (1) {

        printf("You: ");
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
            break;

        if (strcmp(buffer, "/quit\n") == 0 || strcmp(buffer, "/quit") == 0) {
            printf("[SERVER] Disconnecting...\n");
            break;
        }

        if (send_all(client_fd, buffer, strlen(buffer)) == -1) {
            perror("send");
            break;
        }
    }

    // 9. Stop communication
    shutdown(client_fd, SHUT_RDWR);

    pthread_join(receiver_thread, NULL);

    // 10. Close sockets
    close(client_fd);
    close(server_fd);

    printf("[SERVER] Server stopped.\n");

    return EXIT_SUCCESS;
}

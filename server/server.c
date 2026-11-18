
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8080
#define BACKLOG 1

int main(void) {
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    // 1. Create TCP socket
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

    // 3. Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    // 4. Start listening
    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[SERVER] Listening on port %d...\n", PORT);

    // 5. Wait for client connection
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd == -1) {
        perror("accept");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[SERVER] Client connected: %s\n", inet_ntoa(client_addr.sin_addr));

    // Temporary: keep connection open until client disconnects
    char buffer[1024];
    ssize_t bytes_received;

    bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    // Server response
    const char *response = "Hello from server!\n";

    send(client_fd, response, strlen(response), 0);

    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("[SERVER] Received: %s\n", buffer);
    }

    // 6. Close sockets
    close(client_fd);
    close(server_fd);

    return EXIT_SUCCESS;
}

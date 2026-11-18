
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int client_fd;
    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // 1. Check command-line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // 2. Create TCP socket
    client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (client_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // 3. Configure server address
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP address\n");
        close(client_fd);
        return EXIT_FAILURE;
    }

    // 4. Connect to server
    printf("[CLIENT] Connecting to %s:%d...\n", argv[1], PORT);

    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(client_fd);
        return EXIT_FAILURE;
    }

    printf("[CLIENT] Connected successfully!\n");

    // 5. Send a message
    const char *message = "Hello from client!\n";

    if (send(client_fd, message, strlen(message), 0) == -1) {
        perror("send");
        close(client_fd);
        return EXIT_FAILURE;
    }

    printf("[CLIENT] Message sent: %s", message);

    // 6. Receive response
    bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("[SERVER] %s", buffer);
    } else if (bytes_received == 0) {
        printf("[CLIENT] Server disconnected.\n");
    } else {
        perror("recv");
    }

    // 7. Close socket
    close(client_fd);

    return EXIT_SUCCESS;
}

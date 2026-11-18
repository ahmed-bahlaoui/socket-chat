
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

int main(int argc, char *argv[]) {
    int client_fd;
    struct sockaddr_in server_addr;
    pthread_t receiver_thread;

    char buffer[BUFFER_SIZE];

    signal(SIGPIPE, SIG_IGN);

    // 1. Check arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // 2. Create socket
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
        fprintf(stderr, "Invalid IP address\n");
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
    printf("[CHAT] Type /quit to disconnect.\n\n");

    // 5. Start receiver thread
    if (pthread_create(&receiver_thread, NULL, receive_messages, &client_fd) != 0) {
        perror("pthread_create");
        close(client_fd);
        return EXIT_FAILURE;
    }

    // 6. Read and send messages
    while (1) {

        printf("You: ");
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
            break;

        if (strcmp(buffer, "/quit\n") == 0 || strcmp(buffer, "/quit") == 0) {
            printf("[CLIENT] Disconnecting...\n");
            break;
        }

        if (send_all(client_fd, buffer, strlen(buffer)) == -1) {
            perror("send");
            break;
        }
    }

    // 7. Stop communication
    shutdown(client_fd, SHUT_RDWR);

    pthread_join(receiver_thread, NULL);

    // 8. Close socket
    close(client_fd);

    printf("[CLIENT] Disconnected.\n");

    return EXIT_SUCCESS;
}

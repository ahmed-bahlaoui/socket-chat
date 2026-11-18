
#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024
#define MAX_MESSAGE 4096

/*
 * Send all bytes, even if send() sends only part
 * of the requested data.
 */
static int send_all(int socket_fd, const char *data, size_t length) {
    size_t total_sent = 0;

    while (total_sent < length) {

        ssize_t sent = send(socket_fd, data + total_sent, length - total_sent, 0);

        if (sent == -1) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (sent == 0)
            return -1;

        total_sent += (size_t)sent;
    }

    return 0;
}

/*
 * Thread responsible for receiving messages.
 */
static void *receive_messages(void *arg) {
    int socket_fd = *(int *)arg;

    char buffer[BUFFER_SIZE];
    char message[MAX_MESSAGE];

    size_t message_length = 0;
    int discarding = 0;

    while (1) {

        ssize_t received = recv(socket_fd, buffer, sizeof(buffer), 0);

        if (received == 0) {
            printf("\n[CHAT] Peer disconnected.\n");
            break;
        }

        if (received == -1) {
            if (errno == EINTR)
                continue;

            perror("recv");
            break;
        }

        for (ssize_t i = 0; i < received; i++) {

            char c = buffer[i];

            if (c == '\n') {

                if (discarding) {
                    printf("[CHAT] Message too long.\n");
                } else {
                    if (message_length > 0 && message[message_length - 1] == '\r') {
                        message_length--;
                    }

                    message[message_length] = '\0';

                    printf("\n[PEER] %s\n", message);
                    fflush(stdout);
                }

                message_length = 0;
                discarding = 0;
            } else if (!discarding) {

                if (message_length < MAX_MESSAGE - 1) {
                    message[message_length++] = c;
                } else {
                    discarding = 1;
                    message_length = 0;
                }
            }
        }
    }

    return NULL;
}

#endif

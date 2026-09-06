/*
 * Chat Server
 * A multi-client chat server using select() for I/O multiplexing.
 * Supports up to MAX_CLIENTS concurrent connections, tracks nicknames,
 * and broadcasts messages from one client to all other connected clients.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define MAX_NICK 32
#define BUFFER_SIZE 1024

/*
 * Helper function to send all data reliably, handling partial sends
 * Returns 0 on success, -1 on error
 */
int send_all(int sockfd, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t sent = send(sockfd, buf + total, len - total, 0);
        if (sent < 0) {
            return -1;
        }
        total += (size_t)sent;
    }
    return 0;
}

/*
 * Send a line to a single client slot. Closes and clears the slot on failure.
 */
void send_to(int client_sockets[], int slot, const char *buf, size_t len) {
    if (client_sockets[slot] <= 0) {
        return;
    }
    if (send_all(client_sockets[slot], buf, len) < 0) {
        close(client_sockets[slot]);
        client_sockets[slot] = 0;
    }
}

/*
 * Broadcast a line to every connected client except (optionally) one slot.
 */
void broadcast(int client_sockets[], int except_slot, const char *buf, size_t len) {
    for (int j = 0; j < MAX_CLIENTS; j++) {
        if (client_sockets[j] > 0 && j != except_slot) {
            send_to(client_sockets, j, buf, len);
        }
    }
}

int main(void) {
    int server_fd, new_socket, client_sockets[MAX_CLIENTS];
    char nicks[MAX_CLIENTS][MAX_NICK];
    struct sockaddr_in address;
    int opt = 1, max_sd, activity;
    fd_set readfds;
    char buffer[BUFFER_SIZE];

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = 0;
        snprintf(nicks[i], MAX_NICK, "guest%d", i + 1);
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0) {
                FD_SET(client_sockets[i], &readfds);
            }
            if (client_sockets[i] > max_sd) {
                max_sd = client_sockets[i];
            }
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Select error");
            continue;
        }

        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, NULL, NULL)) < 0) {
                perror("Accept failed");
                exit(EXIT_FAILURE);
            }

            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    slot = i;
                    break;
                }
            }

            if (slot < 0) {
                printf("Server full, rejecting new client\n");
                close(new_socket);
            } else {
                client_sockets[slot] = new_socket;
                printf("Client connected: %s (socket %d)\n", nicks[slot], new_socket);

                char line[BUFFER_SIZE];
                int n = snprintf(line, BUFFER_SIZE, "*** %s joined the room\n", nicks[slot]);
                broadcast(client_sockets, slot, line, (size_t)n);

                n = snprintf(line, BUFFER_SIZE,
                             "Welcome! You are %s. Set a nickname with /nick <name>\n", nicks[slot]);
                send_to(client_sockets, slot, line, (size_t)n);
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0 && FD_ISSET(client_sockets[i], &readfds)) {
                int valread = (int)read(client_sockets[i], buffer, BUFFER_SIZE - 1);

                if (valread < 0) {
                    perror("Read error");
                    close(client_sockets[i]);
                    client_sockets[i] = 0;
                } else if (valread == 0) {
                    close(client_sockets[i]);
                    client_sockets[i] = 0;
                    printf("Client disconnected\n");
                } else {
                    buffer[valread] = '\0';

                    /* /nick <name>: rename and announce */
                    if (strncmp(buffer, "/nick ", 6) == 0) {
                        char new_nick[MAX_NICK];
                        if (sscanf(buffer + 6, "%31s", new_nick) == 1 && new_nick[0] != '\0') {
                            char line[BUFFER_SIZE];
                            int n = snprintf(line, BUFFER_SIZE,
                                             "*** %s is now known as %s\n", nicks[i], new_nick);
                            broadcast(client_sockets, -1, line, (size_t)n);
                            snprintf(nicks[i], MAX_NICK, "%s", new_nick);
                            printf("Rename on socket %d: %s\n", client_sockets[i], new_nick);
                        } else {
                            const char *usage = "Usage: /nick <name>\n";
                            send_to(client_sockets, i, usage, strlen(usage));
                        }
                    } else {
                        /* Regular message: prefix with the sender's nick */
                        char line[BUFFER_SIZE + MAX_NICK + 4];
                        int n = snprintf(line, sizeof(line), "[%s] %s", nicks[i], buffer);
                        if (n > 0) {
                            printf("[%s] message relayed\n", nicks[i]);
                            broadcast(client_sockets, i, line, (size_t)n);
                        }
                    }
                }
            }
        }
    }
    return 0;
}

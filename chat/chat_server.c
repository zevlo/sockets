/*
 * Chat Server
 * A multi-client chat server using select() for I/O multiplexing.
 * Supports up to MAX_CLIENTS concurrent connections and broadcasts
 * messages from one client to all other connected clients.
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
            return -1; // Error
        }
        total += sent;
    }
    return 0; // Success
}

int main() {
    int server_fd, new_socket, client_sockets[MAX_CLIENTS];
    struct sockaddr_in address;
    int opt = 1, max_sd, activity;
    fd_set readfds;
    char buffer[BUFFER_SIZE] = {0};

    // Initialize client socket array (0 means slot is available)
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = 0;
    }

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to allow address reuse
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
    address.sin_port = htons(PORT);

    // Bind socket to address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for connections (backlog of 3)
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    // Main server loop using select() for I/O multiplexing
    while (1) {
        // Clear and rebuild the file descriptor set
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);  // Always monitor server socket
        max_sd = server_fd;

        // Add all active client sockets to the set
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0) {
                FD_SET(client_sockets[i], &readfds);
            }
            if (client_sockets[i] > max_sd) {
                max_sd = client_sockets[i];
            }
        }

        // Wait for activity on any socket (blocking call)
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            perror("Select error");
            // For other errors, continue to avoid infinite loop on transient errors
            // but could exit(EXIT_FAILURE) for fatal errors like EBADF
            continue;
        }

        // Check if server socket has a new connection
        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, NULL, NULL)) < 0) {
                perror("Accept failed");
                exit(EXIT_FAILURE);
            }

            // Find an available slot for the new client
            int client_added = 0;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    printf("New client connected: socket %d\n", new_socket);
                    client_added = 1;
                    break;
                }
            }

            // If server is full, reject the connection
            if (!client_added) {
                printf("Server full, rejecting new client\n");
                close(new_socket);
            }
        }

        // Check all client sockets for incoming data
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0 && FD_ISSET(client_sockets[i], &readfds)) {
                int valread = read(client_sockets[i], buffer, BUFFER_SIZE);

                // Handle read errors
                if (valread < 0) {
                    perror("Read error");
                    close(client_sockets[i]);
                    client_sockets[i] = 0;
                }
                // Client disconnected (EOF)
                else if (valread == 0) {
                    close(client_sockets[i]);
                    client_sockets[i] = 0;
                    printf("Client disconnected\n");
                }
                // Broadcast message to all other clients
                else {
                    // Null-terminate the buffer safely (prevent buffer overflow)
                    if (valread < BUFFER_SIZE) {
                        buffer[valread] = '\0';
                    } else {
                        buffer[BUFFER_SIZE - 1] = '\0';
                    }

                    // Send message to all other connected clients
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (client_sockets[j] > 0 && j != i) {
                            if (send_all(client_sockets[j], buffer, strlen(buffer)) < 0) {
                                // Send failed, client disconnected
                                close(client_sockets[j]);
                                client_sockets[j] = 0;
                                printf("Client disconnected (send failed)\n");
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

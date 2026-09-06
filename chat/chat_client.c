/*
 * Chat Client
 * A multi-threaded chat client that connects to a chat server.
 * Uses a separate thread to receive messages while the main thread
 * handles user input and sends messages to the server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

/*
 * Thread function to receive messages from the server
 * Runs in a separate thread and continuously reads from the socket
 */
void *receive_messages(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char buffer[BUFFER_SIZE];
    
    while(1) {
        int valread = read(sock, buffer, BUFFER_SIZE);
        // Connection closed or error occurred
        if (valread <= 0) break;
        
        // Null-terminate the buffer safely (prevent buffer overflow)
        if (valread < BUFFER_SIZE) {
            buffer[valread] = '\0';
        } else {
            buffer[BUFFER_SIZE - 1] = '\0';
        }
        printf("Received: %s", buffer);
    }
    return NULL;
}

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    pthread_t thread_id;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server\n");

    // Create thread to receive messages
    if (pthread_create(&thread_id, NULL, receive_messages, (void*)&sock) != 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }

    // Detach thread so it cleans up automatically when it exits
    pthread_detach(thread_id);

    // Main loop: read user input and send messages to server
    while(1) {
        printf("Enter message: ");
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            break; // EOF or error on stdin
        }
        
        // Send message to server
        ssize_t bytes_sent = send(sock, buffer, strlen(buffer), 0);
        if (bytes_sent < 0) {
            perror("Send failed");
            break; // Exit loop if send fails (connection closed)
        }
    }
    return 0;
}

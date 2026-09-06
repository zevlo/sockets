/*
 * Web Server
 * A static HTTP server that parses the request line and serves files
 * from the www/ directory with correct content types and error responses.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define WEB_ROOT "www"
#define REQUEST_SIZE 2048

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
 * Send an HTTP error response with a minimal HTML body
 */
void send_error_response(int client_socket, int status_code, const char *status_text, const char *message) {
    char response[BUFFER_SIZE];
    int len = snprintf(response, BUFFER_SIZE,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>%d %s</h1><p>%s</p></body></html>\r\n",
        status_code, status_text, status_code, status_text, message);

    if (len > 0 && len < (int)BUFFER_SIZE) {
        send_all(client_socket, response, (size_t)len);
    }
}

/*
 * Look up the Content-Type for a file extension.
 * Returns text/html for no extension match made explicit by the caller.
 */
const char *content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        return "application/octet-stream";
    }
    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0) {
        return "text/html";
    }
    if (strcasecmp(dot, ".css") == 0) {
        return "text/css";
    }
    if (strcasecmp(dot, ".js") == 0) {
        return "application/javascript";
    }
    if (strcasecmp(dot, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcasecmp(dot, ".png") == 0) {
        return "image/png";
    }
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcasecmp(dot, ".txt") == 0) {
        return "text/plain";
    }
    return "application/octet-stream";
}

/*
 * Reject paths that could escape www/: any ".." segment, or a
 * percent-encoded dot ("%2e"), which browsers may use to smuggle one through.
 */
int path_is_unsafe(const char *path) {
    if (strstr(path, "..") != NULL) {
        return 1;
    }
    char lower[512];
    size_t i = 0;
    for (; path[i] != '\0' && i < sizeof(lower) - 1; i++) {
        lower[i] = (char)tolower((unsigned char)path[i]);
    }
    lower[i] = '\0';
    return strstr(lower, "%2e") != NULL;
}

/*
 * Read the request until the end of the headers (\r\n\r\n) or the buffer fills.
 * Returns 0 on success, -1 on read error or malformed request.
 */
int read_request(int client_socket, char *request, size_t size) {
    size_t received = 0;
    while (received < size - 1) {
        ssize_t n = read(client_socket, request + received, size - 1 - received);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }
        received += (size_t)n;
        request[received] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL) {
            return 0;
        }
    }
    request[received] = '\0';
    return strstr(request, "\r\n") != NULL ? 0 : -1;
}

/*
 * Serve one HTTP request: parse the request line, map the path into www/,
 * send the file with headers or an error response.
 */
void handle_request(int client_socket) {
    char request[REQUEST_SIZE];
    char method[16], raw_path[512], version[16];

    if (read_request(client_socket, request, sizeof(request)) < 0) {
        send_error_response(client_socket, 400, "Bad Request", "Could not read a well-formed HTTP request.");
        return;
    }

    /* Parse just the first line: METHOD SP PATH SP VERSION */
    if (sscanf(request, "%15s %511s %15s", method, raw_path, version) != 3) {
        send_error_response(client_socket, 400, "Bad Request", "Malformed request line.");
        return;
    }

    if (strcmp(method, "GET") != 0) {
        send_error_response(client_socket, 501, "Not Implemented", "This server only handles GET.");
        return;
    }

    if (path_is_unsafe(raw_path)) {
        printf("%s %s -> 403\n", method, raw_path);
        send_error_response(client_socket, 403, "Forbidden", "Path traversal is not allowed.");
        return;
    }

    /* Map "/" to the index page, then serve the file from www/ */
    char file_path[600];
    const char *rel = strcmp(raw_path, "/") == 0 ? "/index.html" : raw_path;
    snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, rel);

    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        printf("%s %s -> 404\n", method, raw_path);
        send_error_response(client_socket, 404, "Not Found", "The requested file was not found on this server.");
        return;
    }

    /* Content-Length from the file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size < 0) {
        fclose(file);
        send_error_response(client_socket, 500, "Internal Server Error", "Could not determine file size.");
        return;
    }

    char header[BUFFER_SIZE];
    int header_len = snprintf(header, BUFFER_SIZE,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        content_type(file_path), file_size);

    if (header_len <= 0 || send_all(client_socket, header, (size_t)header_len) < 0) {
        perror("Error sending HTTP header");
        fclose(file);
        return;
    }

    /* Stream the file in chunks */
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send_all(client_socket, buffer, bytes_read) < 0) {
            perror("Error sending file content");
            break;
        }
    }

    fclose(file);
    printf("%s %s -> 200 (%ld bytes)\n", method, raw_path, file_size);
}

int main(void) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Serving %s/ on http://localhost:%d/\n", WEB_ROOT, PORT);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        handle_request(client_socket);

        close(client_socket);
    }
}

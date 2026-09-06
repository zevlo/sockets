#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    int sockfd = 0, n = 0;
    char recvBuff[1024];
    struct sockaddr_in serv_addr;

    // Validate Arguments
    if(argc != 2) {
        printf("\n Usage: %s <ip of server> \n", argv[0]);
        return 1;
    }

    // Create Socket
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(recvBuff, 0, sizeof(recvBuff));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080); 

    // Convert IP Address
    if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "\nInvalid address/ Address not supported \n");
        close(sockfd);
        return 1;
    }

    // Connect to Server
    if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connect Failed");
        close(sockfd);
        return 1;
    }

    // Read Response
    while((n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0) {
        recvBuff[n] = 0;
        if(fputs(recvBuff, stdout) == EOF) {
            perror("\n Error : Fputs error\n");
        }
        printf("\n"); 
    }

    if(n < 0) {
        perror("\n Read error \n");
    }

    close(sockfd);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

void usage(const char* prog_name) {
    printf("Usage: %s <server_ip> <server_port> <agent_id>\n", prog_name);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* server_ip = argv[1];
    int server_port = atoi(argv[2]);
    const char* agent_id = argv[3];

    // Validate input lengths to prevent buffer overflows
    if (strlen(agent_id) > 31) {
        fprintf(stderr, "Error: agent_id too long (max 31 characters).\n");
        return EXIT_FAILURE;
    }

    // Create a TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    // Define server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP address: %s\n", server_ip);
        close(sock);
        return EXIT_FAILURE;
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to connect to server");
        close(sock);
        return EXIT_FAILURE;
    }

    printf("Connected to server %s:%d\n", server_ip, server_port);

    // Prepare and send message
    char message[2 + 32];  // 'A' + agent_id
    memset(message, 0, sizeof(message));
    message[0] = 'A';
    strncpy(message + 1, agent_id, 31);
    message[sizeof(message) - 1] = '\0';  // Ensure null-termination

    if (send(sock, message, strlen(message), 0) < 0) {
        perror("Failed to send message");
        close(sock);
        return EXIT_FAILURE;
    }

    char buffer[1024];
    for (;;) {
        int n = recv(sock, buffer, 1023, 0);
        if (n <= 0) {
            if (n < 0) perror("recv failed");
            break;
        }
        buffer[n] = '\0';
        printf("Received message: '%s'\n", buffer);  // Debugging line

        if (strstr(buffer, "SHUTDOWN") != NULL) {
            printf("Received shutdown command\n");
            // system("shutdown -h now");  // shutdown command
            break;
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define BUFFER_SIZE 1024

void usage(const char* prog_name) {
    printf("Usage:\n");
    printf("  %s <server_ip> <server_port> <client_id> <target_agent_id>\n", prog_name);
    printf("  %s -l <server_ip> <server_port>\n", prog_name); // List agents
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    int list_mode = 0;
    const char* server_ip;
    int server_port;
    const char* client_id = NULL;
    const char* target_agent_id = NULL;

    if (strcmp(argv[1], "-l") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        list_mode = 1;
        server_ip = argv[2];
        server_port = atoi(argv[3]);
    } else {
        if (argc != 5) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        server_ip = argv[1];
        server_port = atoi(argv[2]);
        client_id = argv[3];
        target_agent_id = argv[4];

        // Validate input lengths to prevent buffer overflows
        if (strlen(client_id) > 31) {
            fprintf(stderr, "Error: client_id too long (max 31 characters).\n");
            return EXIT_FAILURE;
        }

        if (strlen(target_agent_id) > 31) {
            fprintf(stderr, "Error: target_agent_id too long (max 31 characters).\n");
            return EXIT_FAILURE;
        }
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
        perror("Connection to server failed");
        close(sock);
        return EXIT_FAILURE;
    }

    printf("Connected to server %s:%d\n", server_ip, server_port);

    // Prepare and send the message based on mode
    if (list_mode) {
        // Prepare 'L' message for listing agents
        char message[2]; // 'L' + '\0'
        memset(message, 0, sizeof(message));
        message[0] = 'L';
        message[1] = '\0';

        if (send(sock, message, strlen(message), 0) < 0) {
            perror("Failed to send list request");
            close(sock);
            return EXIT_FAILURE;
        }

        printf("Requested list of active agents.\n");
    } else {
        // Prepare 'C' message for shutdown command
        char message[2 + 32 + 1 + 32 + 1]; // 'C' + client_id + ' ' + target_agent_id + '\0'
        memset(message, 0, sizeof(message));
        message[0] = 'C';
        strncpy(message + 1, client_id, 31);
        message[1 + strlen(client_id)] = ' ';
        strncpy(message + 2 + strlen(client_id), target_agent_id, 31);
        message[sizeof(message) - 1] = '\0'; // Ensure null-termination

        if (send(sock, message, strlen(message), 0) < 0) {
            perror("Failed to send shutdown command");
            close(sock);
            return EXIT_FAILURE;
        }

        printf("Sent client ID and target agent ID: %s\n", message +1); // Skip 'C'
    }

    printf("Waiting for server response...\n");

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // Receive and display the response from the server
    while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        if (list_mode) {
            printf("Active Agents:\n%s\n", buffer);
        } else {
            printf("Received from server: %s\n", buffer);
        }
    }

    if (bytes_received < 0) {
        perror("Error receiving data from server");
    } else {
        printf("Server closed the connection.\n");
    }

    // Close the socket
    close(sock);
    printf("Disconnected from server.\n");

    return EXIT_SUCCESS;
}
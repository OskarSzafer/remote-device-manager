// client.c
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
    printf("  %s <server_ip> <server_port> <username> <password>\n", prog_name);
}

int main(int argc, char* argv[]) {
    if (argc != 5) { // Required arguments: server_ip, server_port, username, password
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* server_ip = argv[1];
    int server_port = atoi(argv[2]);
    const char* username = argv[3];
    const char* password = argv[4];

    // Validate input lengths to prevent buffer overflows
    if (strlen(username) > 31) {
        fprintf(stderr, "Error: username too long (max 31 characters).\n");
        return EXIT_FAILURE;
    }

    if (strlen(password) > 31) {
        fprintf(stderr, "Error: password too long (max 31 characters).\n");
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
        perror("Connection to server failed");
        close(sock);
        return EXIT_FAILURE;
    }

    printf("Connected to server %s:%d\n", server_ip, server_port);

    // Step 1: Send authentication message
    // Format: "LOGIN <username> <password>\n"
    char auth_message[BUFFER_SIZE];
    snprintf(auth_message, sizeof(auth_message), "LOGIN %s %s\n", username, password);

    if (send(sock, auth_message, strlen(auth_message), 0) < 0) {
        perror("Failed to send authentication message");
        close(sock);
        return EXIT_FAILURE;
    }

    printf("Sent authentication message.\n");

    // Wait for authentication response
    char auth_response[BUFFER_SIZE];
    ssize_t auth_bytes = recv(sock, auth_response, sizeof(auth_response) - 1, 0);
    if (auth_bytes <= 0) {
        if (auth_bytes < 0) perror("Failed to receive authentication response");
        else printf("Server closed the connection unexpectedly.\n");
        close(sock);
        return EXIT_FAILURE;
    }
    auth_response[auth_bytes] = '\0';
    printf("Authentication response: %s", auth_response);

    // Check if authentication was successful
    if (strncmp(auth_response, "Authentication successful", 24) != 0) {
        printf("Authentication failed. Exiting.\n");
        close(sock);
        return EXIT_FAILURE;
    }

    // Step 2: Enter command loop
    printf("You can now enter commands. Available commands:\n");
    printf("  LIST\n");
    printf("  SHUTDOWN <agent_id>\n");
    printf("  EXIT\n");

    while (1) {
        printf("Enter command: ");
        char input[BUFFER_SIZE];
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\nInput error. Exiting.\n");
            break;
        }

        // Remove trailing newline characters
        input[strcspn(input, "\r\n")] = '\0';

        // Validate and send the command
        if (strncmp(input, "LIST", 4) == 0 && (input[4] == '\0' || input[4] == ' ')) {
            // Send 'LIST' command
            char command[5] = "LIST";
            if (send(sock, command, strlen(command), 0) < 0) {
                perror("Failed to send LIST command");
                break;
            }
        }
        else if (strncmp(input, "SHUTDOWN ", 9) == 0) {
            // Extract agent_id
            char agent_id[32];
            if (sscanf(input + 9, "%31s", agent_id) != 1) {
                printf("Invalid SHUTDOWN command format. Usage: SHUTDOWN <agent_id>\n");
                continue;
            }

            // Prepare and send 'SHUTDOWN <agent_id>' command
            char command[BUFFER_SIZE];
            snprintf(command, sizeof(command), "SHUTDOWN %s", agent_id);
            if (send(sock, command, strlen(command), 0) < 0) {
                perror("Failed to send SHUTDOWN command");
                break;
            }
        }
        else if (strncmp(input, "EXIT", 4) == 0 && (input[4] == '\0' || input[4] == ' ')) {
            // Send 'EXIT' command to close the connection
            char command[5] = "EXIT";
            if (send(sock, command, strlen(command), 0) < 0) {
                perror("Failed to send EXIT command");
            }
            printf("Exiting as per request.\n");
            break;
        }
        else {
            printf("Unknown command. Available commands: LIST, SHUTDOWN <agent_id>, EXIT\n");
            continue;
        }

        // Wait for server response
        char response[BUFFER_SIZE];
        ssize_t bytes_received = recv(sock, response, sizeof(response) - 1, 0);
        if (bytes_received < 0) {
            perror("Error receiving data from server");
            break;
        }
        else if (bytes_received == 0) {
            printf("Server closed the connection.\n");
            break;
        }

        response[bytes_received] = '\0';
        printf("Server response:\n%s\n", response);
    }

    // Close the socket
    close(sock);
    printf("Disconnected from server.\n");

    return EXIT_SUCCESS;
}

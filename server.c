// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>

#define MAX_AGENTS 50
#define BUFFER_SIZE 1024
#define PRIVILEGES_FILE "privileges.txt" // Name of the privileges file
#define USERS_FILE "users.txt"           // Name of the users file

typedef struct {
    int socket;
    char id[32];
    int active;
} Agent;

Agent agents[MAX_AGENTS];
pthread_mutex_t agents_mutex = PTHREAD_MUTEX_INITIALIZER;

// Flag to indicate server should stop
volatile sig_atomic_t stop_server = 0;

// Signal handler
void handle_signal(int sig) {
    if (sig == SIGINT) {
        stop_server = 1;
    }
}

// Function to check user credentials
int check_credentials(const char* username, const char* password) {
    FILE *fp = fopen(USERS_FILE, "r");
    if (!fp) {
        perror("Failed to open users file");
        return 0; // Deny access if file cannot be opened
    }

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        // Ignore empty lines and comments
        if (line[0] == '\n' || line[0] == '#') continue;

        char file_username[32], file_password[32];
        // Parse the line for username and password
        if (sscanf(line, "%31s %31s", file_username, file_password) == 2) {
            if (strcmp(username, file_username) == 0 && strcmp(password, file_password) == 0) {
                fclose(fp);
                return 1; // Valid credentials
            }
        }
    }

    fclose(fp);
    return 0; // Invalid credentials
}

// Function to register an agent
void register_agent(int socket, char* id) {
    pthread_mutex_lock(&agents_mutex);
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!agents[i].active) {
            agents[i].socket = socket;
            strncpy(agents[i].id, id, sizeof(agents[i].id) - 1);
            agents[i].id[sizeof(agents[i].id) - 1] = '\0'; // Ensure null-termination
            agents[i].active = 1;
            pthread_mutex_unlock(&agents_mutex);
            printf("Agent %s connected (socket %d)\n", agents[i].id, socket);
            return;
        }
    }
    pthread_mutex_unlock(&agents_mutex);
    printf("Maximum number of agents reached. Unable to register agent %s (socket %d)\n", id, socket);
}

// Function to remove an agent
void remove_agent(int socket) {
    pthread_mutex_lock(&agents_mutex);
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agents[i].active && agents[i].socket == socket) {
            printf("Removing agent %s (socket %d)\n", agents[i].id, socket);
            agents[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&agents_mutex);
    printf("Agent on socket %d disconnected\n", socket);
}

// Function to check privileges
int check_privileges(char* client_id, char* target_id) {
    FILE *fp = fopen(PRIVILEGES_FILE, "r");
    if (!fp) {
        perror("Failed to open privileges file");
        return 0; // Deny access if file cannot be opened
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        // Ignore empty lines and comments
        if (line[0] == '\n' || line[0] == '#') continue;

        char file_client_id[32], file_agent_id[32];
        // Parse the line for client_id and agent_id
        if (sscanf(line, "%31s %31s", file_client_id, file_agent_id) == 2) {
            if (strcmp(client_id, file_client_id) == 0 && strcmp(target_id, file_agent_id) == 0) {
                fclose(fp);
                return 1; // Privileged
            }
        }
    }

    fclose(fp);
    return 0; // Not privileged
}

// **Corrected Function:** Changed signature to accept socket as int
void handle_client_commands(int socket, char* username) {
    char buffer[BUFFER_SIZE];
    int n;

    // Handle client commands in a loop
    while (1) {
        n = recv(socket, buffer, BUFFER_SIZE - 1, 0);
        if (n <= 0) {
            if (n < 0) perror("Failed to receive command");
            else printf("Client %s disconnected.\n", username);
            close(socket);
            break;
        }
        buffer[n] = '\0';

        // Remove trailing newline characters
        buffer[strcspn(buffer, "\r\n")] = '\0';

        // Parse the command
        if (strncmp(buffer, "LIST", 4) == 0 && (buffer[4] == '\0' || buffer[4] == ' ')) {
            printf("User %s requested list of active agents.\n", username);
            pthread_mutex_lock(&agents_mutex);
            // Compile the list of active agents
            char list[BUFFER_SIZE];
            memset(list, 0, sizeof(list));
            strcat(list, "Agent List:\n");
            for (int i = 0; i < MAX_AGENTS; i++) {
                if (agents[i].active) {
                    strcat(list, agents[i].id);
                    strcat(list, "\n");
                }
            }
            pthread_mutex_unlock(&agents_mutex);

            // Send the list to the client
            if (send(socket, list, strlen(list), 0) < 0) {
                perror("Failed to send agent list to client");
            } else {
                printf("Sent list of active agents to user %s.\n", username);
            }
        }
        else if (strncmp(buffer, "SHUTDOWN ", 9) == 0) {
            char target_id[32];
            if (sscanf(buffer + 9, "%31s", target_id) != 1) {
                char *msg = "Invalid SHUTDOWN command format. Usage: SHUTDOWN <agent_id>\n";
                send(socket, msg, strlen(msg), 0);
                continue;
            }

            printf("User %s requested shutdown for agent %s\n", username, target_id);

            if (check_privileges(username, target_id)) {
                pthread_mutex_lock(&agents_mutex);
                int found = 0;
                for (int i = 0; i < MAX_AGENTS; i++) {
                    if (agents[i].active && strcmp(agents[i].id, target_id) == 0) {
                        if (send(agents[i].socket, "SHUTDOWN", 8, 0) < 0) {
                            perror("Failed to send SHUTDOWN command to agent");
                            char *error_msg = "Failed to send SHUTDOWN command to agent.\n";
                            send(socket, error_msg, strlen(error_msg), 0);
                        } else {
                            printf("Sent SHUTDOWN command to agent %s\n", target_id);
                            // Send confirmation to client
                            char confirmation[] = "Shutdown command sent successfully.\n";
                            send(socket, confirmation, strlen(confirmation), 0);
                        }
                        found = 1;
                        break;
                    }
                }
                pthread_mutex_unlock(&agents_mutex);

                if (!found) {
                    printf("Agent %s not found or inactive\n", target_id);
                    char not_found[] = "Agent not found or inactive.\n";
                    send(socket, not_found, strlen(not_found), 0);
                }
            } else {
                printf("Privileges check failed for user %s targeting %s\n", username, target_id);
                char no_privileges[] = "You do not have privileges to perform this action.\n";
                send(socket, no_privileges, strlen(no_privileges), 0);
            }
        }
        else if (strncmp(buffer, "EXIT", 4) == 0 && (buffer[4] == '\0' || buffer[4] == ' ')) {
            printf("User %s requested to close the connection.\n", username);
            char *msg = "Connection closed as per request.\n";
            send(socket, msg, strlen(msg), 0);
            close(socket);
            break;
        }
        else {
            char *msg = "Unknown command. Available commands: LIST, SHUTDOWN <agent_id>, EXIT\n";
            send(socket, msg, strlen(msg), 0);
        }
    }
}

// Thread function to handle each connection
void* handle_connection(void* arg) {
    int socket = *(int*)arg;
    free(arg); // Free the allocated memory once here
    char buffer[BUFFER_SIZE];
    int n;

    // Peek at the first byte to determine connection type
    n = recv(socket, buffer, 1, MSG_PEEK);
    if (n <= 0) {
        if (n < 0) perror("Failed to peek connection type");
        close(socket);
        return NULL;
    }

    if (buffer[0] == 'A') {
        // Agent connection
        // Receive the 'A' + agent_id message
        n = recv(socket, buffer, BUFFER_SIZE - 1, 0);
        if (n <= 0) {
            if (n < 0) perror("Failed to receive agent message");
            close(socket);
            return NULL;
        }
        buffer[n] = '\0';

        // Parse agent message
        if (buffer[0] != 'A') {
            printf("Invalid agent registration message.\n");
            close(socket);
            return NULL;
        }

        char agent_id[32];
        strncpy(agent_id, buffer + 1, sizeof(agent_id) - 1);
        agent_id[sizeof(agent_id) - 1] = '\0'; // Ensure null-termination

        register_agent(socket, agent_id);

        // Handle agent messages
        while (1) {
            n = recv(socket, buffer, BUFFER_SIZE - 1, 0);
            if (n <= 0) {
                if (n < 0) perror("recv failed for agent");
                remove_agent(socket);
                close(socket);
                break;
            }
            buffer[n] = '\0';

            printf("Received from agent %s: %s\n", agent_id, buffer);
        }
    }
    else {
        // Assume client connection requiring authentication
        // Receive the authentication message
        n = recv(socket, buffer, BUFFER_SIZE - 1, 0);
        if (n <= 0) {
            if (n < 0) perror("Failed to receive authentication message");
            close(socket);
            return NULL;
        }
        buffer[n] = '\0';

        // Parse the authentication message
        char command[16], username[32], password[32];
        if (sscanf(buffer, "%15s %31s %31s", command, username, password) != 3) {
            printf("Invalid authentication message format.\n");
            char *msg = "Authentication failed: Invalid format.\n";
            send(socket, msg, strlen(msg), 0);
            close(socket);
            return NULL;
        }

        if (strcmp(command, "LOGIN") != 0) {
            printf("Unknown authentication command: %s\n", command);
            char *msg = "Authentication failed: Unknown command.\n";
            send(socket, msg, strlen(msg), 0);
            close(socket);
            return NULL;
        }

        // Check credentials
        if (!check_credentials(username, password)) {
            printf("Authentication failed for user %s\n", username);
            char *msg = "Authentication failed: Invalid username or password.\n";
            send(socket, msg, strlen(msg), 0);
            close(socket);
            return NULL;
        }

        // Authentication successful
        printf("User %s authenticated successfully.\n", username);
        char *success_msg = "Authentication successful.\n";
        send(socket, success_msg, strlen(success_msg), 0);

        // Handle client commands
        handle_client_commands(socket, username);
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    int n;
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Failed to create socket");
        return 1;
    }

    // Initialize agents
    memset(agents, 0, sizeof(agents));

    // Set up signal handling
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Define server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Initialize to zero
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(1100);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket to the address and port
    n = bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (n < 0) {
        perror("Failed to bind to port");
        close(server_socket);
        return 1;
    }

    // Listen for incoming connections
    n = listen(server_socket, 10); // Increased backlog for more connections
    if (n < 0) {
        perror("Failed to listen on port");
        close(server_socket);
        return 1;
    }

    printf("Server listening on port 1100...\n");

    while (!stop_server) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);

        if (client_socket < 0) {
            if (stop_server) break; // Exit loop if stopping
            perror("Failed to accept connection");
            continue;
        }

        // Allocate memory for the client socket
        int *pclient = malloc(sizeof(int));
        if (pclient == NULL) {
            perror("Failed to allocate memory for client socket");
            close(client_socket);
            continue;
        }
        *pclient = client_socket;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_connection, pclient) != 0) {
            perror("Failed to create thread");
            free(pclient);
            close(client_socket);
            continue;
        }
        pthread_detach(thread);
    }

    printf("Shutting down server...\n");
    close(server_socket);

    // Close all active agent sockets
    pthread_mutex_lock(&agents_mutex);
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agents[i].active) {
            close(agents[i].socket);
            agents[i].active = 0;
            printf("Closed connection with agent %s\n", agents[i].id);
        }
    }
    pthread_mutex_unlock(&agents_mutex);

    printf("Server shutdown complete.\n");
    return 0;
}

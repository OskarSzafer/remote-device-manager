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

int check_privileges(char* client_id, char* target_id) {
    // Implement privilege checking here
    // Example: Only allow clients to control agents with the same prefix
    // Modify this logic based on your actual requirements
    if (strncmp(client_id, target_id, 3) == 0) {
        return 1;
    }
    return 1;
}

void* handle_connection(void* arg) {
    int socket = *(int*)arg;
    free(arg); // Free the allocated memory
    char buffer[BUFFER_SIZE];
    int n;

    // Receive the initial message (either 'A' for agent or 'C'/'L' for client)
    n = recv(socket, buffer, BUFFER_SIZE - 1, 0);
    if (n <= 0) {
        if (n < 0) perror("Failed to receive initial message");
        close(socket);
        return NULL;
    }
    buffer[n] = '\0';

    char type = buffer[0];
    if (type == 'C') { // Client Shutdown Command
        // The message format is 'C' + client_id + ' ' + target_agent_id
        char *space_ptr = strchr(buffer + 1, ' ');
        if (space_ptr == NULL) {
            printf("Invalid client message format.\n");
            close(socket);
            return NULL;
        }

        *space_ptr = '\0'; // Split the string into two parts
        char *client_id = buffer + 1;
        char *target_id = space_ptr + 1;

        printf("Client %s requested shutdown for agent %s\n", client_id, target_id);

        if (check_privileges(client_id, target_id)) {
            pthread_mutex_lock(&agents_mutex);
            int found = 0;
            for (int i = 0; i < MAX_AGENTS; i++) {
                if (agents[i].active && strcmp(agents[i].id, target_id) == 0) {
                    if (send(agents[i].socket, "SHUTDOWN", 8, 0) < 0) {
                        perror("Failed to send SHUTDOWN command to agent");
                    } else {
                        printf("Sent SHUTDOWN command to agent %s\n", target_id);
                        // Optionally, send confirmation to client
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
            printf("Privileges check failed for client %s targeting %s\n", client_id, target_id);
            char no_privileges[] = "You do not have privileges to perform this action.\n";
            send(socket, no_privileges, strlen(no_privileges), 0);
        }
        close(socket);
    }
    else if (type == 'L') { // Client List Request
        printf("Client requested list of active agents.\n");
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
            printf("Sent list of active agents to client.\n");
        }
        close(socket);
    }
    else if (type == 'A') { // Agent
        char id[32];
        strncpy(id, buffer +1, sizeof(id) -1);
        id[sizeof(id) -1] = '\0'; // Ensure null-termination

        register_agent(socket, id);
        while (1) {
            n = recv(socket, buffer, BUFFER_SIZE -1, 0);
            if (n <= 0) {
                if (n < 0) perror("recv failed for agent");
                remove_agent(socket);
                close(socket);
                break;
            }
            buffer[n] = '\0';
            // Optionally handle additional messages from the agent
            // For example, agent could send status updates
            printf("Received from agent %s: %s\n", id, buffer);
        }
    }
    else {
        printf("Unknown connection type: %c\n", type);
        close(socket);
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
    n = listen(server_socket, 5);
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
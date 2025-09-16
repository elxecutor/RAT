#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <errno.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
    #include <direct.h>
    #define PATH_MAX 260
#else
    #include <limits.h>
#endif

// Persistence method types
typedef enum {
    PERSISTENCE_NONE = 0,
    PERSISTENCE_SYSTEMD = 1,
    PERSISTENCE_CRON = 2,
    PERSISTENCE_AUTOSTART = 3,
    PERSISTENCE_REGISTRY = 4,
    PERSISTENCE_STARTUP_FOLDER = 5
} PersistenceMethod;

// Function declarations for Linux persistence methods
int install_systemd_service(const char *executable_path);
int install_cron_job(const char *executable_path);
int install_autostart_entry(const char *executable_path);
int remove_systemd_service(void);
int remove_cron_job(void);
int remove_autostart_entry(void);

// Function declarations for Windows persistence methods
#ifdef _WIN32
int install_registry_run(const char *executable_path);
int install_startup_folder(const char *executable_path);
int remove_registry_run(void);
int remove_startup_folder(void);
#endif

// Universal persistence functions
int install_persistence(PersistenceMethod method, const char *executable_path);
int remove_persistence(PersistenceMethod method);
int check_persistence_status(PersistenceMethod method);
const char* get_persistence_method_name(PersistenceMethod method);

// Automatic persistence functions
int install_automatic_persistence(void);
int is_persistence_needed(void);

// Utility functions
int copy_file(const char *src, const char *dest);
int file_exists(const char *path);
char* get_executable_path(void);
char* get_home_directory(void);

#endif // PERSISTENCE_H
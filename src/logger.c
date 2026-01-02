#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "logger.h"

#define MAX_MODULES 10
#define MODULE_NAME_LEN 20
static char enabled_modules[MAX_MODULES][MODULE_NAME_LEN];

static int is_module_enabled(const char* module) {
    for (int i = 0; i < MAX_MODULES; i++) {
        if (strcmp(enabled_modules[i], module) == 0) {
            return 1;
        }
    }
    return 0;
}

void enable_module(const char* module) {
    // prevent duplicates
    if (is_module_enabled(module)) {
        return;
    }
    for (int i = 0; i < MAX_MODULES; i++) {
        if (enabled_modules[i][0] == '\0') {
            strncpy(enabled_modules[i], module, MODULE_NAME_LEN - 1);
            enabled_modules[i][MODULE_NAME_LEN - 1] = '\0';
            break;
        }
    }
}

void disable_module(const char* module) {
    for (int i = 0; i < MAX_MODULES; i++) {
        if (strcmp(enabled_modules[i], module) == 0) {
            enabled_modules[i][0] = '\0';
            break;
        }
    }
}

void log_message(const char* module, const char* format, ...) {
    if (!is_module_enabled(module)) {
        return;
    }
    va_list args;
    va_start(args, format);
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
    if (strcmp(module, LOG_LEVEL_ERROR) == 0) {
        fprintf(stderr, "\033[1;31m"); // Red color for errors
        fprintf(stderr, "[%s] [%s] ", time_str, module);
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n\033[0m"); // Reset color
    } else {
        printf("[%s] [%s] ", time_str, module);
        vprintf(format, args);
        printf("\n");
    }
    va_end(args);
}

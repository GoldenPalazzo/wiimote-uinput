#ifndef _GLOGGER_H_
#define _GLOGGER_H_

#define LOG_LEVEL_INFO "INFO"
#define LOG_LEVEL_ERROR "ERROR"

#define LOG_INFO(format, ...) \
    log_message(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) \
    log_message(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)

void enable_module(const char *module);
void disable_module(const char *module);
void log_message(const char *module, const char *format, ...);
#endif

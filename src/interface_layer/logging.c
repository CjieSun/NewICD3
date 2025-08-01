#include "logging.h"
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

static log_level_t current_log_level = LOG_LEVEL_INFO;
static int logging_initialized = 0;

/* Level names for output */
static const char* level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

void log_init(void) {
    if (logging_initialized) {
        return;
    }
    
    /* Check environment variable for log level */
    const char* env_level = getenv("ICD3_LOG_LEVEL");
    if (env_level) {
        if (strcmp(env_level, "DEBUG") == 0) {
            current_log_level = LOG_LEVEL_DEBUG;
        } else if (strcmp(env_level, "INFO") == 0) {
            current_log_level = LOG_LEVEL_INFO;
        } else if (strcmp(env_level, "WARN") == 0) {
            current_log_level = LOG_LEVEL_WARN;
        } else if (strcmp(env_level, "ERROR") == 0) {
            current_log_level = LOG_LEVEL_ERROR;
        }
    }
    
    logging_initialized = 1;
}

void log_set_level(log_level_t level) {
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_ERROR) {
        current_log_level = level;
    }
}

log_level_t log_get_level(void) {
    return current_log_level;
}

void log_message(log_level_t level, const char *file, const char *func, const char *format, ...) {
    if (!logging_initialized) {
        log_init();
    }
    
    /* Filter by log level */
    if (level < current_log_level) {
        return;
    }
    
    /* Get timestamp */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    
    /* Print timestamp, level, location, and message */
    printf("[%02d:%02d:%02d.%03ld] [%s] [%s:%s] ",
           tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
           tv.tv_usec / 1000,
           level_names[level],
           file, func);
    
    /* Print the actual message */
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
}
#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <time.h>
#include <string.h>

/**
 * @file logging.h
 * @brief Simple logging system with level control and source location
 * 
 * Features:
 * - Log levels: DEBUG, INFO, WARN, ERROR
 * - Source file and function information
 * - Runtime level control via environment variable ICD3_LOG_LEVEL
 * - Timestamp in log messages
 */

/* Log levels */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

/* Function declarations */
void log_init(void);
void log_set_level(log_level_t level);
log_level_t log_get_level(void);
void log_message(log_level_t level, const char *file, const char *func, const char *format, ...);

/* Extract filename from full path */
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

/* Convenience macros */
#define LOG_DEBUG(fmt, ...) log_message(LOG_LEVEL_DEBUG, __FILENAME__, __func__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_message(LOG_LEVEL_INFO,  __FILENAME__, __func__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_message(LOG_LEVEL_WARN,  __FILENAME__, __func__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_message(LOG_LEVEL_ERROR, __FILENAME__, __func__, fmt, ##__VA_ARGS__)

#endif /* LOGGING_H */
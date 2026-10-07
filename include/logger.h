#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>

typedef struct {
    int fd;
    unsigned delay_ms;
} Logger;

int logger_open(Logger *logger, const char *path, unsigned delay_ms);
void logger_close(Logger *logger);
void logger_event(Logger *logger, uint64_t model_time, const char *format, ...);
void logger_line(Logger *logger, const char *format, ...);

#endif

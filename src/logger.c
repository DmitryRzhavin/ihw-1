#define _POSIX_C_SOURCE 200809L

#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int write_all(int fd, const char *data, size_t size)
{
    size_t written = 0;

    while (written < size) {
        ssize_t result = write(fd, data + written, size - written);
        if (result > 0) {
            written += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static void pause_ms(unsigned delay_ms)
{
    struct timespec req;
    struct timespec rem;

    if (delay_ms == 0)
        return;

    req.tv_sec = (time_t)(delay_ms / 1000U);
    req.tv_nsec = (long)(delay_ms % 1000U) * 1000000L;

    (void)rem;
    if (nanosleep(&req, NULL) != 0 && errno != EINTR)
        return;
}

int logger_open(Logger *logger, const char *path, unsigned delay_ms)
{
    if (logger == NULL)
        return -1;

    logger->fd = -1;
    logger->delay_ms = delay_ms;

    if (path == NULL)
        return 0;

    logger->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logger->fd < 0) {
        perror(path);
        return -1;
    }
    return 0;
}

void logger_close(Logger *logger)
{
    if (logger != NULL && logger->fd >= 0) {
        (void)close(logger->fd);
        logger->fd = -1;
    }
}

static void emit(Logger *logger, const char *text, size_t length, int delay)
{
    (void)write_all(STDOUT_FILENO, text, length);
    if (logger != NULL && logger->fd >= 0)
        (void)write_all(logger->fd, text, length);
    if (delay && logger != NULL)
        pause_ms(logger->delay_ms);
}

void logger_event(Logger *logger, uint64_t model_time, const char *format, ...)
{
    char message[768];
    char line[896];
    va_list args;
    int message_len;
    int line_len;

    va_start(args, format);
    message_len = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (message_len < 0)
        return;

    line_len = snprintf(line, sizeof(line), "[%6llu] %s\n",
                        (unsigned long long)model_time, message);
    if (line_len < 0)
        return;
    if ((size_t)line_len >= sizeof(line))
        line_len = (int)sizeof(line) - 1;

    emit(logger, line, (size_t)line_len, 1);
}

void logger_line(Logger *logger, const char *format, ...)
{
    char line[1024];
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    if (length < 0)
        return;
    if ((size_t)length >= sizeof(line))
        length = (int)sizeof(line) - 1;

    emit(logger, line, (size_t)length, 0);
}

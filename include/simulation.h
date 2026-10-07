#ifndef SIMULATION_H
#define SIMULATION_H

#include "model.h"

#include <signal.h>

typedef struct {
    const char *log_path;              /* NULL => лог-файл отключен */
    unsigned delay_ms;                 /* задержка после события */
    const volatile sig_atomic_t *stop_flag;
} SimulationOptions;

typedef enum {
    SIMULATION_OK = 0,
    SIMULATION_ERROR = 1,
    SIMULATION_INTERRUPTED = 2
} SimulationResult;

SimulationResult simulation_run(const Config *config,
                                const SimulationOptions *options);

#endif

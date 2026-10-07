#include "config.h"
#include "simulation.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OPT_VISITORS,
    OPT_WAITERS,
    OPT_COOKS,
    OPT_ARRIVAL_INTERVAL,
    OPT_DISHES_PER_ORDER,
    OPT_STOVES,
    OPT_OVENS,
    OPT_GRILLS,
    OPT_TABLES,
    OPT_COUNT
} Option;

static const char *option_names[OPT_COUNT] = {
    "--visitors",
    "--waiters",
    "--cooks",
    "--arrival-interval",
    "--dishes-per-order",
    "--stoves",
    "--ovens",
    "--grills",
    "--tables"
};

static int parse_unsigned(const char *text, unsigned *result)
{
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0' || *text == '-')
        return -1;

    errno = 0;
    value = strtoul(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || value > UINT_MAX)
        return -1;

    *result = (unsigned)value;
    return 0;
}

static void print_usage(const char *program)
{
    printf(
        "Usage: %s [--config FILE] [options]\\n"
        "Without --config, parameters are entered interactively.\\n"
        "Options override values loaded from the configuration file:\\n"
        "  --visitors N\\n"
        "  --waiters N\\n"
        "  --cooks N\\n"
        "  --arrival-interval N\\n"
        "  --dishes-per-order N\\n"
        "  --stoves N --ovens N --grills N --tables N\\n"
        "  --help\\n",
        program
    );
}

static int find_option(const char *name)
{
    unsigned i;

    for (i = 0; i < OPT_COUNT; ++i) {
        if (strcmp(name, option_names[i]) == 0)
            return (int)i;
    }

    return -1;
}

static void apply_option(Config *config, Option option, unsigned value)
{
    switch (option) {
    case OPT_VISITORS:
        config->visitor_count = value;
        break;
    case OPT_WAITERS:
        config->waiter_count = value;
        break;
    case OPT_COOKS:
        config->cook_count = value;
        break;
    case OPT_ARRIVAL_INTERVAL:
        config->arrival_interval = value;
        break;
    case OPT_DISHES_PER_ORDER:
        config->dishes_per_order = value;
        break;
    case OPT_STOVES:
        config->equipment_count[EQUIPMENT_STOVE] = value;
        break;
    case OPT_OVENS:
        config->equipment_count[EQUIPMENT_OVEN] = value;
        break;
    case OPT_GRILLS:
        config->equipment_count[EQUIPMENT_GRILL] = value;
        break;
    case OPT_TABLES:
        config->equipment_count[EQUIPMENT_TABLE] = value;
        break;
    case OPT_COUNT:
        break;
    }
}

int main(int argc, char **argv)
{
    const char *config_path = NULL;
    unsigned overrides[OPT_COUNT] = {0};
    int has_override[OPT_COUNT] = {0};
    Config config;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--config requires a file path\\n");
                return 2;
            }
            config_path = argv[++i];
            continue;
        }

        int option = find_option(argv[i]);
        if (option < 0) {
            fprintf(stderr, "Unknown argument: %s\\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }

        if (i + 1 >= argc ||
            parse_unsigned(argv[i + 1], &overrides[option]) != 0) {
            fprintf(stderr, "Invalid value for %s\\n", argv[i]);
            return 2;
        }

        has_override[option] = 1;
        ++i;
    }

    if (config_path != NULL) {
        if (config_load_file(config_path, &config) != 0)
            return 1;
    } else {
        if (config_read_interactive(&config) != 0)
            return 1;
    }

    for (i = 0; i < OPT_COUNT; ++i) {
        if (has_override[i])
            apply_option(&config, (Option)i, overrides[i]);
    }

    if (config_validate(&config) != 0)
        return 1;

    return simulation_run(&config) == 0 ? 0 : 1;
}

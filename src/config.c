#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text)
{
    char *end;

    while (isspace((unsigned char)*text))
        ++text;

    if (*text == '\0')
        return text;

    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end))
        --end;

    end[1] = '\0';
    return text;
}

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

static int equipment_from_name(const char *name, EquipmentType *equipment)
{
    if (strcmp(name, "stove") == 0)
        *equipment = EQUIPMENT_STOVE;
    else if (strcmp(name, "oven") == 0)
        *equipment = EQUIPMENT_OVEN;
    else if (strcmp(name, "grill") == 0)
        *equipment = EQUIPMENT_GRILL;
    else if (strcmp(name, "table") == 0)
        *equipment = EQUIPMENT_TABLE;
    else
        return -1;

    return 0;
}

static const char *equipment_name(EquipmentType equipment)
{
    static const char *names[EQUIPMENT_TYPES] = {
        "stove", "oven", "grill", "table"
    };

    if ((unsigned)equipment >= EQUIPMENT_TYPES)
        return "unknown";

    return names[equipment];
}

static int parse_dish(char *value, Config *config)
{
    char *separator = strchr(value, '|');
    char *name;
    char *operation_text;
    char *token;

    if (separator == NULL || config->menu_size >= MAX_DISHES)
        return -1;

    *separator = '\0';
    name = trim(value);
    operation_text = trim(separator + 1);

    if (*name == '\0' || strlen(name) >= MAX_NAME_LENGTH ||
        *operation_text == '\0')
        return -1;

    DishSpec *dish = &config->menu[config->menu_size];
    memset(dish, 0, sizeof(*dish));
    strcpy(dish->name, name);

    token = strtok(operation_text, ",");
    while (token != NULL) {
        char *colon = strchr(token, ':');
        char *equipment_text;
        char *duration_text;
        EquipmentType equipment;
        unsigned duration;

        if (colon == NULL || dish->operation_count >= MAX_STEPS_PER_DISH)
            return -1;

        *colon = '\0';
        equipment_text = trim(token);
        duration_text = trim(colon + 1);

        if (equipment_from_name(equipment_text, &equipment) != 0 ||
            parse_unsigned(duration_text, &duration) != 0 ||
            duration == 0)
            return -1;

        OperationSpec *operation =
            &dish->operations[dish->operation_count++];
        operation->equipment = equipment;
        operation->duration = duration;

        token = strtok(NULL, ",");
    }

    if (dish->operation_count == 0)
        return -1;

    ++config->menu_size;
    return 0;
}

static int parse_order(char *value, Config *config)
{
    char *token;
    unsigned count = 0;
    unsigned visitor_index = config->order_count;

    if (visitor_index >= MAX_VISITORS)
        return -1;

    token = strtok(value, ",");
    while (token != NULL) {
        unsigned dish_index;
        char *name = trim(token);
        int found = 0;

        if (count >= MAX_DISHES_PER_ORDER)
            return -1;

        for (dish_index = 0; dish_index < config->menu_size; ++dish_index) {
            if (strcmp(name, config->menu[dish_index].name) == 0) {
                config->order_dishes[visitor_index][count++] = dish_index;
                found = 1;
                break;
            }
        }

        if (!found)
            return -1;

        token = strtok(NULL, ",");
    }

    if (count == 0)
        return -1;

    config->order_dish_count[visitor_index] = count;
    ++config->order_count;
    return 0;
}

static int set_config_value(Config *config, char *key, char *value)
{
    unsigned number;

    if (strcmp(key, "dish") == 0)
        return parse_dish(value, config);

    if (strcmp(key, "order") == 0)
        return parse_order(value, config);

    if (parse_unsigned(value, &number) != 0)
        return -1;

    if (strcmp(key, "visitors") == 0)
        config->visitor_count = number;
    else if (strcmp(key, "waiters") == 0)
        config->waiter_count = number;
    else if (strcmp(key, "cooks") == 0)
        config->cook_count = number;
    else if (strcmp(key, "arrival_interval") == 0)
        config->arrival_interval = number;
    else if (strcmp(key, "kitchen_queue_capacity") == 0)
        config->kitchen_queue_capacity = number;
    else if (strcmp(key, "ready_queue_capacity") == 0)
        config->ready_queue_capacity = number;
    else if (strcmp(key, "dishes_per_order") == 0)
        config->dishes_per_order = number;
    else if (strcmp(key, "equipment_stove") == 0)
        config->equipment_count[EQUIPMENT_STOVE] = number;
    else if (strcmp(key, "equipment_oven") == 0)
        config->equipment_count[EQUIPMENT_OVEN] = number;
    else if (strcmp(key, "equipment_grill") == 0)
        config->equipment_count[EQUIPMENT_GRILL] = number;
    else if (strcmp(key, "equipment_table") == 0)
        config->equipment_count[EQUIPMENT_TABLE] = number;
    else
        return -1;

    return 0;
}

int config_load_file(const char *path, Config *config)
{
    FILE *file;
    char line[512];
    unsigned line_number = 0;

    if (path == NULL || config == NULL)
        return -1;

    file = fopen(path, "r");
    if (file == NULL) {
        perror(path);
        return -1;
    }

    memset(config, 0, sizeof(*config));

    while (fgets(line, sizeof(line), file) != NULL) {
        char *text;
        char *equals;
        char *key;
        char *value;

        ++line_number;
        text = trim(line);

        if (*text == '\0' || *text == '#')
            continue;

        equals = strchr(text, '=');
        if (equals == NULL) {
            fprintf(stderr, "%s:%u: expected key=value\n", path, line_number);
            fclose(file);
            return -1;
        }

        *equals = '\0';
        key = trim(text);
        value = trim(equals + 1);

        if (set_config_value(config, key, value) != 0) {
            fprintf(stderr, "%s:%u: invalid setting '%s'\n",
                    path, line_number, key);
            fclose(file);
            return -1;
        }
    }

    if (ferror(file)) {
        perror(path);
        fclose(file);
        return -1;
    }

    fclose(file);
    return config_validate(config);
}

int config_validate(const Config *config)
{
    unsigned i;
    unsigned j;

    if (config == NULL ||
        config->visitor_count == 0 ||
        config->visitor_count > MAX_VISITORS ||
        config->waiter_count == 0 ||
        config->cook_count == 0 ||
        config->arrival_interval == 0 ||
        config->menu_size == 0 ||
        config->menu_size > MAX_DISHES ||
        config->dishes_per_order == 0 ||
        config->dishes_per_order > MAX_DISHES_PER_ORDER ||
        config->dishes_per_order > config->menu_size) {
        fprintf(stderr, "Invalid general configuration values\n");
        return -1;
    }

    if (config->order_count != 0 &&
        config->order_count != config->visitor_count) {
        fprintf(stderr, "Provide exactly one order per visitor, or no order lines\\n");
        return -1;
    }

    for (i = 0; i < config->order_count; ++i) {
        unsigned j;

        if (config->order_dish_count[i] == 0 ||
            config->order_dish_count[i] > MAX_DISHES_PER_ORDER) {
            fprintf(stderr, "Invalid order for visitor %u\\n", i + 1);
            return -1;
        }

        if (config->order_dish_count[i] != config->dishes_per_order) {
            fprintf(stderr,
                    "Order for visitor %u has %u dishes; expected %u\n",
                    i + 1,
                    config->order_dish_count[i],
                    config->dishes_per_order);
            return -1;
        }

        for (j = 0; j < config->order_dish_count[i]; ++j) {
            if (config->order_dishes[i][j] >= config->menu_size) {
                fprintf(stderr, "Invalid dish in order for visitor %u\\n",
                        i + 1);
                return -1;
            }
        }
    }

    for (i = 0; i < config->menu_size; ++i) {
        const DishSpec *dish = &config->menu[i];

        if (dish->name[0] == '\0' ||
            dish->operation_count == 0 ||
            dish->operation_count > MAX_STEPS_PER_DISH) {
            fprintf(stderr, "Invalid dish at menu index %u\n", i);
            return -1;
        }

        for (j = 0; j < dish->operation_count; ++j) {
            const OperationSpec *operation = &dish->operations[j];
            unsigned equipment = (unsigned)operation->equipment;

            if (equipment >= EQUIPMENT_TYPES ||
                operation->duration == 0 ||
                config->equipment_count[equipment] == 0) {
                fprintf(stderr,
                        "Dish '%s' uses invalid or unavailable equipment\n",
                        dish->name);
                return -1;
            }
        }
    }

    return 0;
}

static int read_line(const char *prompt, char *buffer, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, (int)size, stdin) == NULL)
        return -1;

    buffer[strcspn(buffer, "\n")] = '\0';
    return 0;
}

static int read_number(const char *prompt, unsigned *value)
{
    char buffer[128];
    char *text;

    if (read_line(prompt, buffer, sizeof(buffer)) != 0)
        return -1;

    text = trim(buffer);
    return parse_unsigned(text, value);
}

int config_read_interactive(Config *config)
{
    unsigned i;
    unsigned j;
    unsigned choice;
    char prompt[128];

    if (config == NULL)
        return -1;

    memset(config, 0, sizeof(*config));

    if (read_number("Number of visitors: ", &config->visitor_count) != 0 ||
        read_number("Number of waiters: ", &config->waiter_count) != 0 ||
        read_number("Number of cooks: ", &config->cook_count) != 0 ||
        read_number("Arrival interval: ", &config->arrival_interval) != 0 ||
        read_number("Kitchen queue capacity (0 = unlimited): ",
                    &config->kitchen_queue_capacity) != 0 ||
        read_number("Ready-dish queue capacity (0 = unlimited): ",
                    &config->ready_queue_capacity) != 0 ||
        read_number("Dishes in each order: ", &config->dishes_per_order) != 0) {
        fprintf(stderr, "Invalid numeric input\n");
        return -1;
    }

    for (i = 0; i < EQUIPMENT_TYPES; ++i) {
        snprintf(prompt, sizeof(prompt), "Number of %s units: ",
                 equipment_name((EquipmentType)i));
        if (read_number(prompt, &config->equipment_count[i]) != 0) {
            fprintf(stderr, "Invalid equipment count\n");
            return -1;
        }
    }

    if (read_number("Number of dishes in menu: ", &config->menu_size) != 0 ||
        config->menu_size > MAX_DISHES) {
        fprintf(stderr, "Invalid menu size (maximum %d)\n", MAX_DISHES);
        return -1;
    }

    for (i = 0; i < config->menu_size; ++i) {
        DishSpec *dish = &config->menu[i];

        snprintf(prompt, sizeof(prompt), "Name of dish %u: ", i + 1);
        if (read_line(prompt, dish->name, sizeof(dish->name)) != 0)
            return -1;

        snprintf(prompt, sizeof(prompt),
                 "Number of operations for '%s': ", dish->name);
        if (read_number(prompt, &dish->operation_count) != 0 ||
            dish->operation_count > MAX_STEPS_PER_DISH) {
            fprintf(stderr, "Invalid operation count\n");
            return -1;
        }

        for (j = 0; j < dish->operation_count; ++j) {
            OperationSpec *operation = &dish->operations[j];

            printf("Operation %u equipment (1=stove, 2=oven, "
                   "3=grill, 4=table): ", j + 1);
            if (read_number("", &choice) != 0 || choice < 1 ||
                choice > EQUIPMENT_TYPES) {
                fprintf(stderr, "Invalid equipment choice\n");
                return -1;
            }

            operation->equipment = (EquipmentType)(choice - 1);

            snprintf(prompt, sizeof(prompt),
                     "Operation %u duration: ", j + 1);
            if (read_number(prompt, &operation->duration) != 0)
                return -1;
        }
    }

    return config_validate(config);
}

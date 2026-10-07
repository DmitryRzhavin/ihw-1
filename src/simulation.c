#include "simulation.h"

#include "config.h"
#include "logger.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_OPERATIONS \
    (MAX_VISITORS * MAX_DISHES_PER_ORDER * MAX_STEPS_PER_DISH)

typedef enum {
    OP_BLOCKED,
    OP_WAITING_CAPACITY,
    OP_QUEUED,
    OP_RUNNING,
    OP_DONE
} OperationState;

typedef struct {
    unsigned order_id;
    unsigned dish_number;
    unsigned step_number;
    EquipmentType equipment;
    unsigned duration;
    OperationState state;
    unsigned cook_id;
    uint64_t finish_time;
    uint64_t queued_at;
    uint64_t queue_sequence;
} RuntimeOperation;

typedef struct {
    unsigned menu_index;
    char name[MAX_NAME_LENGTH];
    unsigned operation_count;
    unsigned operation_ids[MAX_STEPS_PER_DISH];
    unsigned completed_operations;
    int ready;
    int in_ready_queue;
    int waiting_for_ready_capacity;
    int collected;
    uint64_t ready_sequence;
} RuntimeDish;

typedef struct {
    unsigned visitor_id;
    unsigned waiter_id;
    unsigned dish_count;
    RuntimeDish dishes[MAX_DISHES_PER_ORDER];
    int accepted;
    int canceled;
    int delivered;
} RuntimeOrder;

typedef struct {
    unsigned visitors_arrived;
    unsigned orders_accepted;
    unsigned orders_delivered;
    unsigned orders_canceled;
    unsigned dishes_created;
    unsigned dishes_ready;
    unsigned operations_created;
    unsigned operations_started;
    unsigned operations_completed;
    uint64_t total_queue_wait;
    uint64_t cook_busy_time;
    uint64_t equipment_busy_time[EQUIPMENT_TYPES];
} Statistics;

static int stop_requested(const SimulationOptions *options)
{
    return options != NULL && options->stop_flag != NULL &&
           *options->stop_flag != 0;
}

static uint32_t rng_next(uint32_t *state)
{
    /* xorshift32: простой воспроизводимый ГСЧ для имитационной модели. */
    uint32_t x = *state;
    if (x == 0U)
        x = 0xA341316CU;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint64_t arrival_time_for(const Config *config, unsigned visitor_index)
{
    uint64_t time = 0;
    unsigned i;

    if (config->arrival_gap_count == 0)
        return (uint64_t)visitor_index * config->arrival_interval;

    for (i = 0; i <= visitor_index; ++i)
        time += config->arrival_gaps[i];
    return time;
}

static void assign_order_dishes(const Config *config,
                                unsigned visitor_index,
                                RuntimeOrder *order,
                                uint32_t *rng_state)
{
    unsigned i;

    if (config->order_count != 0) {
        order->dish_count = config->order_dish_count[visitor_index];
        for (i = 0; i < order->dish_count; ++i)
            order->dishes[i].menu_index =
                config->order_dishes[visitor_index][i];
        return;
    }

    {
        unsigned pool[MAX_DISHES];
        for (i = 0; i < config->menu_size; ++i)
            pool[i] = i;

        order->dish_count = config->dishes_per_order;
        for (i = 0; i < order->dish_count; ++i) {
            unsigned remaining = config->menu_size - i;
            unsigned pick = i + (rng_next(rng_state) % remaining);
            unsigned tmp = pool[i];
            pool[i] = pool[pick];
            pool[pick] = tmp;
            order->dishes[i].menu_index = pool[i];
        }
    }
}

static int find_free_cook(const int *cook_busy, unsigned cook_limit)
{
    unsigned i;
    for (i = 0; i < cook_limit; ++i) {
        if (!cook_busy[i])
            return (int)i;
    }
    return -1;
}

static int operation_is_better(const RuntimeOperation *candidate,
                               const RuntimeOperation *selected,
                               OrderPriority priority)
{
    if (selected == NULL)
        return 1;

    if (candidate->order_id != selected->order_id) {
        if (priority == PRIORITY_FIFO)
            return candidate->order_id < selected->order_id;
        return candidate->order_id > selected->order_id;
    }

    return candidate->queue_sequence < selected->queue_sequence;
}

static int find_next_operation(const RuntimeOperation *operations,
                               unsigned operation_count,
                               const unsigned *equipment_busy,
                               const Config *config)
{
    int selected = -1;
    unsigned i;

    for (i = 0; i < operation_count; ++i) {
        unsigned equipment;

        if (operations[i].state != OP_QUEUED)
            continue;

        equipment = (unsigned)operations[i].equipment;
        if (equipment_busy[equipment] >= config->equipment_count[equipment])
            continue;

        if (operation_is_better(&operations[i],
                                selected < 0 ? NULL : &operations[selected],
                                config->priority))
            selected = (int)i;
    }
    return selected;
}

static unsigned queued_operation_count(const RuntimeOperation *operations,
                                        unsigned operation_count)
{
    unsigned count = 0;
    unsigned i;
    for (i = 0; i < operation_count; ++i) {
        if (operations[i].state == OP_QUEUED)
            ++count;
    }
    return count;
}

static void enqueue_operation(RuntimeOperation *operations,
                              const RuntimeOrder *orders,
                              unsigned operation_id,
                              unsigned operation_count,
                              const Config *config,
                              uint64_t current_time,
                              uint64_t *next_queue_sequence,
                              Logger *logger)
{
    RuntimeOperation *operation = &operations[operation_id];
    const RuntimeDish *dish =
        &orders[operation->order_id - 1].dishes[operation->dish_number - 1];

    operation->queued_at = current_time;
    operation->queue_sequence = ++*next_queue_sequence;

    if (config->kitchen_queue_capacity == 0 ||
        queued_operation_count(operations, operation_count) <
            config->kitchen_queue_capacity) {
        operation->state = OP_QUEUED;
        logger_event(logger, current_time,
                     "Coordinator queued operation %u of dish '%s' for order %u",
                     operation->step_number, dish->name, operation->order_id);
    } else {
        operation->state = OP_WAITING_CAPACITY;
        logger_event(logger, current_time,
                     "Kitchen queue full; operation %u of dish '%s' waits "
                     "for order %u",
                     operation->step_number, dish->name, operation->order_id);
    }
}

static void promote_waiting_operations(RuntimeOperation *operations,
                                       const RuntimeOrder *orders,
                                       unsigned operation_count,
                                       const Config *config,
                                       uint64_t current_time,
                                       Logger *logger)
{
    for (;;) {
        int selected = -1;
        unsigned i;

        if (config->kitchen_queue_capacity != 0 &&
            queued_operation_count(operations, operation_count) >=
                config->kitchen_queue_capacity)
            return;

        for (i = 0; i < operation_count; ++i) {
            if (operations[i].state != OP_WAITING_CAPACITY)
                continue;
            if (operation_is_better(
                    &operations[i],
                    selected < 0 ? NULL : &operations[(unsigned)selected],
                    config->priority))
                selected = (int)i;
        }

        if (selected < 0)
            return;

        operations[(unsigned)selected].state = OP_QUEUED;
        {
            RuntimeOperation *operation = &operations[(unsigned)selected];
            const RuntimeDish *dish =
                &orders[operation->order_id - 1]
                     .dishes[operation->dish_number - 1];
            logger_event(logger, current_time,
                         "Coordinator moved waiting operation %u of dish '%s' "
                         "into kitchen queue for order %u",
                         operation->step_number, dish->name,
                         operation->order_id);
        }
    }
}

static int order_is_possible(const Config *config,
                             const RuntimeOrder *order,
                             EquipmentType *missing_equipment,
                             const char **dish_name)
{
    unsigned i;
    unsigned j;

    for (i = 0; i < order->dish_count; ++i) {
        const DishSpec *spec = &config->menu[order->dishes[i].menu_index];
        for (j = 0; j < spec->operation_count; ++j) {
            EquipmentType equipment = spec->operations[j].equipment;
            if (config->equipment_count[(unsigned)equipment] == 0) {
                *missing_equipment = equipment;
                *dish_name = spec->name;
                return 0;
            }
        }
    }
    return 1;
}

static int accept_waiting_orders(RuntimeOrder *orders,
                                 unsigned arrived_visitors,
                                 int *waiter_busy,
                                 unsigned waiter_limit,
                                 RuntimeOperation *operations,
                                 unsigned *operation_count,
                                 uint64_t *next_queue_sequence,
                                 unsigned *finished_orders,
                                 const Config *config,
                                 Statistics *stats,
                                 uint64_t current_time,
                                 Logger *logger)
{
    unsigned visitor_index;

    for (visitor_index = 0; visitor_index < arrived_visitors; ++visitor_index) {
        RuntimeOrder *order = &orders[visitor_index];
        unsigned waiter_index;
        unsigned dish_index;

        if (order->accepted || order->canceled || order->delivered)
            continue;

        for (waiter_index = 0; waiter_index < waiter_limit; ++waiter_index) {
            if (!waiter_busy[waiter_index])
                break;
        }
        if (waiter_index == waiter_limit)
            return 0;

        order->accepted = 1;
        order->waiter_id = waiter_index + 1;
        waiter_busy[waiter_index] = 1;
        ++stats->orders_accepted;

        logger_event(logger, current_time,
                     "Waiter %u accepted order %u from visitor %u",
                     order->waiter_id, order->visitor_id, order->visitor_id);

        {
            EquipmentType missing = EQUIPMENT_STOVE;
            const char *missing_dish = NULL;
            if (!order_is_possible(config, order, &missing, &missing_dish)) {
                order->canceled = 1;
                waiter_busy[waiter_index] = 0;
                ++stats->orders_canceled;
                ++*finished_orders;
                logger_event(logger, current_time,
                             "Coordinator canceled order %u: dish '%s' requires "
                             "unavailable %s equipment",
                             order->visitor_id, missing_dish,
                             config_equipment_name(missing));
                logger_event(logger, current_time,
                             "Visitor %u leaves without service after explicit "
                             "order cancellation",
                             order->visitor_id);
                continue;
            }
        }

        for (dish_index = 0; dish_index < order->dish_count; ++dish_index) {
            RuntimeDish *dish = &order->dishes[dish_index];
            const DishSpec *spec = &config->menu[dish->menu_index];
            unsigned step;

            (void)snprintf(dish->name, sizeof(dish->name), "%s", spec->name);
            dish->operation_count = spec->operation_count;
            ++stats->dishes_created;

            logger_event(logger, current_time,
                         "Coordinator created dish '%s' for order %u",
                         dish->name, order->visitor_id);

            for (step = 0; step < spec->operation_count; ++step) {
                RuntimeOperation *operation;

                if (*operation_count >= MAX_OPERATIONS) {
                    fprintf(stderr, "Too many operations in simulation\n");
                    return -1;
                }

                operation = &operations[*operation_count];
                memset(operation, 0, sizeof(*operation));
                operation->order_id = order->visitor_id;
                operation->dish_number = dish_index + 1;
                operation->step_number = step + 1;
                operation->equipment = spec->operations[step].equipment;
                operation->duration = spec->operations[step].duration;
                operation->state = OP_BLOCKED;

                dish->operation_ids[step] = *operation_count;
                ++*operation_count;
                ++stats->operations_created;

                logger_event(logger, current_time,
                             "Coordinator created operation %u (%s, %u time "
                             "unit%s) for dish '%s', order %u",
                             step + 1,
                             config_equipment_name(operation->equipment),
                             operation->duration,
                             operation->duration == 1 ? "" : "s",
                             dish->name, order->visitor_id);
            }

            enqueue_operation(operations, orders, dish->operation_ids[0],
                              *operation_count, config, current_time,
                              next_queue_sequence, logger);
        }
    }

    return 0;
}

static int order_is_collected(const RuntimeOrder *order)
{
    unsigned i;
    for (i = 0; i < order->dish_count; ++i) {
        if (!order->dishes[i].collected)
            return 0;
    }
    return 1;
}

static void process_ready_dishes(RuntimeOrder *orders,
                                 unsigned arrived_visitors,
                                 int *waiter_busy,
                                 unsigned ready_queue_capacity,
                                 unsigned *finished_orders,
                                 Statistics *stats,
                                 uint64_t current_time,
                                 Logger *logger)
{
    for (;;) {
        unsigned queued_count = 0;
        unsigned order_index;
        unsigned dish_index;
        unsigned selected_order = MAX_VISITORS;
        unsigned selected_dish = MAX_DISHES_PER_ORDER;
        uint64_t selected_sequence = UINT64_MAX;

        for (order_index = 0; order_index < arrived_visitors; ++order_index) {
            RuntimeOrder *order = &orders[order_index];
            for (dish_index = 0; dish_index < order->dish_count; ++dish_index) {
                RuntimeDish *dish = &order->dishes[dish_index];
                if (dish->in_ready_queue)
                    ++queued_count;
                if (dish->ready && !dish->collected && !dish->in_ready_queue &&
                    dish->ready_sequence < selected_sequence) {
                    selected_order = order_index;
                    selected_dish = dish_index;
                    selected_sequence = dish->ready_sequence;
                }
            }
        }

        while (selected_order != MAX_VISITORS &&
               (ready_queue_capacity == 0 ||
                queued_count < ready_queue_capacity)) {
            RuntimeOrder *order = &orders[selected_order];
            RuntimeDish *dish = &order->dishes[selected_dish];

            dish->in_ready_queue = 1;
            dish->waiting_for_ready_capacity = 0;
            ++queued_count;
            logger_event(logger, current_time,
                         "Coordinator placed dish '%s' in ready area for order %u",
                         dish->name, order->visitor_id);

            selected_order = MAX_VISITORS;
            selected_dish = MAX_DISHES_PER_ORDER;
            selected_sequence = UINT64_MAX;

            for (order_index = 0; order_index < arrived_visitors; ++order_index) {
                RuntimeOrder *candidate_order = &orders[order_index];
                for (dish_index = 0; dish_index < candidate_order->dish_count;
                     ++dish_index) {
                    RuntimeDish *candidate =
                        &candidate_order->dishes[dish_index];
                    if (candidate->ready && !candidate->collected &&
                        !candidate->in_ready_queue &&
                        candidate->ready_sequence < selected_sequence) {
                        selected_order = order_index;
                        selected_dish = dish_index;
                        selected_sequence = candidate->ready_sequence;
                    }
                }
            }
        }

        if (ready_queue_capacity != 0 &&
            queued_count >= ready_queue_capacity) {
            for (order_index = 0; order_index < arrived_visitors; ++order_index) {
                RuntimeOrder *order = &orders[order_index];
                for (dish_index = 0; dish_index < order->dish_count; ++dish_index) {
                    RuntimeDish *dish = &order->dishes[dish_index];
                    if (dish->ready && !dish->collected &&
                        !dish->in_ready_queue &&
                        !dish->waiting_for_ready_capacity) {
                        dish->waiting_for_ready_capacity = 1;
                        logger_event(logger, current_time,
                                     "Ready area full; dish '%s' waits for "
                                     "order %u",
                                     dish->name, order->visitor_id);
                    }
                }
            }
        }

        selected_order = MAX_VISITORS;
        selected_dish = MAX_DISHES_PER_ORDER;
        selected_sequence = UINT64_MAX;

        for (order_index = 0; order_index < arrived_visitors; ++order_index) {
            RuntimeOrder *order = &orders[order_index];
            for (dish_index = 0; dish_index < order->dish_count; ++dish_index) {
                RuntimeDish *dish = &order->dishes[dish_index];
                if (dish->in_ready_queue &&
                    dish->ready_sequence < selected_sequence) {
                    selected_order = order_index;
                    selected_dish = dish_index;
                    selected_sequence = dish->ready_sequence;
                }
            }
        }

        if (selected_order == MAX_VISITORS)
            return;

        {
            RuntimeOrder *order = &orders[selected_order];
            RuntimeDish *dish = &order->dishes[selected_dish];
            unsigned waiter_id = order->waiter_id;

            dish->in_ready_queue = 0;
            dish->collected = 1;
            logger_event(logger, current_time,
                         "Waiter %u collected dish '%s' for order %u",
                         waiter_id, dish->name, order->visitor_id);

            if (order_is_collected(order) && !order->delivered) {
                order->delivered = 1;
                ++stats->orders_delivered;
                ++*finished_orders;
                waiter_busy[waiter_id - 1] = 0;
                logger_event(logger, current_time,
                             "Waiter %u assembled and delivered order %u to "
                             "visitor %u",
                             waiter_id, order->visitor_id, order->visitor_id);
            }
        }
    }
}

static void print_summary(Logger *logger,
                          const Config *config,
                          const Statistics *stats,
                          uint64_t current_time,
                          int interrupted)
{
    unsigned i;
    double average_wait = 0.0;
    double cook_utilization = 0.0;

    if (stats->operations_started != 0)
        average_wait = (double)stats->total_queue_wait /
                       (double)stats->operations_started;
    if (current_time != 0 && config->cook_count != 0)
        cook_utilization = 100.0 * (double)stats->cook_busy_time /
                           ((double)config->cook_count * (double)current_time);

    logger_line(logger,
                "\n================ FINAL STATISTICS ================\n");
    logger_line(logger, "Termination: %s\n",
                interrupted ? "interrupted by user" : "normal");
    logger_line(logger, "Model time: %llu\n",
                (unsigned long long)current_time);
    logger_line(logger, "Visitors arrived: %u / %u\n",
                stats->visitors_arrived, config->visitor_count);
    logger_line(logger, "Orders accepted: %u\n", stats->orders_accepted);
    logger_line(logger, "Orders delivered: %u\n", stats->orders_delivered);
    logger_line(logger, "Orders canceled as impossible: %u\n",
                stats->orders_canceled);
    logger_line(logger, "Dishes created / ready: %u / %u\n",
                stats->dishes_created, stats->dishes_ready);
    logger_line(logger, "Operations created / started / completed: %u / %u / %u\n",
                stats->operations_created, stats->operations_started,
                stats->operations_completed);
    logger_line(logger, "Average kitchen-queue wait: %.2f model units\n",
                average_wait);
    logger_line(logger, "Cook utilization: %.2f%%\n", cook_utilization);

    for (i = 0; i < EQUIPMENT_TYPES; ++i) {
        double utilization = 0.0;
        if (current_time != 0 && config->equipment_count[i] != 0) {
            utilization = 100.0 * (double)stats->equipment_busy_time[i] /
                          ((double)config->equipment_count[i] *
                           (double)current_time);
        }
        logger_line(logger, "%-5s utilization: %.2f%% (units: %u)\n",
                    config_equipment_name((EquipmentType)i), utilization,
                    config->equipment_count[i]);
    }
    logger_line(logger,
                "==================================================\n");
}

SimulationResult simulation_run(const Config *config,
                                const SimulationOptions *options)
{
    RuntimeOrder orders[MAX_VISITORS];
    RuntimeOperation operations[MAX_OPERATIONS];
    int cook_busy[MAX_OPERATIONS];
    int waiter_busy[MAX_VISITORS];
    unsigned equipment_busy[EQUIPMENT_TYPES] = {0};
    Statistics stats;
    Logger logger;
    unsigned operation_count = 0;
    unsigned next_visitor = 0;
    unsigned finished_orders = 0;
    unsigned cook_limit;
    unsigned waiter_limit;
    uint64_t next_ready_sequence = 0;
    uint64_t next_queue_sequence = 0;
    uint64_t current_time = 0;
    uint32_t rng_state;

    if (config == NULL || options == NULL || config_validate(config) != 0)
        return SIMULATION_ERROR;

    memset(orders, 0, sizeof(orders));
    memset(operations, 0, sizeof(operations));
    memset(cook_busy, 0, sizeof(cook_busy));
    memset(waiter_busy, 0, sizeof(waiter_busy));
    memset(&stats, 0, sizeof(stats));

    cook_limit = config->cook_count;
    if (cook_limit > MAX_OPERATIONS)
        cook_limit = MAX_OPERATIONS;
    waiter_limit = config->waiter_count;
    if (waiter_limit > MAX_VISITORS)
        waiter_limit = MAX_VISITORS;

    rng_state = config->seed;

    if (logger_open(&logger, options->log_path, options->delay_ms) != 0)
        return SIMULATION_ERROR;

    logger_event(&logger, 0,
                 "Restaurant opened; sequential discrete-event simulation started");
    logger_event(&logger, 0,
                 "Configuration: visitors=%u, waiters=%u, cooks=%u, "
                 "priority=%s, seed=%u",
                 config->visitor_count, config->waiter_count, config->cook_count,
                 config->priority == PRIORITY_FIFO ? "FIFO" : "LIFO",
                 config->seed);

    while (finished_orders < config->visitor_count) {
        uint64_t next_arrival_time = UINT64_MAX;
        uint64_t next_completion_time = UINT64_MAX;
        unsigned i;

        if (stop_requested(options)) {
            logger_event(&logger, current_time,
                         "Interrupt received; restaurant is stopping safely");
            print_summary(&logger, config, &stats, current_time, 1);
            logger_close(&logger);
            return SIMULATION_INTERRUPTED;
        }

        if (next_visitor < config->visitor_count)
            next_arrival_time = arrival_time_for(config, next_visitor);

        for (i = 0; i < operation_count; ++i) {
            if (operations[i].state == OP_RUNNING &&
                operations[i].finish_time < next_completion_time)
                next_completion_time = operations[i].finish_time;
        }

        if (next_arrival_time == UINT64_MAX &&
            next_completion_time == UINT64_MAX) {
            logger_event(&logger, current_time,
                         "Simulation cannot progress: no events remain while "
                         "orders are unfinished");
            print_summary(&logger, config, &stats, current_time, 0);
            logger_close(&logger);
            return SIMULATION_ERROR;
        }

        current_time = next_arrival_time < next_completion_time
                           ? next_arrival_time
                           : next_completion_time;

        /* 1. Сначала завершаются все операции в текущий момент времени. */
        for (i = 0; i < operation_count; ++i) {
            RuntimeOperation *operation = &operations[i];
            RuntimeDish *dish;

            if (operation->state != OP_RUNNING ||
                operation->finish_time != current_time)
                continue;

            operation->state = OP_DONE;
            cook_busy[operation->cook_id - 1] = 0;
            --equipment_busy[(unsigned)operation->equipment];
            ++stats.operations_completed;
            stats.cook_busy_time += operation->duration;
            stats.equipment_busy_time[(unsigned)operation->equipment] +=
                operation->duration;

            dish = &orders[operation->order_id - 1]
                        .dishes[operation->dish_number - 1];
            ++dish->completed_operations;

            logger_event(&logger, current_time,
                         "Cook %u finished operation %u of dish '%s' "
                         "for order %u and released %s",
                         operation->cook_id, operation->step_number, dish->name,
                         operation->order_id,
                         config_equipment_name(operation->equipment));

            if (dish->completed_operations == dish->operation_count) {
                dish->ready = 1;
                dish->ready_sequence = ++next_ready_sequence;
                ++stats.dishes_ready;
                logger_event(&logger, current_time,
                             "Dish '%s' is ready for order %u",
                             dish->name, operation->order_id);
            } else {
                unsigned next_step_index = operation->step_number;
                unsigned next_operation_id = dish->operation_ids[next_step_index];
                enqueue_operation(operations, orders, next_operation_id,
                                  operation_count, config, current_time,
                                  &next_queue_sequence, &logger);
            }
        }

        /* 2. Готовые блюда попадают в ready-area и забираются официантами. */
        process_ready_dishes(orders, next_visitor, waiter_busy,
                             config->ready_queue_capacity, &finished_orders,
                             &stats, current_time, &logger);

        /* 3. Освободившиеся официанты принимают ожидающие заказы. */
        if (accept_waiting_orders(orders, next_visitor, waiter_busy,
                                  waiter_limit, operations, &operation_count,
                                  &next_queue_sequence, &finished_orders,
                                  config, &stats, current_time, &logger) != 0) {
            logger_close(&logger);
            return SIMULATION_ERROR;
        }

        /* 4. Приходят все посетители, назначенные на текущий момент. */
        while (next_visitor < config->visitor_count &&
               arrival_time_for(config, next_visitor) == current_time) {
            RuntimeOrder *order = &orders[next_visitor];
            order->visitor_id = next_visitor + 1;
            assign_order_dishes(config, next_visitor, order, &rng_state);
            ++next_visitor;
            ++stats.visitors_arrived;

            logger_event(&logger, current_time,
                         "Visitor %u arrived and formed order %u with %u dish%s",
                         order->visitor_id, order->visitor_id, order->dish_count,
                         order->dish_count == 1 ? "" : "es");

            if (next_visitor == config->visitor_count)
                logger_event(&logger, current_time,
                             "Order reception is closed: the last planned "
                             "visitor has arrived");
        }

        /* 5. Новопришедшие посетители получают свободных официантов. */
        if (accept_waiting_orders(orders, next_visitor, waiter_busy,
                                  waiter_limit, operations, &operation_count,
                                  &next_queue_sequence, &finished_orders,
                                  config, &stats, current_time, &logger) != 0) {
            logger_close(&logger);
            return SIMULATION_ERROR;
        }

        /* 6. Свободные повара выбирают доступные операции. */
        for (;;) {
            int cook_index;
            int operation_index;
            RuntimeOperation *operation;
            RuntimeDish *dish;

            promote_waiting_operations(operations, orders, operation_count,
                                       config, current_time, &logger);

            cook_index = find_free_cook(cook_busy, cook_limit);
            if (cook_index < 0)
                break;

            operation_index = find_next_operation(
                operations, operation_count, equipment_busy, config);
            if (operation_index < 0)
                break;

            operation = &operations[(unsigned)operation_index];
            dish = &orders[operation->order_id - 1]
                        .dishes[operation->dish_number - 1];

            operation->state = OP_RUNNING;
            operation->cook_id = (unsigned)cook_index + 1;
            operation->finish_time = current_time + operation->duration;
            cook_busy[cook_index] = 1;
            ++equipment_busy[(unsigned)operation->equipment];

            ++stats.operations_started;
            stats.total_queue_wait += current_time - operation->queued_at;

            logger_event(&logger, current_time,
                         "Cook %u acquired %s and started operation %u of "
                         "dish '%s' for order %u (finishes at t=%llu)",
                         operation->cook_id,
                         config_equipment_name(operation->equipment),
                         operation->step_number, dish->name, operation->order_id,
                         (unsigned long long)operation->finish_time);
        }
    }

    logger_event(&logger, current_time,
                 "Restaurant closed: delivered=%u, canceled=%u, total=%u",
                 stats.orders_delivered, stats.orders_canceled,
                 config->visitor_count);
    print_summary(&logger, config, &stats, current_time, 0);
    logger_close(&logger);
    return SIMULATION_OK;
}

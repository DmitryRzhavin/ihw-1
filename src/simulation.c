#include "simulation.h"
#include "config.h"

#include <inttypes.h>
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
} RuntimeOperation;

typedef struct {
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
    int delivered;
} RuntimeOrder;

static const char *equipment_name(EquipmentType equipment)
{
    static const char *names[EQUIPMENT_TYPES] = {
        "stove", "oven", "grill", "table"
    };

    if ((unsigned)equipment >= EQUIPMENT_TYPES)
        return "unknown";

    return names[equipment];
}

static void log_event(uint64_t time, const char *message)
{
    printf("[%6" PRIu64 "] %s\n", time, message);
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

/*
 * Ищет первую по порядку создания операцию, которая уже доступна
 * и для которой есть свободная единица нужного оборудования.
 */
static int find_next_operation(const RuntimeOperation *operations,
                               unsigned operation_count,
                               const unsigned *equipment_busy,
                               const Config *config)
{
    unsigned i;

    for (i = 0; i < operation_count; ++i) {
        unsigned equipment;

        if (operations[i].state != OP_QUEUED)
            continue;

        equipment = (unsigned)operations[i].equipment;
        if (equipment_busy[equipment] < config->equipment_count[equipment])
            return (int)i;

        /* Эта операция пока недоступна — проверяем следующие в очереди. */
    }

    return -1;
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
                              uint64_t current_time)
{
    RuntimeOperation *operation = &operations[operation_id];
    const RuntimeDish *dish =
        &orders[operation->order_id - 1].dishes[operation->dish_number - 1];
    char message[256];

    if (config->kitchen_queue_capacity == 0 ||
        queued_operation_count(operations, operation_count) <
            config->kitchen_queue_capacity) {
        operation->state = OP_QUEUED;

        snprintf(message, sizeof(message),
                 "Coordinator queued operation %u of dish '%s' for order %u",
                 operation->step_number, dish->name, operation->order_id);
    } else {
        operation->state = OP_WAITING_CAPACITY;

        snprintf(message, sizeof(message),
                 "Kitchen queue full; operation %u of dish '%s' "
                 "waits for order %u",
                 operation->step_number, dish->name, operation->order_id);
    }

    log_event(current_time, message);
}

/* Освободившиеся места получает первая ожидающая операция. */
static void promote_waiting_operations(RuntimeOperation *operations,
                                       const RuntimeOrder *orders,
                                       unsigned operation_count,
                                       const Config *config,
                                       uint64_t current_time)
{
    unsigned i;

    for (i = 0; i < operation_count; ++i) {
        RuntimeOperation *operation = &operations[i];
        const RuntimeDish *dish;
        char message[256];

        if (operation->state != OP_WAITING_CAPACITY)
            continue;

        if (config->kitchen_queue_capacity != 0 &&
            queued_operation_count(operations, operation_count) >=
                config->kitchen_queue_capacity) {
            return;
        }

        operation->state = OP_QUEUED;
        dish = &orders[operation->order_id - 1]
                    .dishes[operation->dish_number - 1];

        snprintf(message, sizeof(message),
                 "Coordinator queued operation %u of dish '%s' for order %u",
                 operation->step_number, dish->name, operation->order_id);
        log_event(current_time, message);
    }
}

static int accept_waiting_orders(RuntimeOrder *orders,
                                 unsigned arrived_visitors,
                                 int *waiter_busy,
                                 unsigned waiter_limit,
                                 RuntimeOperation *operations,
                                 unsigned *operation_count,
                                 const Config *config,
                                 uint64_t current_time)
{
    unsigned visitor_index;

    /* FIFO по порядку прихода посетителей. */
    for (visitor_index = 0; visitor_index < arrived_visitors;
         ++visitor_index) {
        RuntimeOrder *order = &orders[visitor_index];
        unsigned waiter_index;
        unsigned dish_index;
        char message[256];

        if (order->accepted)
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

        snprintf(message, sizeof(message),
                 "Waiter %u accepted order %u from visitor %u",
                 order->waiter_id, order->visitor_id, order->visitor_id);
        log_event(current_time, message);

        for (dish_index = 0; dish_index < order->dish_count; ++dish_index) {
            const DishSpec *spec;
            RuntimeDish *dish;
            unsigned step;

            if (config->order_count != 0) {
                spec = &config->menu[
                    config->order_dishes[visitor_index][dish_index]];
            } else {
                spec = &config->menu[
                    (visitor_index + dish_index) % config->menu_size];
            }

            dish = &order->dishes[dish_index];
            strcpy(dish->name, spec->name);
            dish->operation_count = spec->operation_count;

            snprintf(message, sizeof(message),
                     "Coordinator created dish '%s' for order %u",
                     dish->name, order->visitor_id);
            log_event(current_time, message);

            for (step = 0; step < spec->operation_count; ++step) {
                RuntimeOperation *operation;

                if (*operation_count >= MAX_OPERATIONS) {
                    fprintf(stderr, "Too many operations in simulation\n");
                    return -1;
                }

                operation = &operations[*operation_count];
                operation->order_id = order->visitor_id;
                operation->dish_number = dish_index + 1;
                operation->step_number = step + 1;
                operation->equipment = spec->operations[step].equipment;
                operation->duration = spec->operations[step].duration;
                operation->state = OP_BLOCKED;

                dish->operation_ids[step] = *operation_count;
                ++*operation_count;

                snprintf(message, sizeof(message),
                         "Coordinator created operation %u (%s, %u time "
                         "unit%s) for dish '%s', order %u",
                         step + 1,
                         equipment_name(operation->equipment),
                         operation->duration,
                         operation->duration == 1 ? "" : "s",
                         dish->name,
                         order->visitor_id);
                log_event(current_time, message);
            }

            enqueue_operation(operations, orders,
                              dish->operation_ids[0], *operation_count,
                              config, current_time);
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

/*
 * Обрабатывает зону готовых блюд как FIFO-буфер.
 * Вместимость 0 означает отсутствие ограничения.
 * Забор блюд официантом не занимает модельного времени.
 */
static void process_ready_dishes(RuntimeOrder *orders,
                                 unsigned arrived_visitors,
                                 int *waiter_busy,
                                 unsigned ready_queue_capacity,
                                 unsigned *delivered_orders,
                                 uint64_t current_time)
{
    for (;;) {
        unsigned queued_count = 0;
        unsigned order_index;
        unsigned dish_index;
        unsigned selected_order = MAX_VISITORS;
        unsigned selected_dish = MAX_DISHES_PER_ORDER;
        uint64_t selected_sequence = UINT64_MAX;
        char message[256];

        for (order_index = 0; order_index < arrived_visitors;
             ++order_index) {
            RuntimeOrder *order = &orders[order_index];

            for (dish_index = 0; dish_index < order->dish_count;
                 ++dish_index) {
                RuntimeDish *dish = &order->dishes[dish_index];

                if (dish->in_ready_queue)
                    ++queued_count;

                if (dish->ready && !dish->collected &&
                    !dish->in_ready_queue &&
                    dish->ready_sequence < selected_sequence) {
                    selected_order = order_index;
                    selected_dish = dish_index;
                    selected_sequence = dish->ready_sequence;
                }
            }
        }

        /* Помещаем в зону готовые блюда в порядке их готовности. */
        while (selected_order != MAX_VISITORS &&
               (ready_queue_capacity == 0 ||
                queued_count < ready_queue_capacity)) {
            RuntimeOrder *order = &orders[selected_order];
            RuntimeDish *dish = &order->dishes[selected_dish];

            dish->in_ready_queue = 1;
            dish->waiting_for_ready_capacity = 0;
            ++queued_count;

            snprintf(message, sizeof(message),
                     "Coordinator placed dish '%s' in ready area "
                     "for order %u",
                     dish->name, order->visitor_id);
            log_event(current_time, message);

            selected_order = MAX_VISITORS;
            selected_dish = MAX_DISHES_PER_ORDER;
            selected_sequence = UINT64_MAX;

            for (order_index = 0; order_index < arrived_visitors;
                 ++order_index) {
                RuntimeOrder *candidate_order = &orders[order_index];

                for (dish_index = 0;
                     dish_index < candidate_order->dish_count;
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

        /* Отмечаем блюда, ожидающие освобождения места в зоне. */
        if (ready_queue_capacity != 0 &&
            queued_count >= ready_queue_capacity) {
            for (order_index = 0; order_index < arrived_visitors;
                 ++order_index) {
                RuntimeOrder *order = &orders[order_index];

                for (dish_index = 0; dish_index < order->dish_count;
                     ++dish_index) {
                    RuntimeDish *dish = &order->dishes[dish_index];

                    if (dish->ready && !dish->collected &&
                        !dish->in_ready_queue &&
                        !dish->waiting_for_ready_capacity) {
                        dish->waiting_for_ready_capacity = 1;

                        snprintf(message, sizeof(message),
                                 "Ready area full; dish '%s' waits "
                                 "for order %u",
                                 dish->name, order->visitor_id);
                        log_event(current_time, message);
                    }
                }
            }
        }

        /* Официант забирает самое раннее блюдо из зоны. */
        selected_order = MAX_VISITORS;
        selected_dish = MAX_DISHES_PER_ORDER;
        selected_sequence = UINT64_MAX;

        for (order_index = 0; order_index < arrived_visitors;
             ++order_index) {
            RuntimeOrder *order = &orders[order_index];

            for (dish_index = 0; dish_index < order->dish_count;
                 ++dish_index) {
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

            snprintf(message, sizeof(message),
                     "Waiter %u collected dish '%s' for order %u",
                     waiter_id, dish->name, order->visitor_id);
            log_event(current_time, message);

            if (order_is_collected(order) && !order->delivered) {
                order->delivered = 1;
                ++*delivered_orders;
                waiter_busy[waiter_id - 1] = 0;

                snprintf(message, sizeof(message),
                         "Waiter %u assembled and delivered order %u "
                         "to visitor %u",
                         waiter_id, order->visitor_id, order->visitor_id);
                log_event(current_time, message);
            }
        }
    }
}

int simulation_run(const Config *config)
{
    RuntimeOrder orders[MAX_VISITORS];
    RuntimeOperation operations[MAX_OPERATIONS];
    int cook_busy[MAX_OPERATIONS];
    unsigned equipment_busy[EQUIPMENT_TYPES] = {0};
    unsigned operation_count = 0;
    unsigned next_visitor = 0;
    unsigned delivered_orders = 0;
    uint64_t next_ready_sequence = 0;
    int waiter_busy[MAX_VISITORS];
    unsigned waiter_limit;
    unsigned cook_limit;
    uint64_t current_time = 0;

    if (config == NULL || config_validate(config) != 0)
        return -1;

    memset(orders, 0, sizeof(orders));
    memset(operations, 0, sizeof(operations));
    memset(cook_busy, 0, sizeof(cook_busy));
    memset(waiter_busy, 0, sizeof(waiter_busy));

    /*
     * Более поваров, чем одновременно существующих операций,
     * модель использовать не сможет, поэтому ограничиваем поиск.
     */
    cook_limit = config->cook_count;
    if (cook_limit > MAX_OPERATIONS)
        cook_limit = MAX_OPERATIONS;

    waiter_limit = config->waiter_count;
    if (waiter_limit > MAX_VISITORS)
        waiter_limit = MAX_VISITORS;

    log_event(0, "Restaurant simulation started");

    while (delivered_orders < config->visitor_count) {
        uint64_t next_arrival_time = UINT64_MAX;
        uint64_t next_completion_time = UINT64_MAX;
        unsigned i;

        if (next_visitor < config->visitor_count) {
            next_arrival_time =
                (uint64_t)next_visitor * config->arrival_interval;
        }

        for (i = 0; i < operation_count; ++i) {
            if (operations[i].state == OP_RUNNING &&
                operations[i].finish_time < next_completion_time) {
                next_completion_time = operations[i].finish_time;
            }
        }

        if (next_arrival_time == UINT64_MAX &&
            next_completion_time == UINT64_MAX) {
            fprintf(stderr,
                    "Simulation stopped: no events remain, "
                    "but some orders are unfinished.\n");
            return -1;
        }

        current_time = next_arrival_time < next_completion_time
            ? next_arrival_time
            : next_completion_time;

        /* Сначала завершаем все операции, назначенные на это время. */
        for (i = 0; i < operation_count; ++i) {
            RuntimeOperation *operation = &operations[i];
            RuntimeDish *dish;
            char message[256];

            if (operation->state != OP_RUNNING ||
                operation->finish_time != current_time)
                continue;

            operation->state = OP_DONE;
            cook_busy[operation->cook_id - 1] = 0;
            --equipment_busy[(unsigned)operation->equipment];

            dish = &orders[operation->order_id - 1]
                        .dishes[operation->dish_number - 1];
            ++dish->completed_operations;

            snprintf(message, sizeof(message),
                     "Cook %u finished operation %u of dish '%s' "
                     "for order %u",
                     operation->cook_id,
                     operation->step_number,
                     dish->name,
                     operation->order_id);
            log_event(current_time, message);

            if (dish->completed_operations == dish->operation_count) {
                dish->ready = 1;
                dish->ready_sequence = ++next_ready_sequence;

                snprintf(message, sizeof(message),
                         "Dish '%s' is ready for order %u",
                         dish->name, operation->order_id);
                log_event(current_time, message);
            } else {
                unsigned next_step = operation->step_number;
                unsigned next_operation_id =
                    dish->operation_ids[next_step];

                enqueue_operation(operations, orders,
                                  next_operation_id, operation_count,
                                  config, current_time);
            }
        }

        process_ready_dishes(orders, next_visitor,
                             waiter_busy,
                             config->ready_queue_capacity,
                             &delivered_orders,
                             current_time);

        if (accept_waiting_orders(orders, next_visitor,
                                  waiter_busy, waiter_limit,
                                  operations, &operation_count,
                                  config, current_time) != 0)
            return -1;

        /* Обрабатываем посетителя, пришедшего в этот момент. */
        if (next_visitor < config->visitor_count &&
            next_arrival_time == current_time) {
            unsigned visitor_index = next_visitor;
            unsigned order_id = visitor_index + 1;
            RuntimeOrder *order = &orders[visitor_index];
            char message[256];

            ++next_visitor;
            order->visitor_id = order_id;
            order->dish_count = config->order_count != 0
                ? config->order_dish_count[visitor_index]
                : config->dishes_per_order;

            snprintf(message, sizeof(message),
                     "Visitor %u arrived", order_id);
            log_event(current_time, message);
        }

        if (accept_waiting_orders(orders, next_visitor,
                                  waiter_busy, waiter_limit,
                                  operations, &operation_count,
                                  config, current_time) != 0)
            return -1;

        /* Назначаем доступные операции свободным поварам. */
        for (;;) {
            int cook_index = find_free_cook(cook_busy, cook_limit);
            int operation_index;
            RuntimeOperation *operation;
            RuntimeDish *dish;
            char message[256];

            promote_waiting_operations(operations, orders,
                                       operation_count, config, current_time);

            if (cook_index < 0)
                break;

            operation_index = find_next_operation(
                operations, operation_count, equipment_busy, config);
            if (operation_index < 0)
                break;

            operation = &operations[operation_index];
            dish = &orders[operation->order_id - 1]
                        .dishes[operation->dish_number - 1];

            operation->state = OP_RUNNING;
            operation->cook_id = (unsigned)cook_index + 1;
            operation->finish_time =
                current_time + operation->duration;

            cook_busy[cook_index] = 1;
            ++equipment_busy[(unsigned)operation->equipment];

            snprintf(message, sizeof(message),
                     "Cook %u acquired %s and started operation %u "
                     "of dish '%s' for order %u",
                     operation->cook_id,
                     equipment_name(operation->equipment),
                     operation->step_number,
                     dish->name,
                     operation->order_id);
            log_event(current_time, message);
        }
    }

    {
        char message[128];

        snprintf(message, sizeof(message),
                 "Restaurant closed: all %u orders were delivered",
                 delivered_orders);
        log_event(current_time, message);
    }

    return 0;
}

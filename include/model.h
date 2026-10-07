#ifndef MODEL_H
#define MODEL_H

#define MAX_DISHES 8
#define MAX_STEPS_PER_DISH 8
#define MAX_NAME_LENGTH 32
#define MAX_VISITORS 100
#define MAX_DISHES_PER_ORDER 4
#define EQUIPMENT_TYPES 4

typedef enum {
    EQUIPMENT_STOVE = 0,
    EQUIPMENT_OVEN,
    EQUIPMENT_GRILL,
    EQUIPMENT_TABLE
} EquipmentType;

typedef enum {
    PRIORITY_FIFO = 0,
    PRIORITY_LIFO
} OrderPriority;

typedef struct {
    EquipmentType equipment;
    unsigned duration;
} OperationSpec;

typedef struct {
    char name[MAX_NAME_LENGTH];
    unsigned operation_count;
    OperationSpec operations[MAX_STEPS_PER_DISH];
} DishSpec;

typedef struct {
    unsigned visitor_count;
    unsigned waiter_count;
    unsigned cook_count;
    unsigned equipment_count[EQUIPMENT_TYPES];
    unsigned arrival_interval;

    /* Ноль означает неограниченную вместимость очереди. */
    unsigned kitchen_queue_capacity;
    unsigned ready_queue_capacity;
    OrderPriority priority;

    unsigned dishes_per_order;
    unsigned menu_size;
    DishSpec menu[MAX_DISHES];

    /* Индексы блюд меню, заказанных каждым посетителем. */
    unsigned order_count;
    unsigned order_dish_count[MAX_VISITORS];
    unsigned order_dishes[MAX_VISITORS][MAX_DISHES_PER_ORDER];
} Config;

#endif

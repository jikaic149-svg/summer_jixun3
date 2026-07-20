#ifndef APP_COMMAND_BUS_H
#define APP_COMMAND_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "lcd_demo_config.h"

typedef enum {
    APP_COMMAND_SET_DISTANCE = 0,
    APP_COMMAND_FIRE_DISTANCE,
    APP_COMMAND_FIXED_TARGET,
    APP_COMMAND_AUTO_SEARCH,
    APP_COMMAND_SWEEP,
    APP_COMMAND_STOP
} app_command_type_t;

typedef struct {
    app_command_type_t type;
    uint16_t distance_cm;
    uint32_t timestamp_ms;
} app_command_t;

typedef enum {
    APP_PUBLISH_OK = 0,
    APP_PUBLISH_FULL,
    APP_PUBLISH_INVALID
} app_publish_result_t;

typedef struct {
    app_command_t slots[APP_COMMAND_QUEUE_CAPACITY];
    uint8_t read_index;
    uint8_t write_index;
    uint8_t count;
} app_command_bus_t;

void app_command_bus_init(app_command_bus_t *bus);
app_publish_result_t app_command_bus_publish(app_command_bus_t *bus,
                                             const app_command_t *command);
bool app_command_bus_take(app_command_bus_t *bus, app_command_t *command);

#endif

#include "app_command_bus.h"

#include <stddef.h>

_Static_assert(APP_COMMAND_QUEUE_CAPACITY > 0U,
               "APP_COMMAND_QUEUE_CAPACITY must be non-zero");
_Static_assert(APP_COMMAND_QUEUE_CAPACITY <= UINT8_MAX,
               "queue indices are uint8_t");

void app_command_bus_init(app_command_bus_t *bus)
{
    if (bus == NULL) {
        return;
    }

    bus->read_index = 0U;
    bus->write_index = 0U;
    bus->count = 0U;
}

app_publish_result_t app_command_bus_publish(app_command_bus_t *bus,
                                             const app_command_t *command)
{
    if ((bus == NULL) || (command == NULL)) {
        return APP_PUBLISH_INVALID;
    }
    if (bus->count >= APP_COMMAND_QUEUE_CAPACITY) {
        return APP_PUBLISH_FULL;
    }

    bus->slots[bus->write_index] = *command;
    bus->write_index = (uint8_t)((bus->write_index + 1U) %
                                 APP_COMMAND_QUEUE_CAPACITY);
    ++bus->count;
    return APP_PUBLISH_OK;
}

bool app_command_bus_take(app_command_bus_t *bus, app_command_t *command)
{
    if ((bus == NULL) || (command == NULL) || (bus->count == 0U)) {
        return false;
    }

    *command = bus->slots[bus->read_index];
    bus->read_index = (uint8_t)((bus->read_index + 1U) %
                                APP_COMMAND_QUEUE_CAPACITY);
    --bus->count;
    return true;
}

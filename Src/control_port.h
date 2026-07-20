#ifndef CONTROL_PORT_H
#define CONTROL_PORT_H

#include "app_command_bus.h"

typedef enum {
    CONTROL_SUBMIT_ACCEPTED = 0,
    CONTROL_SUBMIT_BUSY,
    CONTROL_SUBMIT_UNAVAILABLE
} control_submit_result_t;

control_submit_result_t control_port_submit(const app_command_t *command);

#endif

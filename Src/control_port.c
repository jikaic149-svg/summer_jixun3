#include "control_port.h"

#include <stddef.h>

__attribute__((weak))
control_submit_result_t control_port_submit(const app_command_t *command)
{
    if (command == NULL) {
        return CONTROL_SUBMIT_BUSY;
    }

    /* UI-only demo: acknowledge commands without driving any hardware. */
    return CONTROL_SUBMIT_ACCEPTED;
}

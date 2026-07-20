#ifndef CHASSIS_SERIAL_CMD_H
#define CHASSIS_SERIAL_CMD_H

#include <stdint.h>

uint8_t ChassisSerialCmd_HandleLine(const uint8_t *line, uint8_t len);

#endif /* CHASSIS_SERIAL_CMD_H */

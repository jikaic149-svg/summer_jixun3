#ifndef BSP_LASER_H
#define BSP_LASER_H

#include <stdint.h>

uint8_t BSP_LaserInit(void);
void BSP_LaserOn(void);
void BSP_LaserOff(void);
uint8_t BSP_LaserIsOn(void);

#endif /* BSP_LASER_H */

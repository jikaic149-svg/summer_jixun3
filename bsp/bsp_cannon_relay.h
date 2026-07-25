#ifndef BSP_CANNON_RELAY_H
#define BSP_CANNON_RELAY_H

#include <stdbool.h>

void bsp_cannon_relay_init(void);
void bsp_cannon_relay_set_fire(bool enabled);
void bsp_cannon_relay_set_charge(bool enabled);
void bsp_cannon_relay_all_off(void);

#endif

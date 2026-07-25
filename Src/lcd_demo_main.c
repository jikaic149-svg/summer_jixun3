#include "bsp_board_f407ve.h"
#include "bsp_cannon_relay.h"
#include "lcd_demo_app.h"

int main(void)
{
    bsp_board_init();
    bsp_cannon_relay_init();
    lcd_demo_app_init();

    for (;;) {
        bsp_board_poll();
        lcd_demo_app_step(bsp_board_millis());
        bsp_board_idle();
    }
}

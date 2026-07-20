#include "bsp_board_f407ve.h"
#include "lcd_demo_app.h"

int main(void)
{
    bsp_board_init();
    lcd_demo_app_init();

    for (;;) {
        bsp_board_poll();
        lcd_demo_app_step(bsp_board_millis());
        bsp_board_idle();
    }
}

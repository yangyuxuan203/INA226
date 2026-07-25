#include "lv_port_energy.h"

#include "lcd.h"
#include "touch.h"
#include "lvgl.h"
#include <stdbool.h>

#define ENERGY_LVGL_BUF_LINES 6U

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[800U * ENERGY_LVGL_BUF_LINES];
static lv_color_t s_buf2[800U * ENERGY_LVGL_BUF_LINES];

static void EnergyLvgl_Flush(lv_disp_drv_t *disp_drv,
                             const lv_area_t *area,
                             lv_color_t *color_p)
{
    int32_t x;
    int32_t y;

    if (area->x2 < 0 || area->y2 < 0 ||
        area->x1 >= (int32_t)lcddev.width ||
        area->y1 >= (int32_t)lcddev.height)
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    lcd_set_window((uint16_t)area->x1,
                   (uint16_t)area->y1,
                   (uint16_t)(area->x2 - area->x1 + 1),
                   (uint16_t)(area->y2 - area->y1 + 1));
    lcd_write_ram_prepare();

    for (y = area->y1; y <= area->y2; y++)
    {
        for (x = area->x1; x <= area->x2; x++)
        {
            lcd_wr_data(color_p->full);
            color_p++;
        }
    }

    lv_disp_flush_ready(disp_drv);
}

static void EnergyLvgl_TouchRead(lv_indev_drv_t *indev_drv,
                                 lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    (void)indev_drv;

    tp_dev.scan(0);
    if (tp_dev.sta & TP_PRES_DOWN)
    {
        if (tp_dev.x[0] < lcddev.width && tp_dev.y[0] < lcddev.height)
        {
            last_x = (lv_coord_t)tp_dev.x[0];
            last_y = (lv_coord_t)tp_dev.y[0];
        }
        data->state = LV_INDEV_STATE_PR;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

void EnergyLvgl_PortInit(void)
{
    static lv_disp_drv_t disp_drv;
    static lv_indev_drv_t indev_drv;

    lcd_display_dir(0);
    (void)tp_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2,
                          lcddev.width * ENERGY_LVGL_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = lcddev.width;
    disp_drv.ver_res = lcddev.height;
    disp_drv.flush_cb = EnergyLvgl_Flush;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = EnergyLvgl_TouchRead;
    lv_indev_drv_register(&indev_drv);
}

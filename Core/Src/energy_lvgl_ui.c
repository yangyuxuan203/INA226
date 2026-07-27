#include "energy_lvgl_ui.h"

#include "app_config.h"
#include "app_health.h"
#include "lv_port_energy.h"
#include "main.h"
#include "lvgl.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <stdarg.h>

#define UI_W                 480
#define UI_H                 800
#define UI_HEADER_H          82
#define UI_AI_HISTORY_LEN    6

typedef enum
{
    ENERGY_PAGE_HOME = 0,
    ENERGY_PAGE_CAR,
    ENERGY_PAGE_HUMAN,
    ENERGY_PAGE_AI,
} EnergyPage_t;

typedef struct
{
    lv_obj_t *time;
    lv_obj_t *cloud;
    lv_obj_t *detail_title;
    lv_obj_t *detail_lines[10];
    lv_obj_t *ai_history_lines[4];
    EnergyPage_t detail_page;
    uint8_t in_detail;
} EnergyUi_t;

typedef struct
{
    float pv[UI_AI_HISTORY_LEN];
    float load[UI_AI_HISTORY_LEN];
    float soc[UI_AI_HISTORY_LEN];
    uint8_t count;
    uint8_t head;
    uint32_t last_tick;
    uint32_t version;
} AiHistory_t;

static EnergyUi_t s_ui;
static AiHistory_t s_ai_hist;
static lv_timer_t *s_update_timer;

static lv_color_t c_bg(void) { return lv_color_hex(0x0b1120); }
static lv_color_t c_panel(void) { return lv_color_hex(0x172033); }
static lv_color_t c_panel_2(void) { return lv_color_hex(0x1f2a44); }
static lv_color_t c_text(void) { return lv_color_hex(0xeaf2ff); }
static lv_color_t c_muted(void) { return lv_color_hex(0x9aa8bd); }
static lv_color_t c_home(void) { return lv_color_hex(0x2dd4bf); }
static lv_color_t c_car(void) { return lv_color_hex(0x60a5fa); }
static lv_color_t c_human(void) { return lv_color_hex(0xfbbf24); }
static lv_color_t c_ai(void) { return lv_color_hex(0xc084fc); }
static lv_color_t c_badge(void) { return lv_color_hex(0x24314e); }

static void set_label(lv_obj_t *label, const char *text)
{
    const char *old_text;

    if (label != NULL)
    {
        old_text = lv_label_get_text(label);
        if (old_text != NULL && strcmp(old_text, text) == 0)
        {
            return;
        }
        lv_label_set_text(label, text);
    }
}

static void set_line(lv_obj_t *label, const char *fmt, ...)
{
    char buf[112];
    va_list args;

    if (label == NULL)
    {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    set_label(label, buf);
}

static void load_screen(lv_obj_t *scr)
{
    lv_obj_t *old = lv_scr_act();

    lv_scr_load(scr);
    if (old != NULL && old != scr)
    {
        lv_obj_del_async(old);
    }
}

static lv_obj_t *box(lv_obj_t *parent, int16_t x, int16_t y,
                     int16_t w, int16_t h, lv_color_t bg,
                     lv_color_t border, uint8_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, int16_t x, int16_t y,
                       const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *label_center(lv_obj_t *parent, const char *txt,
                              const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_center(l);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *dot(lv_obj_t *parent, int16_t x, int16_t y, int16_t size,
                     lv_color_t color)
{
    lv_obj_t *obj = box(parent, x, y, size, size, color, color, (uint8_t)(size / 2));
    lv_obj_set_style_border_width(obj, 0, 0);
    return obj;
}

static const char *car_status_text(uint8_t status)
{
    switch (status)
    {
    case 0x00: return "IDLE";
    case 0x01: return "CHARGING";
    case 0x02: return "DISCHARGE";
    case 0x03: return "FAULT";
    case 0x04: return "TILTED";
    default: return "UNKNOWN";
    }
}

static const char *human_state_text(uint8_t state)
{
    switch (state)
    {
    case 0U: return "STILL";
    case 1U: return "RAISE";
    case 2U: return "WALK";
    case 3U: return "RUN";
    case 4U: return "FALL";
    case 5U: return "MOVE";
    default: return "UNKNOWN";
    }
}

static void card_art(lv_obj_t *card, lv_color_t accent, EnergyPage_t page)
{
    lv_obj_t *base = box(card, 312, 20, 78, 78, c_badge(), accent, 39);
    lv_obj_set_style_border_width(base, 2, 0);

    switch (page)
    {
    case ENERGY_PAGE_HOME:
        (void)box(base, 21, 28, 36, 28, c_panel_2(), accent, 3);
        (void)dot(base, 27, 20, 10, accent);
        (void)box(base, 34, 15, 16, 16, accent, accent, 2);
        break;
    case ENERGY_PAGE_CAR:
        (void)box(base, 15, 34, 48, 18, accent, accent, 5);
        (void)dot(base, 20, 50, 12, c_bg());
        (void)dot(base, 47, 50, 12, c_bg());
        (void)box(base, 26, 24, 25, 13, c_panel_2(), accent, 3);
        break;
    case ENERGY_PAGE_HUMAN:
        (void)dot(base, 29, 15, 20, accent);
        (void)box(base, 24, 38, 30, 25, accent, accent, 10);
        (void)dot(base, 15, 40, 10, c_panel_2());
        (void)dot(base, 54, 40, 10, c_panel_2());
        break;
    case ENERGY_PAGE_AI:
        (void)box(base, 18, 44, 8, 18, accent, accent, 2);
        (void)box(base, 32, 30, 8, 32, accent, accent, 2);
        (void)box(base, 46, 20, 8, 42, accent, accent, 2);
        (void)dot(base, 57, 14, 8, c_panel_2());
        break;
    default:
        break;
    }
}

static lv_obj_t *make_card(lv_obj_t *parent, const char *title, const char *sub,
                           lv_color_t accent, int16_t y, EnergyPage_t page)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, 24, y);
    lv_obj_set_size(btn, 432, 132);
    lv_obj_set_style_bg_color(btn, c_panel(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, accent, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_user_data(btn, (void *)(uintptr_t)page);

    (void)box(btn, 16, 18, 5, 96, accent, accent, 3);
    label(btn, title, 34, 26, &lv_font_montserrat_20, c_text());
    lv_obj_t *s = label(btn, sub, 34, 62, &lv_font_montserrat_14, c_muted());
    lv_obj_set_width(s, 245);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    label(btn, "Tap to open", 34, 100, &lv_font_montserrat_14, accent);
    card_art(btn, accent, page);
    return btn;
}

static void show_dashboard(void);
static void show_detail(EnergyPage_t page);

static void card_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        show_detail((EnergyPage_t)(uintptr_t)lv_obj_get_user_data(obj));
    }
}

static void back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        show_dashboard();
    }
}

static void build_header(lv_obj_t *scr)
{
    lv_obj_t *bar = box(scr, 0, 0, UI_W, UI_HEADER_H, lv_color_hex(0x101827),
                        lv_color_hex(0x101827), 0);
    label(bar, "Energy Hub", 22, 15, &lv_font_montserrat_20, c_text());
    label(bar, "PV / Home / Car / Human", 23, 45, &lv_font_montserrat_14, c_muted());
    s_ui.time = label(bar, "BJ --:--:--", 276, 16, &lv_font_montserrat_16, c_text());
    s_ui.cloud = label(bar, "Cloud: --", 276, 45, &lv_font_montserrat_14, c_muted());
}

static void ai_history_add(const EnergyLvglSnapshot_t *s)
{
    uint32_t now = HAL_GetTick();

    if (!s->ai_valid)
    {
        return;
    }
    if (s_ai_hist.count > 0U && (now - s_ai_hist.last_tick) < 10000U)
    {
        return;
    }

    s_ai_hist.pv[s_ai_hist.head] = s->ai_pv_p;
    s_ai_hist.load[s_ai_hist.head] = s->ai_load_p;
    s_ai_hist.soc[s_ai_hist.head] = s->ai_home_soc;
    s_ai_hist.head = (uint8_t)((s_ai_hist.head + 1U) % UI_AI_HISTORY_LEN);
    if (s_ai_hist.count < UI_AI_HISTORY_LEN)
    {
        s_ai_hist.count++;
    }
    s_ai_hist.last_tick = now;
    s_ai_hist.version++;
}

static void update_ai_history_text(void)
{
    uint8_t i;
    uint8_t idx;

    if (s_ui.ai_history_lines[0] == NULL)
    {
        return;
    }

    for (i = 0; i < 3U; i++)
    {
        if (i >= s_ai_hist.count)
        {
            set_label(s_ui.ai_history_lines[i], "--");
            continue;
        }
        idx = (uint8_t)((s_ai_hist.head + UI_AI_HISTORY_LEN - 1U - i) % UI_AI_HISTORY_LEN);
        set_line(s_ui.ai_history_lines[i],
                 "%u) PV %.2f W   Load %.2f W   SOC %.1f %%",
                 (unsigned int)(i + 1U),
                 s_ai_hist.pv[idx],
                 s_ai_hist.load[idx],
                 s_ai_hist.soc[idx]);
    }
}

static void update_dashboard(const EnergyLvglSnapshot_t *s)
{
    set_label(s_ui.time, s->time_valid ? s->beijing_time : "BJ time waiting");
    set_label(s_ui.cloud, s->onenet_online ? "Cloud: online" : "Cloud: offline");
    ai_history_add(s);
}

static void update_detail(const EnergyLvglSnapshot_t *s)
{
    set_label(s_ui.time, s->time_valid ? s->beijing_time : "BJ time waiting");
    set_label(s_ui.cloud, s->onenet_online ? "Cloud: online" : "Cloud: offline");
    ai_history_add(s);

    switch (s_ui.detail_page)
    {
    case ENERGY_PAGE_HOME:
        set_line(s_ui.detail_lines[0], "Home battery     %.2f V   %.3f A   %.2f W",
                 s->home_v, s->home_i, s->home_p);
        set_line(s_ui.detail_lines[1], "Home SOC         %.1f %%      sensor %s",
                 s->home_soc, s->home_ok ? "OK" : "ERR");
        set_line(s_ui.detail_lines[2], "Home load        %.2f V   %.3f A   %.2f W",
                 s->load_v, s->load_i, s->load_p);
        set_line(s_ui.detail_lines[3], "Solar PV         %.2f V   %.3f A   %.2f W",
                 s->pv_v, s->pv_i, s->pv_p);
        set_line(s_ui.detail_lines[4], "Light            %.1f lux     PV sensor %s",
                 s->lux, s->pv_ok ? "OK" : "ERR");
        set_line(s_ui.detail_lines[5], "Source MOS       PV %u   HOME %u   RIGID %u",
                 s->pvsrc, s->hsrc, s->rigid);
        set_line(s_ui.detail_lines[6], "Loads            LED %u   FAN %u   QI %u",
                 s->led, s->fan, s->qi);
        set_line(s_ui.detail_lines[7], "Charge / V2H     home %u   car %u   v2h %u",
                 s->hchg, s->cchg, s->v2h);
        break;
    case ENERGY_PAGE_CAR:
        set_line(s_ui.detail_lines[0], "Car SOC          %u %%", s->car_soc);
        set_line(s_ui.detail_lines[1], "Voltage          %.2f V", s->car_v);
        set_line(s_ui.detail_lines[2], "Current          %.0f mA", s->car_i_ma);
        set_line(s_ui.detail_lines[3], "Temperature      %.1f C", s->car_temp);
        set_line(s_ui.detail_lines[4], "Status           %s  (%u)",
                 car_status_text(s->car_status), s->car_status);
        set_line(s_ui.detail_lines[5], "Car charge MOS   %u", s->cchg);
        set_line(s_ui.detail_lines[6], "V2H request      %u", s->v2h);
        set_label(s_ui.detail_lines[7], "");
        break;
    case ENERGY_PAGE_HUMAN:
        set_line(s_ui.detail_lines[0], "ESP32-S3 valid   %u", s->human_valid);
        set_line(s_ui.detail_lines[1], "Battery          %.2f V   %.1f %%",
                 s->human_v, s->human_soc);
        set_line(s_ui.detail_lines[2], "Heart            %u bpm", s->human_hr);
        set_line(s_ui.detail_lines[3], "SpO2             %u %%", s->human_spo2);
        set_line(s_ui.detail_lines[4], "State            %s  (%u)",
                 human_state_text(s->human_state), s->human_state);
        set_line(s_ui.detail_lines[5], "Qi output        %u", s->qi);
        set_label(s_ui.detail_lines[6], "");
        set_label(s_ui.detail_lines[7], "");
        break;
    case ENERGY_PAGE_AI:
        set_line(s_ui.detail_lines[0], "Prediction valid       %u", s->ai_valid);
        set_line(s_ui.detail_lines[1], "Future PV power        %.2f W", s->ai_pv_p);
        set_line(s_ui.detail_lines[2], "Future load power      %.2f W", s->ai_load_p);
        set_line(s_ui.detail_lines[3], "Future home SOC        %.1f %%", s->ai_home_soc);
        set_line(s_ui.detail_lines[4], "Raw SOC from ESP32     %.1f %%", s->ai_raw_home_soc);
        set_line(s_ui.detail_lines[5], "Predicted surplus      %.2f W", s->ai_pv_p - s->ai_load_p);
        set_line(s_ui.detail_lines[6], "History samples        %u / %u",
                 s_ai_hist.count, UI_AI_HISTORY_LEN);
        set_line(s_ui.detail_lines[7], "Latest cached outputs are listed below");
        update_ai_history_text();
        break;
    default:
        break;
    }
}

static void update_timer_cb(lv_timer_t *timer)
{
    EnergyLvglSnapshot_t snap;
    (void)timer;

    EnergyLvgl_GetSnapshot(&snap);
    if (s_ui.in_detail)
    {
        update_detail(&snap);
    }
    else
    {
        update_dashboard(&snap);
    }
}

static void show_ota_page(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr, c_bg(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    (void)box(scr, 52, 250, 376, 250, c_panel(), c_ai(), 16);
    (void)dot(scr, 206, 285, 68, c_badge());
    (void)dot(scr, 224, 303, 32, c_ai());
    label(scr, "OTA Check", 176, 374, &lv_font_montserrat_20, c_text());
    label(scr, "No upgrade package found", 132, 415,
          &lv_font_montserrat_16, c_muted());
    label(scr, "Entering dashboard...", 154, 446,
          &lv_font_montserrat_14, c_ai());
    load_screen(scr);
}

static void ota_done_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    show_dashboard();
}

static void show_dashboard(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_t *card;
    EnergyLvglSnapshot_t snap;

    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.in_detail = 0U;
    lv_obj_set_style_bg_color(scr, c_bg(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    build_header(scr);

    label(scr, "System Overview", 24, 104, &lv_font_montserrat_20, c_text());
    label(scr, "Touch a card to inspect live data", 25, 134,
          &lv_font_montserrat_14, c_muted());

    card = make_card(scr, "Home Energy", "STM32F4 home node, PV input, battery, load and MOS states.",
                     c_home(), 178, ENERGY_PAGE_HOME);
    lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_CLICKED, NULL);

    card = make_card(scr, "Vehicle Battery", "CAN-linked car battery status, charging and V2H response.",
                     c_car(), 326, ENERGY_PAGE_CAR);
    lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_CLICKED, NULL);

    card = make_card(scr, "Human Node", "ESP32-S3 wearable battery, vital signs and Qi interaction.",
                     c_human(), 474, ENERGY_PAGE_HUMAN);
    lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_CLICKED, NULL);

    card = make_card(scr, "AI Forecast", "LSTM prediction of PV power, load power and home SOC.",
                     c_ai(), 622, ENERGY_PAGE_AI);
    lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_CLICKED, NULL);

    load_screen(scr);
    EnergyLvgl_GetSnapshot(&snap);
    update_dashboard(&snap);
}

static void show_detail(EnergyPage_t page)
{
    static const char *titles[] = {
        "Home Energy Detail",
        "Vehicle Battery Detail",
        "Human Node Detail",
        "AI Forecast Detail",
    };
    static const char *subs[] = {
        "PV source, home battery, load bus and MOS states",
        "CAN battery telemetry, charging and V2H status",
        "Wearable node telemetry and Qi interaction",
        "Cached LSTM outputs and short-term trend chart",
    };
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_t *back;
    lv_obj_t *hero;
    lv_color_t accent;
    EnergyLvglSnapshot_t snap;
    int i;
    int16_t row_y;
    int16_t row_step;
    int16_t line_y;
    int16_t row_h;

    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.in_detail = 1U;
    s_ui.detail_page = page;
    accent = page == ENERGY_PAGE_HOME ? c_home() :
             page == ENERGY_PAGE_CAR ? c_car() :
             page == ENERGY_PAGE_HUMAN ? c_human() : c_ai();

    lv_obj_set_style_bg_color(scr, c_bg(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    build_header(scr);

    back = lv_btn_create(scr);
    lv_obj_set_pos(back, 24, 102);
    lv_obj_set_size(back, 116, 48);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(back, c_text(), 0);
    lv_obj_set_style_border_width(back, 2, 0);
    lv_obj_set_style_radius(back, 12, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);
    label_center(back, "BACK", &lv_font_montserrat_16, c_text());

    s_ui.detail_title = label(scr, titles[page], 158, 111,
                              &lv_font_montserrat_20, accent);
    (void)s_ui.detail_title;

    hero = box(scr, 24, 166, 432, 82, c_panel(), accent, 14);
    (void)box(hero, 18, 18, 5, 46, accent, accent, 3);
    label(hero, titles[page], 38, 15, &lv_font_montserrat_20, c_text());
    lv_obj_t *sub = label(hero, subs[page], 38, 49, &lv_font_montserrat_14, c_muted());
    lv_obj_set_width(sub, 270);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    (void)dot(hero, 350, 18, 46, c_badge());
    (void)dot(hero, 363, 31, 20, accent);

    (void)box(scr, 24, 264, 432, 388,
              c_panel(), accent, 14);
    row_y = 286;
    row_step = 44;
    row_h = 36;
    for (i = 0; i < 8; i++)
    {
        (void)box(scr, 42, (int16_t)(row_y + i * row_step), 396,
                  row_h,
                  c_panel_2(), lv_color_hex(0x25314a), 8);
        line_y = (int16_t)(row_y + i * row_step + 8);
        s_ui.detail_lines[i] = label(scr, "", 56, line_y,
                                     &lv_font_montserrat_16, c_text());
        lv_obj_set_width(s_ui.detail_lines[i], 368);
        lv_label_set_long_mode(s_ui.detail_lines[i], LV_LABEL_LONG_DOT);
    }

    if (page == ENERGY_PAGE_AI)
    {
        label(scr, "Prediction History", 28, 662, &lv_font_montserrat_20, c_text());
        label(scr, "Recent cached LSTM outputs", 29, 690, &lv_font_montserrat_14, c_muted());
        for (i = 0; i < 3; i++)
        {
            (void)box(scr, 42, (int16_t)(716 + i * 34), 396, 28,
                      c_panel_2(), lv_color_hex(0x25314a), 8);
            s_ui.ai_history_lines[i] = label(scr, "", 54, (int16_t)(722 + i * 34),
                                             &lv_font_montserrat_14, c_text());
            lv_obj_set_width(s_ui.ai_history_lines[i], 372);
            lv_label_set_long_mode(s_ui.ai_history_lines[i], LV_LABEL_LONG_DOT);
        }
    }

    load_screen(scr);
    EnergyLvgl_GetSnapshot(&snap);
    update_detail(&snap);
}

void EnergyLvgl_Task(void const *argument)
{
    lv_timer_t *ota_timer;

    (void)argument;

    lv_init();
    EnergyLvgl_PortInit();
    ota_timer = lv_timer_create(ota_done_cb,
                                APP_UI_OTA_PAGE_DURATION_MS, NULL);
    s_update_timer = lv_timer_create(update_timer_cb, 1000, NULL);
    if (ota_timer == NULL || s_update_timer == NULL)
    {
        osDelay(1000U);
        NVIC_SystemReset();
    }
    show_ota_page();
    lv_refr_now(NULL);
    AppHealth_Heartbeat(APP_HEALTH_TASK_UI);
    EnergyLvgl_TouchInit();

    for (;;)
    {
        AppHealth_Heartbeat(APP_HEALTH_TASK_UI);
        lv_timer_handler();
        osDelay(10);
    }
}

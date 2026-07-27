#include "onenet_service.h"

#include "app_clock.h"
#include "app_health.h"
#include "app_state.h"
#include "can_app.h"
#include "cmsis_os.h"
#include "energy_service.h"
#include "esp8266_onenet.h"
#include "esp8266_onenet_at.h"
#include "main.h"
#include "onenet_runtime.h"

#include <stdio.h>
#include <string.h>

#ifndef ONENET_TASK_LOG
#ifdef NDEBUG
#define ONENET_TASK_LOG 0U
#else
#define ONENET_TASK_LOG 1U
#endif
#endif

#define ONENET_FULL_UPLOAD_MS              10000U
#define ONENET_SWITCH_SYNC_MS                500U
#define ONENET_SWITCH_HEARTBEAT_MS         30000U
#define ONENET_PING_INTERVAL_MS            60000U
#define ONENET_PING_TIMEOUT_MS             15000U
#define ONENET_STARTUP_DELAY_MS            12000U
#define ONENET_SNTP_RETRY_MS              300000U
#define ONENET_RX_POLL_MS                     20U
#define ONENET_TASK_LOOP_MS                   20U

static void OneNET_FillUploadData(OneNET_UploadData_t *upload,
                                  const AppStateSnapshot_t *app,
                                  const EnergyOutputState_t *output,
                                  const CAN_BatteryData_t *battery)
{
    if (upload == NULL || app == NULL || output == NULL || battery == NULL)
    {
        return;
    }

    upload->car_soc = battery->soc_pct;
    upload->car_status = (uint8_t)battery->status;
    upload->home_feng = output->fan;
    upload->home_led = output->led;
    upload->home_load = output->rigid;
    upload->home_soc = app->sensor.soc_pct;

    if (output->pv_source && output->home_charge)
    {
        upload->home_status = 6U;
    }
    else if (output->pv_source && output->car_charge)
    {
        upload->home_status = 7U;
    }
    else if (output->home_charge)
    {
        upload->home_status = 1U;
    }
    else if (output->car_charge)
    {
        upload->home_status = 2U;
    }
    else if (output->pv_source)
    {
        upload->home_status = 3U;
    }
    else if (output->home_source)
    {
        upload->home_status = 4U;
    }
    else if (output->v2h)
    {
        upload->home_status = 5U;
    }
    else
    {
        upload->home_status = 0U;
    }

    upload->human_heart = app->wearable.valid ? app->wearable.hr : 0U;
    upload->human_soc = app->wearable.valid ? app->wearable.bat_pct : -1.0f;
    upload->human_spo2 = app->wearable.valid ? app->wearable.spo2 : 0U;
    upload->human_status = app->wearable.valid ? app->wearable.state : 0U;
    upload->load_power = app->sensor.ina3_power;
    upload->lux = app->sensor.lux;
    upload->pv_power = app->sensor.pv_power;
    upload->qi = output->qi;
}

static void OneNET_UpdateBeijingTimeBeforeMqtt(void)
{
    char time_buf[APP_STATE_BEIJING_TIME_LENGTH];

    if (ONENET_TASK_LOG)
    {
        printf("ONENET: bounded SNTP sync\r\n");
    }
    if (ESP8266_ONENET_AT_GetSntpTime(time_buf, sizeof(time_buf)) != 0U)
    {
        if (ONENET_TASK_LOG)
        {
            printf("ONENET: SNTP unavailable, continue MQTT\r\n");
        }
        return;
    }

    (void)AppClock_SetFromSntp(time_buf);
    (void)AppState_GetBeijingTime(NULL, time_buf, sizeof(time_buf));
    if (ONENET_TASK_LOG)
    {
        printf("ONENET: SNTP ok %s\r\n", time_buf);
    }
}

static void OneNET_UpdateBeijingTimeOnline(void)
{
    char time_buf[APP_STATE_BEIJING_TIME_LENGTH];

    if (ESP8266_ONENET_AT_QuerySntpTimeOnline(
            time_buf, sizeof(time_buf)) != 0U)
    {
        if (ONENET_TASK_LOG)
        {
            printf("ONENET: online SNTP retry unavailable\r\n");
        }
        return;
    }

    (void)AppClock_SetFromSntp(time_buf);
    (void)AppState_GetBeijingTime(NULL, time_buf, sizeof(time_buf));
    if (ONENET_TASK_LOG)
    {
        printf("ONENET: SNTP ok %s\r\n", time_buf);
    }
}

static uint8_t OneNET_ReplyControlRequest(const OneNET_Control_t *ctrl,
                                          uint16_t code,
                                          const char *message)
{
    if (ctrl == NULL)
    {
        return 1U;
    }
    if (ctrl->request_source != ONENET_REQUEST_SOURCE_PROPERTY_SET)
    {
        return 0U;
    }
    if (!ctrl->request_id_valid)
    {
        if (ONENET_TASK_LOG)
        {
            printf("ONENET: property/set has no valid request id\r\n");
        }
        return 1U;
    }

    return OneNET_MQTT_ReplyPropertySet(ctrl->request_id, code, message);
}

static const char *OneNET_DisconnectReasonName(
    OneNET_DisconnectReason_t reason)
{
    switch (reason)
    {
        case ONENET_DISCONNECT_WIFI_INIT: return "wifi-init";
        case ONENET_DISCONNECT_TCP_OPEN: return "tcp-open";
        case ONENET_DISCONNECT_MQTT_CONNECT: return "mqtt-connect";
        case ONENET_DISCONNECT_SUBSCRIBE: return "subscribe";
        case ONENET_DISCONNECT_LINK_CLOSED: return "link-closed";
        case ONENET_DISCONNECT_PROTOCOL: return "rx-protocol";
        case ONENET_DISCONNECT_PING_SEND: return "ping-send";
        case ONENET_DISCONNECT_PING_TIMEOUT: return "ping-timeout";
        case ONENET_DISCONNECT_TELEMETRY: return "telemetry";
        case ONENET_DISCONNECT_SWITCH_SYNC: return "switch-sync";
        default: return "unknown";
    }
}

static void OneNET_EnterReconnect(OneNET_Runtime_t *runtime,
                                  OneNET_DisconnectReason_t reason)
{
    uint32_t now = HAL_GetTick();

    OneNET_Runtime_ScheduleReconnect(runtime, now, reason);
    (void)AppState_SetOneNETOnline(0U);
    (void)ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_CLOSE,
                                    ESP8266_AT_RSP_OK, 1000U);
    if (ONENET_TASK_LOG)
    {
        printf("ONENET: offline reason=%s retry_in=%lums\r\n",
               OneNET_DisconnectReasonName(reason),
               (unsigned long)OneNET_Runtime_GetBackoffRemaining(
                   runtime, HAL_GetTick()));
    }
}

void OneNET_ServiceTask(void const *argument)
{
    static AppStateSnapshot_t app_snapshot;
    EnergyOutputState_t output_state;
    CAN_BatteryData_t battery_snapshot;
    OneNET_UploadData_t upload;
    OneNET_Control_t ctrl;
    uint32_t last_upload_tick = 0U;
    uint32_t last_switch_sync_tick = 0U;
    uint32_t last_switch_heartbeat_tick = 0U;
    uint8_t prev_fan_on = 0xFFU;
    uint8_t prev_led_on = 0xFFU;
    uint8_t prev_rigid_on = 0xFFU;
    uint8_t prev_qi_on = 0xFFU;
    uint8_t pending_switch_ack = 0U;
    OneNET_Runtime_t runtime;
    OneNET_MQTTRxResult_t process_result;
    OneNET_DisconnectReason_t connect_error;
    EnergyActuatorCommand_t actuator_command;
    uint32_t last_sntp_attempt_tick = 0U;
    uint8_t sntp_attempted = 0U;

    (void)argument;
    if (ONENET_TASK_LOG)
    {
        printf("ONENET: wait module startup %ums\r\n",
               ONENET_STARTUP_DELAY_MS);
    }
    OneNET_Runtime_Init(&runtime, HAL_GetTick(), ONENET_STARTUP_DELAY_MS);

    for (;;)
    {
        AppHealth_Heartbeat(APP_HEALTH_TASK_ONENET);
        AppClock_Service();

        /* Keepalive service must run before any early loop exit. */
        OneNET_Runtime_Service(&runtime, HAL_GetTick());
        if (!OneNET_Runtime_IsOnline(&runtime))
        {
            (void)AppState_SetOneNETOnline(0U);
            if (!OneNET_Runtime_CanConnect(&runtime, HAL_GetTick()))
            {
                osDelay(ONENET_TASK_LOOP_MS);
                continue;
            }

            OneNET_Runtime_BeginConnect(&runtime);
            connect_error = ONENET_DISCONNECT_NONE;
            if (ONENET_TASK_LOG)
            {
                printf("ONENET: connect attempt=%lu\r\n",
                       (unsigned long)(runtime.reconnect_count + 1U));
            }

            if (ESP8266_ONENET_AT_InitWiFi() != 0U)
            {
                connect_error = ONENET_DISCONNECT_WIFI_INIT;
            }
            else
            {
                if (!AppClock_IsValid() &&
                    (!sntp_attempted ||
                     (HAL_GetTick() - last_sntp_attempt_tick) >=
                         ONENET_SNTP_RETRY_MS))
                {
                    sntp_attempted = 1U;
                    last_sntp_attempt_tick = HAL_GetTick();
                    OneNET_UpdateBeijingTimeBeforeMqtt();
                }

                if (OneNET_MQTT_Open() != 0U)
                {
                    connect_error = ONENET_DISCONNECT_TCP_OPEN;
                }
                else if (OneNET_MQTT_Connect() != 0U)
                {
                    connect_error = ONENET_DISCONNECT_MQTT_CONNECT;
                }
                else if (OneNET_MQTT_SubscribeControl() != 0U)
                {
                    connect_error = ONENET_DISCONNECT_SUBSCRIBE;
                }
            }

            if (connect_error != ONENET_DISCONNECT_NONE)
            {
                OneNET_EnterReconnect(&runtime, connect_error);
                osDelay(ONENET_TASK_LOOP_MS);
                continue;
            }

            OneNET_Runtime_MarkOnline(&runtime, HAL_GetTick());
            (void)AppState_SetOneNETOnline(1U);
            last_upload_tick = 0U;
            last_switch_sync_tick = 0U;
            last_switch_heartbeat_tick = 0U;
            prev_fan_on = 0xFFU;
            prev_led_on = 0xFFU;
            prev_rigid_on = 0xFFU;
            prev_qi_on = 0xFFU;
            pending_switch_ack = 1U;
            if (ONENET_TASK_LOG)
            {
                printf("ONENET: mqtt online on USART2\r\n");
            }
        }

        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.home_feng = -1;
        ctrl.home_led = -1;
        ctrl.home_load = -1;
        ctrl.qi = -1;
        process_result = OneNET_MQTT_Process(&ctrl, ONENET_RX_POLL_MS);
        if (process_result == ONENET_MQTT_RX_LINK_CLOSED)
        {
            OneNET_EnterReconnect(&runtime,
                                  ONENET_DISCONNECT_LINK_CLOSED);
            continue;
        }
        if (process_result == ONENET_MQTT_RX_PROTOCOL_ERROR)
        {
            if (OneNET_Runtime_RecordProtocol(&runtime, 0U))
            {
                OneNET_EnterReconnect(&runtime,
                                      ONENET_DISCONNECT_PROTOCOL);
                continue;
            }
        }
        else if (process_result != ONENET_MQTT_RX_IDLE)
        {
            (void)OneNET_Runtime_RecordProtocol(&runtime, 1U);
        }

        if (process_result == ONENET_MQTT_RX_PING_RESPONSE)
        {
            OneNET_Runtime_MarkPingResponse(&runtime, HAL_GetTick());
        }
        if (OneNET_Runtime_IsPingTimedOut(&runtime, HAL_GetTick(),
                                          ONENET_PING_TIMEOUT_MS))
        {
            OneNET_EnterReconnect(&runtime,
                                  ONENET_DISCONNECT_PING_TIMEOUT);
            continue;
        }
        if (OneNET_Runtime_ShouldPing(&runtime, HAL_GetTick(),
                                      ONENET_PING_INTERVAL_MS))
        {
            if (OneNET_MQTT_Ping() == 0U)
            {
                OneNET_Runtime_MarkPingSent(&runtime, HAL_GetTick());
                (void)OneNET_Runtime_RecordOperation(
                    &runtime, ONENET_OPERATION_PING, 1U);
            }
            else if (OneNET_Runtime_RecordOperation(
                         &runtime, ONENET_OPERATION_PING, 0U))
            {
                OneNET_EnterReconnect(&runtime,
                                      ONENET_DISCONNECT_PING_SEND);
                continue;
            }
        }

        if (!AppClock_IsValid() && sntp_attempted &&
            process_result == ONENET_MQTT_RX_IDLE &&
            runtime.ping_outstanding == 0U &&
            (HAL_GetTick() - last_sntp_attempt_tick) >=
                ONENET_SNTP_RETRY_MS)
        {
            last_sntp_attempt_tick = HAL_GetTick();
            OneNET_UpdateBeijingTimeOnline();
        }

        if (ctrl.request_received &&
            process_result == ONENET_MQTT_RX_PAYLOAD_ERROR)
        {
            (void)OneNET_ReplyControlRequest(
                &ctrl, 400U, "invalid control parameters");
            if (ONENET_TASK_LOG)
            {
                printf("ONENET: rejected invalid property/set payload\r\n");
            }
            osDelay(100U);
            continue;
        }
        if (process_result == ONENET_MQTT_RX_PROPERTY_SET && ctrl.updated)
        {
            uint8_t reply_result;

            actuator_command.fan = ctrl.home_feng;
            actuator_command.led = ctrl.home_led;
            actuator_command.load = ctrl.home_load;
            actuator_command.qi = ctrl.qi;
            if (EnergyService_SubmitCommand(&actuator_command) == 0U)
            {
                last_switch_sync_tick = HAL_GetTick();
                pending_switch_ack = 1U;
                if (ONENET_TASK_LOG)
                {
                    printf("ONENET: ctrl queued feng=%d led=%d load=%d "
                           "qi=%d\r\n",
                           ctrl.home_feng, ctrl.home_led,
                           ctrl.home_load, ctrl.qi);
                }
                reply_result = OneNET_ReplyControlRequest(
                    &ctrl, 200U, "success");
            }
            else
            {
                if (ONENET_TASK_LOG)
                {
                    printf("ONENET: control queue full\r\n");
                }
                reply_result = OneNET_ReplyControlRequest(
                    &ctrl, 503U, "control queue busy");
            }

            if (OneNET_Runtime_RecordOperation(
                    &runtime, ONENET_OPERATION_CONTROL_REPLY,
                    reply_result == 0U ? 1U : 0U))
            {
                OneNET_EnterReconnect(&runtime,
                                      ONENET_DISCONNECT_PROTOCOL);
                continue;
            }
        }

        if ((HAL_GetTick() - last_upload_tick) >=
            ONENET_FULL_UPLOAD_MS)
        {
            last_upload_tick = HAL_GetTick();
            if (AppState_GetSnapshot(&app_snapshot) != 0U)
            {
                if (ONENET_TASK_LOG)
                {
                    printf("ONENET: application snapshot unavailable\r\n");
                }
                osDelay(ONENET_TASK_LOOP_MS);
                continue;
            }
            EnergyService_GetOutputState(&output_state);
            CAN_App_GetBatterySnapshot(&battery_snapshot);
            OneNET_FillUploadData(&upload, &app_snapshot, &output_state,
                                  &battery_snapshot);
            if (ONENET_TASK_LOG)
            {
                printf("ONENET: upload full tick=%lu pv=%lumW load=%lumW "
                       "home=%u%% car=%u%% human=%u%%\r\n",
                       (unsigned long)HAL_GetTick(),
                       (unsigned long)(upload.pv_power > 0.0f ?
                           upload.pv_power * 1000.0f : 0.0f),
                       (unsigned long)(upload.load_power > 0.0f ?
                           upload.load_power * 1000.0f : 0.0f),
                       (unsigned int)upload.home_soc,
                       upload.car_soc,
                       (unsigned int)upload.human_soc);
            }
            if (OneNET_Upload(&upload) != 0U)
            {
                uint8_t reconnect_required =
                    OneNET_Runtime_RecordOperation(
                        &runtime, ONENET_OPERATION_TELEMETRY, 0U);
                if (ONENET_TASK_LOG)
                {
                    printf("ONENET: full upload failed count=%u\r\n",
                           runtime.operation_failures[
                               ONENET_OPERATION_TELEMETRY]);
                }
                if (reconnect_required)
                {
                    OneNET_EnterReconnect(
                        &runtime, ONENET_DISCONNECT_TELEMETRY);
                }
                continue;
            }
            if (ONENET_TASK_LOG)
            {
                printf("ONENET: full upload ok\r\n");
            }
            (void)OneNET_Runtime_RecordOperation(
                &runtime, ONENET_OPERATION_TELEMETRY, 1U);
        }

        if ((pending_switch_ack &&
             (HAL_GetTick() - last_switch_sync_tick) >=
                 ONENET_SWITCH_SYNC_MS) ||
            ((HAL_GetTick() - last_switch_heartbeat_tick) >=
                 ONENET_SWITCH_HEARTBEAT_MS))
        {
            uint8_t fan_on;
            uint8_t led_on;
            uint8_t rigid_on;
            uint8_t qi_on;

            EnergyService_GetOutputState(&output_state);
            fan_on = output_state.fan;
            led_on = output_state.led;
            rigid_on = output_state.rigid;
            qi_on = output_state.qi;

            if (fan_on != prev_fan_on ||
                led_on != prev_led_on ||
                rigid_on != prev_rigid_on ||
                qi_on != prev_qi_on ||
                last_switch_sync_tick == 0U ||
                (HAL_GetTick() - last_switch_heartbeat_tick) >=
                    ONENET_SWITCH_HEARTBEAT_MS)
            {
                last_switch_sync_tick = HAL_GetTick();
                if (OneNET_UploadSwitchStates(
                        fan_on, led_on, rigid_on, qi_on) != 0U)
                {
                    uint8_t reconnect_required =
                        OneNET_Runtime_RecordOperation(
                            &runtime, ONENET_OPERATION_SWITCH_SYNC, 0U);
                    if (ONENET_TASK_LOG)
                    {
                        printf("ONENET: switch upload failed count=%u\r\n",
                               runtime.operation_failures[
                                   ONENET_OPERATION_SWITCH_SYNC]);
                    }
                    if (reconnect_required)
                    {
                        OneNET_EnterReconnect(
                            &runtime, ONENET_DISCONNECT_SWITCH_SYNC);
                    }
                    continue;
                }
                if (ONENET_TASK_LOG)
                {
                    printf("ONENET: switch upload ok fan=%u led=%u "
                           "load=%u qi=%u\r\n",
                           fan_on, led_on, rigid_on, qi_on);
                }
                (void)OneNET_Runtime_RecordOperation(
                    &runtime, ONENET_OPERATION_SWITCH_SYNC, 1U);
                prev_fan_on = fan_on;
                prev_led_on = led_on;
                prev_rigid_on = rigid_on;
                prev_qi_on = qi_on;
            }

            last_switch_sync_tick = HAL_GetTick();
            last_switch_heartbeat_tick = HAL_GetTick();
            pending_switch_ack = 0U;
        }

        osDelay(ONENET_TASK_LOOP_MS);
    }
}

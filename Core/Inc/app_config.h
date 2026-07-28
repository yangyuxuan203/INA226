#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Feature switches. Health checks stay active when their serial log is off. */
#define APP_LSTM_PREDICTION_ENABLE       1U
#define APP_LSTM_DISPATCH_ENABLE         0U
#define APP_LSTM_PREDICTION_LOG_ENABLE   1U
#define APP_HEALTH_SERIAL_LOG_ENABLE     0U
#define APP_COMM_SERIAL_LOG_ENABLE       0U

/* ESP32 state upload stays active even when prediction is disabled. */
#define APP_LSTM_INPUT_SCHEMA_VERSION    1U
#define APP_ESP32_STATE_TX_PERIOD_MS   2000U
#define APP_ESP32_PEER_TIMEOUT_MS     15000U
#define APP_ESP32_TX_FAILURE_LIMIT        3U

/* Keep the OTA startup page visible while background tasks initialize. */
#define APP_UI_OTA_PAGE_DURATION_MS    3000U

/* Six baseline data-collection scenes. */
#define APP_SCENE_T1_NIGHT_NO_PV         1U
#define APP_SCENE_T2_DAWN_WEAK_LIGHT     2U
#define APP_SCENE_T3_NOON_STRONG_LIGHT   3U
#define APP_SCENE_T4_CAR_CHARGE          4U
#define APP_SCENE_T5_DUSK_HIGH_LOAD      5U
#define APP_SCENE_T6_LOW_HOME_V2H        6U

/* Change only this value when collecting a different scene. */
#define APP_DATA_SCENE_ID                APP_SCENE_T1_NIGHT_NO_PV

#define APP_DATA_LOG_ENABLE              1U
#define APP_DATA_LOG_PERIOD_MS        2000U

#if (APP_DATA_SCENE_ID < APP_SCENE_T1_NIGHT_NO_PV) || \
    (APP_DATA_SCENE_ID > APP_SCENE_T6_LOW_HOME_V2H)
#error "APP_DATA_SCENE_ID must be in the range 1..6"
#endif

#if (APP_DATA_LOG_PERIOD_MS == 0U)
#error "APP_DATA_LOG_PERIOD_MS must be greater than zero"
#endif

#if (APP_UI_OTA_PAGE_DURATION_MS == 0U)
#error "APP_UI_OTA_PAGE_DURATION_MS must be greater than zero"
#endif

#endif /* APP_CONFIG_H */

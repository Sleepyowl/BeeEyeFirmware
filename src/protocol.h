#pragma once
#include <stdint.h>

#define BEE_EYE_MAGIC 0xBEEF

#define BEE_EYE_TIMER_RESOLUTION_MASK 0xC0
#define BEE_EYE_TIMER_1000HZ 0
#define BEE_EYE_TIMER_100HZ 0x40
#define BEE_EYE_TIMER_64HZ 0x80
#define BEE_EYE_TIMER_1HZ 0xC0

#define BEE_EYE_BATTERY_MASK 0x30
#define BEE_EYE_BATTERY_3V3 0
#define BEE_EYE_BATTERY_4V5 0x10
#define BEE_EYE_BATTERY_6V 0x20
#define BEE_EYE_BATTERY_RESERVED 0x30

#define BEE_EYE_METRIC_TYPE_MASK 0x0F
#define BEE_EYE_METRIC_TEMPHUM 0
#define BEE_EYE_METRIC_WEIGHT 1

struct __attribute__((packed)) ManufacturerData {
    uint16_t            magic;              // must be 0xBEEF
    uint8_t             flags;              // bits 0-3: metric type, bits 4-5: battery type, bits 6-7: next transmission timer resolution
    union {
        struct __attribute__((packed)) {
            int16_t temp;
            int16_t hum;
        } th;

        struct __attribute__((packed)) {
            int32_t weight;
        } w;
    } data;
    uint16_t            nextTransmission;    // delay until next transmission
    uint16_t            batteryMilliVolt;   // battery voltage
    uint8_t             crc;
};

#pragma once

#include <stdint.h>

#define PROTOCOL_VERSION 3

#pragma pack(push, 1)

#define BEE_EYE_MESSAGE_TYPE_MEASURES 1u
#define BEE_EYE_MESSAGE_TYPE_TEXT 2u
#define BEE_EYE_MEASURE_TYPE_BATTERY 0u
#define BEE_EYE_MEASURE_TYPE_TEMPERATURE 1u
#define BEE_EYE_MEASURE_TYPE_TEMPHUM 2u
#define BEE_EYE_MEASURE_TYPE_WEIGHT 3u

/// @brief Sensor measure
struct Measure {   
    uint8_t         type; 
    uint8_t         sensorAddress[8];
    union {
        struct __attribute__((packed)) {
            float tempC;
            float hum;
        } th;

        struct __attribute__((packed)) {
            float weight;
        } w;

        struct __attribute__((packed)) {
            uint16_t mV;
        } bat;
    } data;
}; 

/// @brief Message header
struct MessageHeader {
    uint8_t     magic[4];
    uint8_t     flags; // version && flags    
    uint64_t    srcAddr;
    uint8_t     type;
};

#pragma pack(pop)

void fillHeader(void* target, uint8_t type);
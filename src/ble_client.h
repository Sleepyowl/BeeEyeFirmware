#pragma once

#include <stdint.h>

#define SENSOR_TYPE_TEMPHUM 0
#define SENSOR_TYPE_WEIGHT 1

struct Sensor {
	uint64_t	lastReceive;
	uint8_t		address[6];
	uint8_t		type;
	union {
		struct {
			int16_t rawTemperature;
			int16_t rawHumidity;
		} th;

		struct {
			int32_t rawWeight;
		} w;
	} data;	
	uint64_t	nextAnnounce;
};

int ble_client_start(int interval, int window);
int ble_client_stop(void);
uint64_t ble_client_get_next_sensor_window(void);
uint16_t ble_client_get_measure_count();
uint8_t ble_get_sensor_count(void);
struct Sensor* ble_get_sensor(uint8_t index);
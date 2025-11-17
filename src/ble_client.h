#pragma once

#include <stdint.h>

struct Sensor {
	uint64_t	lastReceive;
	uint8_t		address[6];
	uint16_t	rawTemperature;
	uint16_t	rawHumidity;
};

int ble_client_start(int interval, int window);
int ble_client_stop(void);
uint64_t ble_client_get_next_sensor_window(void);
uint16_t ble_client_get_measure_count();
uint8_t ble_get_sensor_count(void);
struct Sensor* ble_get_sensor(uint8_t index);
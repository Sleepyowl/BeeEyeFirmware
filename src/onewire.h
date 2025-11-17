#pragma once

#include <stdint.h>

void enum_w1(void);
uint8_t get_w1_device_count(void);

// copies 8-byte ROM (address)
int get_w1_address(void* dest, uint8_t sensor_index);
float read_temp(uint8_t sensor_index);
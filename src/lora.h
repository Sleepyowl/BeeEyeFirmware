#pragma once

#include "messages.h"

int init_lora(void);
int lora_transmit_text(const char *text);
int lora_transmit_measures(const struct Measure *measure, uint8_t count);
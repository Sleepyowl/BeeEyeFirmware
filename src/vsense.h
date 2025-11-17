#pragma once
#include <stdbool.h>

void vsense_enable(bool enable);
int vsense_read_mv(void);
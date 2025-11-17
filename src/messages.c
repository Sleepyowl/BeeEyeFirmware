#include "messages.h"

#include <stdint.h>
#include <soc.h> 

#define MAGIC_BEYE 0x45455942ul

void fillHeader(void* target, uint8_t type) {
    struct MessageHeader *header = target;
    *((uint32_t*)header->magic) = MAGIC_BEYE;
    header->flags = (PROTOCOL_VERSION & 63);
    header->type = type;
    header->srcAddr = ((uint64_t)NRF_FICR->DEVICEID[1] << 32) | (uint64_t)NRF_FICR->DEVICEID[0];
}
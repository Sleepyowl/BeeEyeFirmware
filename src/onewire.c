#include "onewire.h"
#include "uart_print.h"

#include <zephyr/drivers/w1.h>

const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ow0));

static struct w1_rom roms[16];
static volatile uint8_t rom_count;

void device_found_cb(struct w1_rom rom, void *user_data)
{
    roms[rom_count] = rom;
	uart_printf("Device found; family: 0x%02x, serial: 0x%016llx\n", rom.family,
		w1_rom_to_uint64(&rom));
    ++rom_count;
}

void enum_w1(void) {
	if (!device_is_ready(dev)) {
		uart_printf("W1 not ready\n");
		return;
	}

    // if (!w1_reset_bus(dev)) {
    //     uart_printf("No w1 slaves responded\n");
    // } else {
    //     uart_printf("W1 slave presence detected\n");
    // }

    rom_count = 0;
    int n = w1_search_bus(dev, W1_CMD_SEARCH_ROM, W1_SEARCH_ALL_FAMILIES, device_found_cb, NULL);
    if (n < 0) uart_printf("search err %d\n", n);
    else uart_printf("found %d\n", n);

}

uint8_t get_w1_device_count(void) {
    return rom_count;
}

int get_w1_address(void* dest, uint8_t sensor_index) {
    if(sensor_index >= rom_count || dest == NULL) {
        return -EINVAL;
    }

    memcpy(dest, roms + sensor_index, 8);
    return 0;
}

float read_temp(uint8_t sensor_index) {
    if(sensor_index >= rom_count) return -1024;

    uart_printf("Reading temperature from {family: 0x%02x, serial: 0x%016llx}\n", 
        roms[sensor_index].family,
		w1_rom_to_uint64(roms));

    struct w1_slave_config config;
    memset(&config, 0, sizeof(struct w1_slave_config));
    memcpy(&config.rom, roms + sensor_index, 8);

    w1_reset_bus(dev);
    w1_match_rom(dev, &config);
    w1_write_byte(dev, 0x44);  // Convert T
    k_msleep(750);             // wait for conversion
    w1_reset_bus(dev);
    w1_match_rom(dev, &config);
    w1_write_byte(dev, 0xBE);  // Read Scratchpad
    uint8_t buf[9];
    for (int i=0;i<9;i++) buf[i]=w1_read_byte(dev);
    int16_t raw = (buf[1]<<8)|buf[0];
    return raw / 16.0f;
}
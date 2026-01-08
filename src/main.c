#include "uart_print.h"
#include "vsense.h"
#include "rtc.h"
#include "onewire.h"
#include "lora.h"
#include "messages.h"
#include "ble_client.h"

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_gpio.h>
#include <nrfx.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_DBG);

#define LED0_NODE DT_NODELABEL(status_led)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
// static const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

#define MAX_MEASURES 8
struct Measure	measures[MAX_MEASURES];

void fillMeasureFromSensor(struct Measure* measure, struct Sensor* sensor);
static uint8_t battery_addr_suffix[6];
static void battery_addr_init(void);

int main(void)
{
	int ret = 0;
	battery_addr_init();

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("GPIO is not ready");
		return 0;
	}


	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return 0;
	} 

	nrf_gpio_cfg(
		led.pin,                    // pin number
		NRF_GPIO_PIN_DIR_OUTPUT,
		NRF_GPIO_PIN_INPUT_DISCONNECT,
		NRF_GPIO_PIN_NOPULL,
		NRF_GPIO_PIN_S0H1,     // <— drive mode
		NRF_GPIO_PIN_NOSENSE
	);

	// Blink the led
	gpio_pin_set_dt(&led, 1);
	k_msleep(250);
	gpio_pin_set_dt(&led, 0);

	ret = intinitialize_rtc();
	if(ret) {
		LOG_ERR("RTC init failed: %d", ret);
	} else {
		LOG_INF("RTC OK");
	}

	// Vsense
	uint16_t batteryMilliVolt = 0;
	ret = vsense_measure_mv(&batteryMilliVolt);
	if(ret) {
		LOG_ERR("Battery Voltage measurement failed");
	}
	LOG_INF("Battery Voltage = %dmV", batteryMilliVolt);

	// Start BLE
	ret = ble_client_start(60, 30);
	if(ret) {
		LOG_ERR("BLE client init failed: %d", ret);
	} else {
		LOG_INF("BLE OK");
	}

	k_msleep(100);

	ret = init_lora();
	if(ret){
		LOG_ERR("LoRa init failed: %d", ret);
	} else {
		LOG_INF("LoRa OK");
	}

	// Send banner
	ret=lora_transmit_text("BeeEye v0.3-nrf");
	if(ret) {
		LOG_ERR("LoRa TX failed: %d", ret);
	}

	uint64_t start = k_uptime_get();
	uint64_t t_stop_hifreq = start + 180000;
	uint64_t t_start_hifreq = 0;
	uint64_t t_start_transmit = start;

	while (1) {
		uint64_t now = k_uptime_get();

		if (t_stop_hifreq != 0 && t_stop_hifreq <= now) {
			t_stop_hifreq = 0;
			LOG_INF("Stopping ble scan, sensors found so far = %d", ble_get_sensor_count());
			ble_client_stop();
			LOG_INF("Switching to low-freq ble scan, sensors found so far = %d", ble_get_sensor_count());
			ble_client_start(2000, 30); // low freq

			t_start_hifreq = ble_client_get_next_sensor_window();
			if(t_start_hifreq == 0) {
				LOG_WRN("No devices detected, scheduling next hi-freq scan in 2 minutes");
				t_start_hifreq = now + 120000;
			} else {
				LOG_INF("Next hi-freq in %d seconds", (int)((t_start_hifreq - now) / 1000));
			}
		}

		if (t_start_hifreq != 0 && t_start_hifreq <= now) {
			t_start_hifreq = 0;
			LOG_INF("Switching to high-freq ble scan");
			ble_client_start(60, 30); // high freq
			t_stop_hifreq = now + 300; 
		}

		if (t_start_transmit != 0 && t_start_transmit <= now) {
			uint64_t cutoff = now - 300000; // ignore sensors with last measure before last transmit
			uint8_t measures_to_transmit = 0;
			for(int i = 0; i < ble_get_sensor_count(); ++i) {
				struct Sensor *sensor = ble_get_sensor(i);
				if (sensor->lastReceive < cutoff) continue;
				fillMeasureFromSensor(measures + measures_to_transmit, sensor);
				++measures_to_transmit;
			}
			// One Wire
			enum_w1();
			for(int i = 0; i < get_w1_device_count(); ++i) {
				struct Measure* measure = measures + measures_to_transmit;
				++measures_to_transmit;

				measure->type = BEE_EYE_MEASURE_TYPE_TEMPERATURE;
				measure->_data1.tempC = read_temp(i);
				get_w1_address(measure->sensorAddress, i);
			}

			// Battery
			uint16_t mv = 0;
			ret = vsense_measure_mv(&mv);
			if(ret) {
				LOG_ERR("Couldn't get battery voltage %d", ret);
			} else {
				if(measures_to_transmit < MAX_MEASURES) {
					measures[measures_to_transmit].type = BEE_EYE_MEASURE_TYPE_BATTERY;
					measures[measures_to_transmit].sensorAddress[0] = 'B';
					measures[measures_to_transmit].sensorAddress[1] = 'T';	
					memcpy(&measures[measures_to_transmit].sensorAddress[2], battery_addr_suffix, 6);				
					measures[measures_to_transmit]._data1.mV = mv;
					++measures_to_transmit;
				}
			}

			LOG_INF("Sending %d measures", measures_to_transmit);
			lora_transmit_measures(measures, measures_to_transmit);
			t_start_transmit = now + 300000; // schedule LoRa transmit every 5 minutes
		}

		uint64_t next = t_start_transmit;
		if (t_stop_hifreq != 0 && t_stop_hifreq < next) next = t_stop_hifreq;
		if (t_start_hifreq != 0 && t_start_hifreq < next) next = t_start_hifreq;

		now = k_uptime_get();
		k_msleep(next - now);
	}
	return 0;
}

void fillMeasureFromSensor(struct Measure* measure, struct Sensor* sensor) {
	measure->sensorAddress[0] = 'B';
	measure->sensorAddress[1] = 'T';
	memcpy(measure->sensorAddress + 2, sensor->address, 6);
	measure->type = BEE_EYE_MEASURE_TYPE_TEMPHUM;
	measure->_data1.tempC = sensor->rawTemperature / 256.0f;
	measure->_data2.hum = sensor->rawHumidity / 256.0f;
}

static void battery_addr_init(void)
{
    /* 64-bit ID from FICR */
    uint64_t dev_id =
        ((uint64_t)NRF_FICR->DEVICEID[1] << 32) |
        ((uint64_t)NRF_FICR->DEVICEID[0]);

    /* Take 6 LSB bytes of the ID */
    for (int i = 0; i < 6; i++) {
        battery_addr_suffix[i] = (uint8_t)(dev_id >> (8 * i));
    }
}
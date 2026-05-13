#include "uart_print.h"
#include "vsense.h"
#include "rtc.h"
#include "onewire.h"
#include "lora.h"
#include "messages.h"
#include "ble_client.h"

#include <stdio.h>
#include <assert.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_gpio.h>
#include <nrfx.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_DBG);

#define LED0_NODE DT_NODELABEL(status_led)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
// static const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

void fillMeasureFromSensor(struct Measure* measure, struct Sensor* sensor);
void fillMeasureFromSensorBat(struct Measure* measure, struct Sensor* sensor);
static uint8_t battery_addr_suffix[6];
static void battery_addr_init(void);
void transmitSensorData(uint64_t cutoff);

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
			ble_client_start(100, 100); // high freq
			t_stop_hifreq = now + 500; 
		}

		if (t_start_transmit != 0 && t_start_transmit <= now) {
			transmitSensorData(now - 300000);
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

#define MAX_MEASURES 8
struct Measure measures[MAX_MEASURES];
static uint8_t measure_count = 0;
static uint8_t total_measure_count = 0;
static void flush_measures(void)
{
	if (measure_count == 0) return;
	__ASSERT(measure_count <= MAX_MEASURES, "measure_count overflow");

	LOG_INF("Sending %d measures", measure_count);
	lora_transmit_measures(measures, measure_count);
	measure_count = 0;
}

static struct Measure *alloc_measure(void)
{
	if (measure_count == MAX_MEASURES) {
		flush_measures();
	}

	++total_measure_count;
	return &measures[measure_count++];
}

void transmitSensorData(uint64_t cutoff)
{
	int ret;

	measure_count = 0;
	total_measure_count = 0;

	// Iterate over BLE sensors
	for (int i = 0; i < ble_get_sensor_count(); ++i) {
		struct Sensor *sensor = ble_get_sensor(i);

		if (sensor->lastReceive < cutoff) continue;

		fillMeasureFromSensor(alloc_measure(), sensor);

		if (!sensor->rawBattery) continue;
		
		fillMeasureFromSensorBat(alloc_measure(), sensor);
	}

	// Iterate over 1Wire sensors
	enum_w1();
	for (int i = 0; i < get_w1_device_count(); ++i) {
		struct Measure *measure = alloc_measure();

		measure->type = BEE_EYE_MEASURE_TYPE_TEMPERATURE;
		measure->data.th.tempC = read_temp(i);
		measure->data.th.hum = 0;

		get_w1_address(measure->sensorAddress, i);
	}

	// Send station's battery voltage
	uint16_t mv = 0;
	ret = vsense_measure_mv(&mv);
	if (ret) {
		LOG_ERR("Couldn't get battery voltage %d", ret);
	} else {
		struct Measure *measure = alloc_measure();

		measure->type = BEE_EYE_MEASURE_TYPE_BATTERY;
		measure->sensorAddress[0] = 'B';
		measure->sensorAddress[1] = 'T';

		memcpy(&measure->sensorAddress[2], battery_addr_suffix, 6);

		measure->data.bat.mV = mv;
	}

	// Flush the reminder
	flush_measures();

	LOG_INF("Sent total of %d measures", total_measure_count);
}

void fillMeasureFromSensor(struct Measure* measure, struct Sensor* sensor) {
	measure->sensorAddress[0] = 'B';
	measure->sensorAddress[1] = 'T';
	memcpy(measure->sensorAddress + 2, sensor->address, 6);
	if(sensor->type == SENSOR_TYPE_TEMPHUM) {
		measure->type = BEE_EYE_MEASURE_TYPE_TEMPHUM;
		measure->data.th.tempC = sensor->data.th.rawTemperature / 256.0f;
		measure->data.th.hum = sensor->data.th.rawHumidity / 256.0f;
	} else if(sensor->type == SENSOR_TYPE_WEIGHT) {
		measure->type = BEE_EYE_MEASURE_TYPE_WEIGHT;
		measure->data.w.weight = sensor->data.w.rawWeight / 256.0f;
	}
}

void fillMeasureFromSensorBat(struct Measure* measure, struct Sensor* sensor) {
	measure->sensorAddress[0] = 'B';
	measure->sensorAddress[1] = 'T';
	memcpy(measure->sensorAddress + 2, sensor->address, 6);
	measure->type = BEE_EYE_MEASURE_TYPE_BATTERY;
	measure->data.bat.mV = sensor->rawBattery / 256.0f;
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
#include "vsense.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <errno.h>

/* Devicetree node for the custom vsense device */
#define VSENSE_NODE DT_NODELABEL(vsense)

/* Enable GPIO from "enable-gpios" */
static const struct gpio_dt_spec vsense_en =
	GPIO_DT_SPEC_GET(VSENSE_NODE, enable_gpios);

/* ADC channel from "io-channels" */
static const struct adc_dt_spec vsense_adc =
	ADC_DT_SPEC_GET_BY_IDX(VSENSE_NODE, 0);

/* Voltage divider: high side 330k, low side 220k
 * Vin = Vmeas * (Rhigh + Rlow) / Rlow = Vmeas * 550 / 220 = Vmeas * 5 / 2
 */
#define VSENSE_DIV_NUM  5
#define VSENSE_DIV_DEN  2

int vsense_measure_mv(uint16_t *out_mv)
{
	int ret;
	int16_t sample;
	struct adc_sequence seq;
	int32_t mv;

	if (out_mv == NULL) {
		return -EINVAL;
	}

	if (!device_is_ready(vsense_en.port)) {
		return -ENODEV;
	}

	if (!device_is_ready(vsense_adc.dev)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&vsense_en, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

    ret = gpio_pin_set_dt(&vsense_en, 1);
    if (ret < 0) {
        return ret;
    }

    k_msleep(5); // RC

	/* Make sure ADC channel is set up */
	ret = adc_channel_setup_dt(&vsense_adc);
	if (ret < 0) {
		return ret;
	}

	memset(&seq, 0, sizeof(seq));
	seq.buffer = &sample;
	seq.buffer_size = sizeof(sample);
	seq.resolution = vsense_adc.resolution;
	seq.channels = BIT(vsense_adc.channel_id);

	ret = adc_read_dt(&vsense_adc, &seq);
	if (ret < 0) {
		return ret;
	}

	mv = sample;

	ret = adc_raw_to_millivolts_dt(&vsense_adc, &mv);
	if (ret < 0) {
		return ret;
	}

	/* Compensate for divider: battery_mv = measured_mv * 5 / 2 */
	int32_t batt_mv = (mv * VSENSE_DIV_NUM) / VSENSE_DIV_DEN;

	if (batt_mv < 0) {
		batt_mv = 0;
	} else if (batt_mv > UINT16_MAX) {
		batt_mv = UINT16_MAX;
	}

	*out_mv = (uint16_t)batt_mv;

    ret = gpio_pin_set_dt(&vsense_en, 0);
    if (ret < 0) {
        return ret;
    }

	return 0;
}
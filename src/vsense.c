#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <stdbool.h>

LOG_MODULE_REGISTER(app_vsense);

#define VSENSE_NODE DT_PATH(vsense)

#if !DT_NODE_HAS_PROP(DT_PATH(vsense), enable_gpios)
#error "enable-gpios missing"
#endif

static const struct gpio_dt_spec vsense_en = GPIO_DT_SPEC_GET(VSENSE_NODE, enable_gpios);
static const struct adc_dt_spec vsense_adc = ADC_DT_SPEC_GET(VSENSE_NODE);

void vsense_enable(bool enable)
{
    if(!device_is_ready(vsense_en.port)) {
        LOG_ERR("Vsense Enable GPIO is not ready");
        return;
    }
    gpio_pin_configure_dt(&vsense_en, GPIO_OUTPUT_INACTIVE);
    gpio_pin_set_dt(&vsense_en, enable);
}

int vsense_read_mv(void)
{
    int16_t buf;
    struct adc_sequence sequence = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    if (!device_is_ready(vsense_adc.dev))
        return -ENODEV;

    if (adc_channel_setup_dt(&vsense_adc) < 0)
        return -EIO;

    int32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        if (adc_read_dt(&vsense_adc, &sequence) < 0)
            return -EIO;
        sum += buf;
    }

    int32_t raw = sum / 8;
    LOG_DBG("Vsense raw reading = %d", raw);
    int32_t mv = raw * vsense_adc.vref_mv / (BIT(vsense_adc.resolution) - 1);
    mv = mv * (330 + 220) / 220; /* divider compensation */
    return mv;
}

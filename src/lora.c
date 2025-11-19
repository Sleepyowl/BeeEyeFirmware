#include "lora.h"
#include "uart_print.h"

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/lora.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_lora, LOG_LEVEL_DBG);

const struct device *lora_dev = DEVICE_DT_GET(DT_NODELABEL(sx1262));

int init_lora(void) {
    struct lora_modem_config config = {
        .frequency = 868100000,  
        .bandwidth = BW_125_KHZ, 
        .datarate = SF_9,       
        .preamble_len = 8,
        .coding_rate = CR_4_7,
        .tx_power = 10,         
        .iq_inverted = false,
        .public_network = false,
        .tx = true
    };

    if(!device_is_ready(lora_dev)) {
        LOG_ERR("SX1262 not ready");
        return -ENODEV;
    }

    int ret = lora_config(lora_dev, &config);
    if (ret < 0) {
        LOG_ERR("LoRa config failed: %d", ret);
        return ret;
    }

    return 0;
}

#define LORA_TXSIZE 255
static uint8_t tx_buf[LORA_TXSIZE];

int lora_transmit_text(const char *text)
{
	if (!text)
    {
		return -EINVAL;
    }

	const size_t text_len = strlen(text);
	const size_t payload  = sizeof(struct MessageHeader) + text_len;

	if (text_len == 0) {
		return -EINVAL;
    }

	if (payload > sizeof(tx_buf)) {
		return -EMSGSIZE;
    }

	fillHeader(tx_buf, BEE_EYE_MESSAGE_TYPE_TEXT);
	memcpy(tx_buf + sizeof(struct MessageHeader), text, text_len);

    LOG_DBG("Sending %d bytes over LoRa (text)", payload);
	return lora_send(lora_dev, tx_buf, payload); /* 0 or -ERRNO */
}

int lora_transmit_measures(const struct Measure *measure, uint8_t count)
{
	if (!measure)
		return -EINVAL;

	size_t payload = sizeof(struct MessageHeader) + sizeof(struct Measure) * count;

    if (payload > sizeof(tx_buf)) {
        // TODO: implement message splitting for large number of sensors
		return -EMSGSIZE;
    }

	fillHeader(tx_buf, BEE_EYE_MESSAGE_TYPE_MEASURES);
	memcpy(tx_buf + sizeof(struct MessageHeader), measure, sizeof(struct Measure) * count);

    LOG_DBG("Sending %d bytes over LoRa (%d measures)", payload, count);
	return lora_send(lora_dev, tx_buf, payload);
}
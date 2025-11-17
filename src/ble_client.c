#include "ble_client.h"
#include "uart_print.h"

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>

#if !defined(CONFIG_BT_EXT_ADV)
#error "enable CONFIG_BT_EXT_ADV"
#endif


#define NAME_LEN 30
static bool scanning_active = false;
static volatile uint16_t ble_measure_count = 0;

#define MAX_SENSORS 8
static struct Sensor sensors[MAX_SENSORS];
static volatile uint8_t num_sensors = 0;


struct __attribute__((packed)) SensorData {
        uint16_t magic;
        int16_t temp;
        int16_t hum;
};

struct AnnounceData {
	char name[NAME_LEN];
	struct SensorData sensorData;
};

static bool data_cb(struct bt_data *data, void *user_data)
{
	if(!user_data) return false;

	struct AnnounceData *announceData = user_data;
	uint8_t len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, NAME_LEN - 1);
		(void)memcpy(announceData->name, data->data, len);
		announceData->name[len] = '\0';
		return true;
	case BT_DATA_MANUFACTURER_DATA:
		if (data->data_len == sizeof(struct SensorData)) {
			(void)memcpy(&announceData->sensorData, data->data, sizeof(struct SensorData));
		} 
		return false;

	default:
		return true;
	}

	// shouldn't actually reach here
	return true;
}

bool addr_is_zero(const uint8_t a[6])
{
    return (a[0] | a[1] | a[2] | a[3] | a[4] | a[5]) == 0;
}

static void scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
	static struct AnnounceData announceData;
	memset(&announceData, 0, sizeof(struct AnnounceData));
	bt_data_parse(buf, data_cb, &announceData);

    if (strncmp(announceData.name, "BeeEye_", 7) != 0) return;
	if (announceData.sensorData.magic != 0xBEEE) return;
	uart_printf("SID=%u adv_type=0x%X props=0x%X addr_type=%d, addr=%02X%02X%02X_%02X%02X%02X\n",
       info->sid, info->adv_type, info->adv_props, info->addr->type
				, info->addr->a.val[5]
				, info->addr->a.val[4]
				, info->addr->a.val[3]
				, info->addr->a.val[2]
				, info->addr->a.val[1]
				, info->addr->a.val[0]);

	++ble_measure_count;

	// // uart_printf("Sensor %s, magic %04x, temp %d, hum %d\n", 
	// // 	announceData.name, 
	// // 	announceData.sensorData.magic, 
	// // 	sys_le16_to_cpu(announceData.sensorData.temp) >> 8, 
	// // 	sys_le16_to_cpu(announceData.sensorData.hum) >> 8);

	for(int i=0;i<num_sensors;++i) {
		if(memcmp(sensors[i].address, info->addr->a.val, 6) == 0) {
			uart_printf("Updated sensor lastReceive\n");
			sensors[i].lastReceive = k_uptime_get();
			sensors[i].rawTemperature = announceData.sensorData.temp;
			sensors[i].rawHumidity = announceData.sensorData.hum;
			return;
		}
	}

	if (num_sensors >= MAX_SENSORS) return;

	uart_printf("Registered sensor\n");
	sensors[num_sensors].lastReceive = k_uptime_get();
	memcpy(sensors[num_sensors].address, info->addr->a.val, 6);
	sensors[num_sensors].rawTemperature = announceData.sensorData.temp;
	sensors[num_sensors].rawHumidity = announceData.sensorData.hum;
	++num_sensors;
}

void bt_le_scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *buf) {
	static struct AnnounceData announceData;
	bt_data_parse(buf, data_cb, &announceData);
	if (strncmp(announceData.name, "BeeEye_", 7) != 0) return;
	if (announceData.sensorData.magic != 0xBEEE) return;
	uart_printf("(DIF) adv_type=0x%X addr_type=%d, addr=%02X%02X%02X_%02X%02X%02X\n",
       adv_type, addr->type
				, addr->a.val[5]
				, addr->a.val[4]
				, addr->a.val[3]
				, addr->a.val[2]
				, addr->a.val[1]
				, addr->a.val[0]);
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv,
};

int ble_client_stop(void)
{
    if (!scanning_active) {
        return -EINVAL;
    }

	bt_le_scan_cb_unregister(&scan_callbacks);

    int ret = bt_le_scan_stop();
	if(ret) {
		uart_printf("Stop scanning failed: %d\n", ret);
		return ret;
	}

    scanning_active = false;
    return 0;
}


int ble_client_start(int interval, int window)
{
	uart_printf("(Re)starting scan with .interval=0x%x, .window=0x%x\n",(interval << 3) / 5, (window << 3) / 5);
    int err;

    if (!bt_is_ready()) {
		memset(sensors, 0, sizeof(sensors));
        err = bt_enable(NULL);
        if (err) {
            uart_printf("BLE init failed: %d\n", err);
            return err;
        }
    }

    if (scanning_active) {
		uart_printf("Scanning already active, stopping\n");
        err = ble_client_stop();
		if(err) {
			return err;
		}
    }

	bt_le_scan_cb_register(&scan_callbacks);

    struct bt_le_scan_param scan_param = {
		.type       = BT_LE_SCAN_TYPE_PASSIVE,
		.options    = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval   = (interval << 3) / 5,  // pause between scans
		.window     = (window << 3) / 5  // actively listening
	};

    err = bt_le_scan_start(&scan_param, NULL);
	if (err) {
		uart_printf("Start scanning failed: %d\n", err);
		return err;
	}

    scanning_active = true;
	uart_printf("Scanning started\n");
    return 0;
}


uint16_t ble_client_get_measure_count() {
	uint16_t measure_count = ble_measure_count;
	ble_measure_count = 0;
	return measure_count;
}

uint64_t ble_client_get_next_sensor_window(void) {
		uint64_t now = k_uptime_get();
		uint64_t result = 0;

        for (int i = 0; i < num_sensors; i++) {
			uart_printf("Record #%d addr=%02X%02X%02X%02X%02X%02X\n", i
				, sensors[i].address[5]
				, sensors[i].address[4]
				, sensors[i].address[3]
				, sensors[i].address[2]
				, sensors[i].address[1]
				, sensors[i].address[0]);
			uint64_t lastReceive = sensors[i].lastReceive;
            if (lastReceive == 0)
                continue;

            uint64_t next = lastReceive + 119500;
			while(next < now) {
				next += 120000;
			}

            if(result == 0 || result > next)
				result = next;
        }
		
		uart_printf("sensors = %d, next sensor window = %llu\n", num_sensors, result);
		return result;
}

uint8_t ble_get_sensor_count(void) {
		return num_sensors;
}

struct Sensor* ble_get_sensor(uint8_t index) {
	if (index >= MAX_SENSORS) return NULL;
	return sensors + index;
}
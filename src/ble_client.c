#include "ble_client.h"
#include "uart_print.h"
#include "protocol.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#if !defined(CONFIG_BT_EXT_ADV)
#error "enable CONFIG_BT_EXT_ADV"
#endif

LOG_MODULE_REGISTER(app_ble, LOG_LEVEL_DBG);

#define NAME_LEN 30
static bool scanning_active = false;
static volatile uint16_t ble_measure_count = 0;

#define MAX_SENSORS 8
static struct Sensor sensors[MAX_SENSORS];
static volatile uint8_t num_sensors = 0;


struct __attribute__((packed)) SensorDataObsolete {
        uint16_t magic;
        int16_t temp;
        int16_t hum;
		uint32_t nextAnnounce;
};

struct AnnounceData {
	char name[NAME_LEN];
	struct ManufacturerData sensorData;
};

static bool data_cb(struct bt_data *data, void *user_data)
{
	if(!user_data) return false;

	struct AnnounceData *announceData = user_data;
	uint8_t len;

	// note: we expect name being parsed before manufacturer data
	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, NAME_LEN - 1);
		(void)memcpy(announceData->name, data->data, len);
		announceData->name[len] = '\0';
		return true;
	case BT_DATA_MANUFACTURER_DATA:
		memset(&announceData->sensorData, 0, sizeof(announceData->sensorData));
		if (data->data_len == sizeof(struct SensorDataObsolete)) {
			const struct SensorDataObsolete *p = (const struct SensorDataObsolete*)data->data;
			announceData->sensorData.nextTransmission = p->nextAnnounce;
			announceData->sensorData.flags = BEE_EYE_BATTERY_3V3 | BEE_EYE_METRIC_TEMPHUM | BEE_EYE_TIMER_1000HZ;
			announceData->sensorData.magic = p->magic == 0xBEEE ? BEE_EYE_MAGIC : 0;
			announceData->sensorData.data.th.temp = p->temp;
			announceData->sensorData.data.th.hum = p->hum;
			announceData->sensorData.crc = crc8(&announceData->sensorData, sizeof(announceData->sensorData) - sizeof(announceData->sensorData.crc), 0x07, 0, false);
		} else if (data->data_len == sizeof(struct ManufacturerData)) {
			(void)memcpy(&announceData->sensorData, data->data, data->data_len);
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

static void update_sensor(struct Sensor *sensor, const struct ManufacturerData *data) {
	sensor->lastReceive = k_uptime_get();
	const uint8_t type = data->flags & BEE_EYE_METRIC_TYPE_MASK;
	const uint8_t freq_unit = data->flags & BEE_EYE_TIMER_RESOLUTION_MASK;
	
	sensor->type = type;
	_Static_assert(sizeof(sensor->data) == sizeof(data->data));
	memcpy(&sensor->data, &data->data, sizeof(sensor->data));

	uint32_t next_trans = data->nextTransmission;

	// convert to ms
	switch (freq_unit) {
	case BEE_EYE_TIMER_64HZ:
		next_trans = ((uint64_t)next_trans) * 15625 / 1000;
		break;
	case BEE_EYE_TIMER_1HZ:
		next_trans = next_trans * 1000;
		break;
	case BEE_EYE_TIMER_100HZ:
		next_trans = next_trans * 10;
		break;
	case BEE_EYE_TIMER_1000HZ:
	default:
		break;
	}

	sensor->nextAnnounce = sensor->lastReceive + next_trans;


	

	LOG_INF("SNS next = %u (%ums)", data->nextTransmission, next_trans);
}

static void scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
	static struct AnnounceData announceData;
	memset(&announceData, 0, sizeof(struct AnnounceData));
	bt_data_parse(buf, data_cb, &announceData);

    if (strncmp(announceData.name, "BeeEye_", 7) != 0) return;
	uint16_t magic = announceData.sensorData.magic;
	if (magic != BEE_EYE_MAGIC) return;
	LOG_DBG("SID=%u adv_type=0x%X props=0x%X addr_type=%d, addr=%02X%02X%02X_%02X%02X%02X",
       info->sid, info->adv_type, info->adv_props, info->addr->type
				, info->addr->a.val[5]
				, info->addr->a.val[4]
				, info->addr->a.val[3]
				, info->addr->a.val[2]
				, info->addr->a.val[1]
				, info->addr->a.val[0]);

	++ble_measure_count;

	LOG_DBG("Sensor %s, flags %02x, next %d", 
		announceData.name, 
		announceData.sensorData.flags, 		
		sys_le32_to_cpu(announceData.sensorData.nextTransmission));

	for(int i=0;i<num_sensors;++i) {
		if(memcmp(sensors[i].address, info->addr->a.val, 6) == 0) {			
			update_sensor(sensors + i, &announceData.sensorData);
			LOG_DBG("Updated sensor %02X%02X", info->addr->a.val[1], info->addr->a.val[0]);
			return;
		}
	}

	if (num_sensors >= MAX_SENSORS) return;

	LOG_DBG("Registered sensor %02X%02X", info->addr->a.val[1], info->addr->a.val[0]);
	memcpy(sensors[num_sensors].address, info->addr->a.val, 6);
	update_sensor(sensors + num_sensors, &announceData.sensorData);
	++num_sensors;
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
		LOG_ERR("Stop scanning failed: %d", ret);
		return ret;
	}

    scanning_active = false;
    return 0;
}


int ble_client_start(int interval, int window)
{
	LOG_INF("(Re)starting scan with .interval=0x%x, .window=0x%x",(interval << 3) / 5, (window << 3) / 5);
    int err;

    if (!bt_is_ready()) {
		memset(sensors, 0, sizeof(sensors));
        err = bt_enable(NULL);
        if (err) {
            LOG_ERR("BLE init failed: %d", err);
            return err;
        }
    }

    if (scanning_active) {
		LOG_DBG("Scanning already active, stopping");
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
		LOG_ERR("Start scanning failed: %d", err);
		return err;
	}

    scanning_active = true;
	LOG_DBG("Scanning started");
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
			LOG_DBG("Record #%d addr=%02X%02X%02X%02X%02X%02X", i
				, sensors[i].address[5]
				, sensors[i].address[4]
				, sensors[i].address[3]
				, sensors[i].address[2]
				, sensors[i].address[1]
				, sensors[i].address[0]);
			uint64_t nextAnnounce = sensors[i].nextAnnounce;
            if (nextAnnounce == 0)
                continue;

            uint64_t next = nextAnnounce;
			while(next < now) {
				next += 120000;
			}

            if(result == 0 || result > next)
				result = next;
        }
		
		LOG_DBG("sensors = %d, next sensor window = %llu (%02d:%02d:%02d.%03d)", 
			num_sensors, 
			result,
			(int)(result / 3600000),
			(int)(result % 3600000) / 60000,
			(int)(result % 60000) / 1000,
			(int)(result % 1000)			
		);
		return result;
}

uint8_t ble_get_sensor_count(void) {
		return num_sensors;
}

struct Sensor* ble_get_sensor(uint8_t index) {
	if (index >= MAX_SENSORS) return NULL;
	return sensors + index;
}
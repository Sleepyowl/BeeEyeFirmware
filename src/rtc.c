#include "uart_print.h"

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <hal/nrf_power.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <hal/nrf_gpio.h>
#include <time.h>
#include <stdint.h>

LOG_MODULE_REGISTER(app_rtc);
static const struct device *rtc = DEVICE_DT_GET(DT_NODELABEL(rv3028));

int intinitialize_rtc(void)
{
    int ret;
    
    // ret = device_init(rtc);
    // if (ret && ret != -EALREADY) {
    //     uart_printf("Couldn't init RTC device: %d\n", ret);
    //     return -ret;
    // }
    
    if (!device_is_ready(rtc)) {
        LOG_ERR("RTC is not ready");
        return -ENODEV;
    }

    struct rtc_time tm;
    ret = rtc_get_time(rtc, &tm);

    if (ret == -ENODATA) {
        LOG_WRN("RTC time is invalid, resetting time");
        struct rtc_time init = {
            .tm_year = 2025 - 1900,  /* years since 1900 */
            .tm_mon  = 0,            /* January = 0 */
            .tm_mday = 1,
            .tm_hour = 0,
            .tm_min  = 0,
            .tm_sec  = 0,
        };

        ret = rtc_set_time(rtc, &init);
        if (ret) {
            LOG_ERR("RTC set time failed: %d", ret);
            return ret;
        }
    } else if (ret) {
        LOG_ERR("RTC get time failed: %d", ret);
        return ret;
    }

    return 0;
}

int get_rtc_ticks(uint64_t* ticks) {
    struct rtc_time tm;
    int ret = rtc_get_time(rtc, &tm);
    if (ret) {
        LOG_ERR("RTC get time failed: %d", ret);
        return ret;
    }

    int64_t seconds = timeutil_timegm64(&tm); // UTC seconds since epoch (1970)

    if (ticks) {
        *ticks = seconds * 1000ULL + ((int64_t)tm.tm_nsec) / 1000000ULL;
    }

    return 0;
}
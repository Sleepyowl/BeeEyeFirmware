#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <stdarg.h>
#include <stdio.h>


// #if !(defined(CONFIG_PRINTK) && defined(CONFIG_UART_CONSOLE))
#define UART_PRINTF_BUF_SIZE 256
static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
static K_MUTEX_DEFINE(uart_lock);
static char buf[UART_PRINTF_BUF_SIZE];
static char CR[1] = {'\r'};
void uart_printf(const char *fmt, ...)
{
    if (!device_is_ready(uart_dev))
        return;

    size_t fmt_len = strlen(fmt);
    if(fmt_len == 0) return;

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0)
        return;

    if (len >= sizeof(buf) -1)
        len = sizeof(buf) - 2;

    bool need_cr = fmt[fmt_len-1] == '\n';
    if(need_cr) {
        buf[len] = '\r';
        len++;
    }
    

    k_mutex_lock(&uart_lock, K_FOREVER);
    uart_tx(uart_dev, buf, len, 20);
    k_mutex_unlock(&uart_lock);
    k_msleep(20);
}
// #else
// void uart_printf(const char *fmt, ...)
// {
//     va_list ap;
//     va_start(ap, fmt);
//     vprintk(fmt, ap);
//     va_end(ap);
// }
// #endif
/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First version
 */

#ifndef __MR_WIN_SIM_H__
#define __MR_WIN_SIM_H__

#include <mrX/mr_config.h>
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_types.h>
#include <mrX/kernel/mr_spinlock.h>
#if defined(MR_USE_DRIVER_UART)
#include <mrX/drivers/uart/mr_uart.h>
#endif /* defined(MR_USE_DRIVER_UART) */
#if defined(MR_USE_DRIVER_FLASH)
#include <mrX/drivers/flash/mr_flash.h>
#include <stdio.h>
#endif /* defined(MR_USE_DRIVER_FLASH) */
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#endif /* defined(MR_USE_DRIVER_GPIO) */
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/adc/mr_adc.h>
#endif /* defined(MR_USE_DRIVER_ADC) */
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#endif /* defined(MR_USE_DRIVER_I2C) */
#if defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#endif /* defined(MR_USE_DRIVER_SPI) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Platform-Win-Sim
 * @{
 */

/* ================================ UART ==================================== */
#if defined(MR_USE_DRIVER_UART)
#ifndef MR_CFG_WIN_UART_RX_BUF_SIZE
#define MR_CFG_WIN_UART_RX_BUF_SIZE     (4096U)
#endif /* MR_CFG_WIN_UART_RX_BUF_SIZE */

/* Windows UART simulation backend mode */
typedef enum mr_sim_uart_mode {
    MR_SIM_UART_MODE_STDIO = 0,
    MR_SIM_UART_MODE_TCP = 1,
    MR_SIM_UART_MODE_MEM = 2,
} mr_sim_uart_mode_t;

/* Windows UART simulation configuration */
typedef struct mr_sim_uart_config {
    mr_sim_uart_mode_t mode;
    const char *host;
    mr_uint16_t port;
} mr_sim_uart_config_t;

/* Windows UART simulation instance */
typedef struct mr_sim_uart {
    mr_uart_data_t data;
    const mr_driver_t *drv;
    mr_uart_config_t uart_cfg;
    mr_sim_uart_config_t sim_cfg;
    mr_uint32_t event_mask;
    mr_spinlock_t lock;
    mr_uint8_t rx_buf[MR_CFG_WIN_UART_RX_BUF_SIZE];
    mr_size_t rx_head;
    mr_size_t rx_tail;
    mr_size_t rx_count;
    mr_uint8_t tx_buf[MR_CFG_WIN_UART_RX_BUF_SIZE];
    mr_size_t tx_head;
    mr_size_t tx_tail;
    mr_size_t tx_count;
    mr_bool_t started;
    volatile LONG running;
    HANDLE thread;
#if defined(MR_USE_WIN_UART_TCP)
    SOCKET listen_sock;
    SOCKET client_sock;
    mr_uint16_t bound_port;
#endif /* defined(MR_USE_WIN_UART_TCP) */
#if defined(MR_USE_DRIVER_UART_STREAM)
    mr_uint8_t *rx_stream_buf;
    mr_size_t rx_stream_size;
    mr_size_t rx_stream_pos;
    mr_bool_t rx_stream_circular;
    mr_bool_t rx_stream_busy;
    const mr_uint8_t *tx_stream_buf;
    mr_size_t tx_stream_size;
    mr_size_t tx_stream_pos;
    mr_bool_t tx_stream_circular;
    mr_bool_t tx_stream_busy;
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
} mr_sim_uart_t;

extern mr_err_t mr_sim_uart_init(mr_driver_t *drv, mr_sim_uart_t *sim,
                                 const mr_sim_uart_config_t *cfg);
extern mr_err_t mr_sim_uart_deinit(mr_driver_t *drv);
extern mr_ssize_t mr_sim_uart_rx_push(mr_driver_t *drv, const void *buf,
                                      mr_size_t size);
extern mr_ssize_t mr_sim_uart_tx_pop(mr_driver_t *drv, void *buf,
                                     mr_size_t size);
#if defined(MR_USE_WIN_UART_TCP)
extern mr_uint16_t mr_sim_uart_tcp_port(mr_driver_t *drv);
extern mr_bool_t mr_sim_uart_tcp_connected(mr_driver_t *drv);
#endif /* defined(MR_USE_WIN_UART_TCP) */
#endif /* defined(MR_USE_DRIVER_UART) */

/* ================================ FLASH =================================== */
#if defined(MR_USE_DRIVER_FLASH)
/* Windows FLASH power-loss callback */
typedef void (*mr_sim_flash_power_loss_cb_t)(mr_driver_t *drv, void *user_data);

/* Windows FLASH simulation configuration */
typedef struct mr_sim_flash_config {
    const char *path;
    mr_uint64_t size;
    mr_uint32_t erase_size;
    mr_uint32_t program_size;
    mr_uint32_t page_size;
    mr_uint8_t erased_byte;
    mr_bool_t strict;
} mr_sim_flash_config_t;

/* Windows FLASH simulation instance */
typedef struct mr_sim_flash {
    mr_flash_data_t data;
    mr_sim_flash_config_t cfg;
    FILE *fp;
    mr_spinlock_t lock;
    mr_bool_t power_loss_armed;
    mr_bool_t power_loss_triggered;
    mr_uint32_t power_loss_countdown;
    mr_size_t power_loss_partial;
    mr_sim_flash_power_loss_cb_t power_loss_cb;
    void *power_loss_user_data;
} mr_sim_flash_t;

extern mr_err_t mr_sim_flash_init(mr_driver_t *drv, mr_sim_flash_t *sim,
                                  const mr_sim_flash_config_t *cfg);
extern mr_err_t mr_sim_flash_deinit(mr_driver_t *drv);
extern mr_err_t mr_sim_flash_reset(mr_driver_t *drv);
extern mr_err_t mr_sim_flash_power_loss_arm(mr_driver_t *drv,
                                            mr_uint32_t op_countdown,
                                            mr_size_t partial_bytes,
                                            mr_sim_flash_power_loss_cb_t cb,
                                            void *user_data);
extern void mr_sim_flash_power_loss_disarm(mr_driver_t *drv);
extern mr_bool_t mr_sim_flash_power_loss_triggered(mr_driver_t *drv);
#endif /* defined(MR_USE_DRIVER_FLASH) */

/* ================================ GPIO ==================================== */
#if defined(MR_USE_DRIVER_GPIO)
/* Windows GPIO simulation instance */
typedef struct mr_sim_gpio {
    mr_gpio_data_t data;
    mr_spinlock_t lock;
    mr_gpio_config_t configs[32];
    mr_uint32_t out_mask;
    mr_uint32_t in_mask;
    mr_uint32_t event_enabled_mask;
    mr_waitable_t events[32];
    mr_bool_t events_inited;
} mr_sim_gpio_t;

extern mr_err_t mr_sim_gpio_init(mr_driver_t *drv, mr_sim_gpio_t *sim,
                                 mr_uint32_t exist_mask,
                                 mr_uint32_t invert_mask);
extern mr_err_t mr_sim_gpio_set(mr_driver_t *drv, mr_uint32_t pin,
                                mr_uint8_t value);
extern mr_err_t mr_sim_gpio_get(mr_driver_t *drv, mr_uint32_t pin,
                                mr_uint8_t *value);
extern mr_err_t mr_sim_gpio_port_set(mr_driver_t *drv, mr_uint32_t mask);
extern mr_err_t mr_sim_gpio_port_get(mr_driver_t *drv, mr_uint32_t *mask);
#endif /* defined(MR_USE_DRIVER_GPIO) */

/* ================================ ADC ===================================== */
#if defined(MR_USE_DRIVER_ADC)
/* Windows ADC simulation instance */
typedef struct mr_sim_adc {
    mr_adc_data_t data;
    mr_spinlock_t lock;
    mr_adc_config_t configs[32];
    mr_uint32_t values[32];
    mr_uint32_t event_mask;
#if defined(MR_USE_DRIVER_ADC_STREAM)
    mr_adc_seq_t stream_seq;
    mr_adc_stream_config_t stream_cfg;
    mr_size_t stream_offset;
    mr_bool_t stream_busy;
    mr_bool_t stream_half_sent;
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
} mr_sim_adc_t;

extern mr_err_t mr_sim_adc_init(mr_driver_t *drv, mr_sim_adc_t *sim,
                                mr_uint16_t ref_internal,
                                mr_uint32_t exist_mask);
extern mr_err_t mr_sim_adc_set(mr_driver_t *drv, mr_uint32_t channel,
                               mr_uint32_t raw);
extern mr_err_t mr_sim_adc_get(mr_driver_t *drv, mr_uint32_t channel,
                               mr_uint32_t *raw);
extern mr_err_t mr_sim_adc_set_millivolt(mr_driver_t *drv, mr_uint32_t channel,
                                         mr_int32_t mv, mr_uint8_t resolution);
#if defined(MR_USE_DRIVER_ADC_STREAM)
extern mr_err_t mr_sim_adc_stream_step(mr_driver_t *drv);
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
#endif /* defined(MR_USE_DRIVER_ADC) */

/* ================================ I2C ===================================== */
#if defined(MR_USE_DRIVER_I2C)
#define MR_SIM_I2C_DEV_MAX              (16U)

/* Virtual I2C fake device transfer callback */
typedef mr_err_t (*mr_sim_i2c_dev_xfer_t)(void *priv, const mr_i2c_msg_t *msgs,
                                          mr_size_t num);

/* Virtual I2C fake device descriptor */
typedef struct mr_sim_i2c_dev {
    mr_uint16_t addr;
    mr_bool_t addr10;
    mr_sim_i2c_dev_xfer_t transfer;
    void *priv;
} mr_sim_i2c_dev_t;

/* Windows virtual I2C bus instance */
typedef struct mr_sim_i2c {
    mr_i2c_data_t data;
    mr_spinlock_t lock;
    mr_i2c_config_t cfg;
    mr_sim_i2c_dev_t devs[MR_SIM_I2C_DEV_MAX];
    mr_bool_t dev_used[MR_SIM_I2C_DEV_MAX];
} mr_sim_i2c_t;

/* Ready-to-use fake I2C EEPROM device */
typedef struct mr_sim_i2c_eeprom {
    mr_uint8_t *mem;
    mr_size_t size;
    mr_uint8_t addr_bytes;
    mr_size_t cursor;
} mr_sim_i2c_eeprom_t;

extern mr_err_t mr_sim_i2c_init(mr_driver_t *drv, mr_sim_i2c_t *sim);
extern mr_err_t mr_sim_i2c_attach(mr_driver_t *drv,
                                  const mr_sim_i2c_dev_t *dev);
extern mr_err_t mr_sim_i2c_detach(mr_driver_t *drv, mr_uint16_t addr,
                                  mr_bool_t addr10);
extern mr_err_t mr_sim_i2c_eeprom_attach(mr_driver_t *drv,
                                         mr_sim_i2c_eeprom_t *eeprom,
                                         mr_uint16_t addr, mr_uint8_t *mem,
                                         mr_size_t size, mr_uint8_t addr_bytes);
#endif /* defined(MR_USE_DRIVER_I2C) */

/* ================================ SPI ===================================== */
#if defined(MR_USE_DRIVER_SPI)
#define MR_SIM_SPI_CS_MAX               (16U)

/* Virtual SPI fake device transfer callback */
typedef mr_err_t (*mr_sim_spi_dev_xfer_t)(void *priv, const mr_spi_msg_t *msgs,
                                          mr_size_t num);

/* Virtual SPI fake device descriptor */
typedef struct mr_sim_spi_dev {
    mr_uint32_t cs;
    mr_sim_spi_dev_xfer_t transfer;
    void *priv;
} mr_sim_spi_dev_t;

/* Windows virtual SPI bus instance */
typedef struct mr_sim_spi {
    mr_spi_data_t data;
    mr_spinlock_t lock;
    mr_uint32_t cs_num;
    mr_spi_config_t configs[MR_SIM_SPI_CS_MAX];
    mr_sim_spi_dev_t devs[MR_SIM_SPI_CS_MAX];
    mr_bool_t dev_used[MR_SIM_SPI_CS_MAX];
} mr_sim_spi_t;

/* Ready-to-use fake SPI EEPROM device (25LCxx / AT25 style) */
typedef struct mr_sim_spi_eeprom {
    mr_uint8_t *mem;
    mr_size_t size;
    mr_uint8_t addr_bytes;
    mr_bool_t write_enable;
} mr_sim_spi_eeprom_t;

extern mr_err_t mr_sim_spi_init(mr_driver_t *drv, mr_sim_spi_t *sim,
                                mr_uint32_t cs_num);
extern mr_err_t mr_sim_spi_attach(mr_driver_t *drv,
                                  const mr_sim_spi_dev_t *dev);
extern mr_err_t mr_sim_spi_detach(mr_driver_t *drv, mr_uint32_t cs);
extern mr_err_t mr_sim_spi_eeprom_attach(mr_driver_t *drv,
                                         mr_sim_spi_eeprom_t *eeprom,
                                         mr_uint32_t cs, mr_uint8_t *mem,
                                         mr_size_t size, mr_uint8_t addr_bytes);
#endif /* defined(MR_USE_DRIVER_SPI) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_WIN_SIM_H__ */

/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_WIN)
#include <mrX/platform/mr_platform.h>
#include <mrX/libc/mr_string.h>
#include <setjmp.h>
#include <stdio.h>

/* ================================ GPIO Tests ============================== */
#if defined(MR_USE_DRIVER_GPIO)
static volatile int __win_gpio_irq_count = 0;
static volatile mr_uint32_t __win_gpio_irq_pin = 0xFFFFFFFFU;

static void win_test_gpio_irq_cb(mr_waiter_t *waiter, void *param,
                                 mr_wait_event_t events, const void *data) {
    const mr_gpio_event_data_t *ev = (const mr_gpio_event_data_t *)data;

    MR_IGNORE(waiter);
    MR_IGNORE(param);
    if ((events & MR_GPIO_WAIT_EXTI) && ev) {
        __win_gpio_irq_count++;
        __win_gpio_irq_pin = ev->pin;
    }
}

MR_TEST(win_sim, gpio_io_and_exti) {
    mr_driver_t drv;
    mr_sim_gpio_t sim;
    mr_gpio_config_t cfg = {0};
    mr_waiter_t waiter;
    mr_uint8_t val = 0;
    mr_uint32_t port_val = 0;
    mr_err_t ret;

    ret = mr_sim_gpio_init(&drv, &sim, 0x0000FFFFU, (1U << 1));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_gpio_output(&drv, 0, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_sim_gpio_get(&drv, 0, &val), 0);
    MR_TEST_EXPECT_EQ_UINT(val, 1U);

    /* Inverted pin 1: logical HIGH -> physical LOW */
    ret = mr_gpio_output(&drv, 1, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_sim_gpio_get(&drv, 1, &val), 0);
    MR_TEST_EXPECT_EQ_UINT(val, 0U);
    MR_TEST_EXPECT_EQ_INT((int)mr_gpio_read(&drv, 1, &val), 1);
    MR_TEST_EXPECT_EQ_UINT(val, 1U);

    /* Input pin 2 via mr_sim_gpio_set */
    ret = mr_gpio_input(&drv, 2, MR_GPIO_PULL_NONE);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_sim_gpio_set(&drv, 2, 1U), 0);
    MR_TEST_EXPECT_EQ_INT((int)mr_gpio_read(&drv, 2, &val), 1);
    MR_TEST_EXPECT_EQ_UINT(val, 1U);

    MR_TEST_EXPECT_EQ_INT((int)mr_gpio_port_read(&drv, &port_val), 4);
    MR_TEST_EXPECT_EQ_UINT(port_val & 0x7U, 0x7U);

    /* EXTI rising edge on pin 3 */
    __win_gpio_irq_count = 0;
    __win_gpio_irq_pin = 0xFFFFFFFFU;
    cfg.mode = MR_GPIO_MODE_IRQ_RISING;
    cfg.pull = MR_GPIO_PULL_NONE;
    ret = mr_gpio_configure(&drv, 3, &cfg);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_waiter_init(&waiter, win_test_gpio_irq_cb, MR_NULL,
                         MR_GPIO_WAIT_EXTI);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_event_attach(&drv, 3, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_event_enable(&drv, 3, MR_GPIO_WAIT_EXTI), 0);

    MR_TEST_EXPECT_EQ_INT(mr_sim_gpio_set(&drv, 3, 0U), 0);
    MR_TEST_EXPECT_EQ_INT(__win_gpio_irq_count, 0);
    MR_TEST_EXPECT_EQ_INT(mr_sim_gpio_set(&drv, 3, 1U), 0);
    MR_TEST_EXPECT_EQ_INT(__win_gpio_irq_count, 1);
    MR_TEST_EXPECT_EQ_UINT(__win_gpio_irq_pin, 3U);

    MR_TEST_EXPECT_EQ_INT(mr_sim_gpio_set(&drv, 16, 1U), -MR_EINVAL);

    mr_waiter_del(&waiter);
    mr_driver_shutdown(&drv);
    mr_gpio_del(&drv);
}
#endif /* defined(MR_USE_DRIVER_GPIO) */

/* ================================ ADC Tests =============================== */
#if defined(MR_USE_DRIVER_ADC)
static volatile mr_uint32_t __win_adc_events = 0U;

static void win_test_adc_ev_cb(mr_waiter_t *waiter, void *param,
                               mr_wait_event_t events, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(param);
    MR_IGNORE(data);
    __win_adc_events |= (mr_uint32_t)events;
}

MR_TEST(win_sim, adc_sample_and_stream) {
    mr_driver_t drv;
    mr_sim_adc_t sim;
    mr_adc_config_t cfg = {
        .diff = MR_ADC_DIFF_SINGLE,
        .ref = MR_ADC_REF_INTERNAL,
        .gain = MR_ADC_GAIN_1,
    };
    mr_uint16_t samples[4] = {0};
    mr_adc_seq_t seq = {
        .channels = (1U << 0) | (1U << 2),
        .resolution = 12U,
        .oversample = 0U,
        .size = sizeof(samples),
        .buf = samples,
    };
    mr_uint32_t raw = 0U;
    mr_err_t ret;

    ret = mr_sim_adc_init(&drv, &sim, 3300U, 0x000000FFU);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_adc_configure(&drv, 0, &cfg), 0);
    MR_TEST_ASSERT_EQ_INT(mr_adc_configure(&drv, 2, &cfg), 0);

    MR_TEST_ASSERT_EQ_INT(mr_sim_adc_set(&drv, 0, 1234U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_sim_adc_set_millivolt(&drv, 2, 1650, 12U), 0);
    MR_TEST_EXPECT_EQ_INT(mr_sim_adc_get(&drv, 2, &raw), 0);
    MR_TEST_EXPECT_EQ_UINT(raw, 2048U);

    MR_TEST_EXPECT_EQ_INT((int)mr_adc_read(&drv, &seq), (int)sizeof(samples));
    MR_TEST_EXPECT_EQ_UINT(samples[0], 1234U);
    MR_TEST_EXPECT_EQ_UINT(samples[1], 2048U);
    MR_TEST_EXPECT_EQ_UINT(samples[2], 1234U);
    MR_TEST_EXPECT_EQ_UINT(samples[3], 2048U);

#if defined(MR_USE_DRIVER_ADC_STREAM)
    {
        mr_adc_stream_config_t scfg = {
            .source = MR_ADC_STREAM_SOURCE_SOFTWARE,
            .circular = MR_FALSE,
        };
        mr_waiter_t waiter;
        mr_bool_t busy = MR_FALSE;

        __win_adc_events = 0U;
        MR_TEST_ASSERT_EQ_INT(
            mr_waiter_init(&waiter, win_test_adc_ev_cb, MR_NULL,
                           MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL),
            0);
        MR_TEST_ASSERT_EQ_INT(mr_adc_event_attach(&drv, &waiter), 0);
        MR_TEST_ASSERT_EQ_INT(
            mr_adc_event_enable(&drv, MR_ADC_WAIT_STREAM_HALF
                                          | MR_ADC_WAIT_STREAM_FULL),
            0);
        MR_TEST_ASSERT_EQ_INT(mr_adc_stream_start(&drv, &seq, &scfg), 0);
        MR_TEST_ASSERT_EQ_INT(mr_sim_adc_stream_step(&drv), 0);
        MR_TEST_EXPECT_EQ_UINT(__win_adc_events, MR_ADC_WAIT_STREAM_HALF);
        MR_TEST_ASSERT_EQ_INT(mr_sim_adc_stream_step(&drv), 0);
        MR_TEST_EXPECT_EQ_UINT(
            __win_adc_events,
            MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL);
        MR_TEST_ASSERT_EQ_INT(mr_adc_stream_is_busy(&drv, &busy), 0);
        MR_TEST_EXPECT_EQ_INT(busy, MR_FALSE);
        mr_waiter_del(&waiter);
    }
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

    mr_driver_shutdown(&drv);
    mr_adc_del(&drv);
}
#endif /* defined(MR_USE_DRIVER_ADC) */

/* ================================ FLASH Tests ============================= */
#if defined(MR_USE_DRIVER_FLASH)
static jmp_buf __win_flash_pl_jmp;

static void win_test_flash_pl_cb(mr_driver_t *drv, void *user_data) {
    MR_IGNORE(drv);
    MR_IGNORE(user_data);
    longjmp(__win_flash_pl_jmp, 1);
}

MR_TEST(win_sim, flash_nor_and_power_loss) {
    const char *path = "ci_test_win_flash.bin";
    mr_driver_t drv;
    mr_sim_flash_t sim;
    mr_sim_flash_config_t cfg = {
        .path = path,
        .size = 16384U,
        .erase_size = 4096U,
        .program_size = 4U,
        .page_size = 256U,
        .erased_byte = 0xFFU,
        .strict = MR_TRUE,
    };
    mr_uint8_t wbuf[8] = {0xAA, 0x55, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    mr_uint8_t rbuf[8] = {0};
    mr_uint8_t illegal[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    mr_err_t ret;

    remove(path);
    ret = mr_sim_flash_init(&drv, &sim, &cfg);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT((int)mr_flash_read(&drv, 0U, rbuf, sizeof(rbuf)), 8);
    MR_TEST_EXPECT_EQ_UINT(rbuf[0], 0xFFU);
    MR_TEST_EXPECT_EQ_UINT(rbuf[7], 0xFFU);

    MR_TEST_EXPECT_EQ_INT((int)mr_flash_program(&drv, 1U, wbuf, 4U), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_flash_erase(&drv, 1024U, 4096U), -MR_EINVAL);

    MR_TEST_ASSERT_EQ_INT((int)mr_flash_program(&drv, 0U, wbuf, sizeof(wbuf)), 8);
    MR_TEST_ASSERT_EQ_INT((int)mr_flash_read(&drv, 0U, rbuf, sizeof(rbuf)), 8);
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(wbuf, rbuf, sizeof(wbuf)), 0);

    MR_TEST_EXPECT_EQ_INT((int)mr_flash_program(&drv, 0U, illegal, 4U), -MR_EIO);

    MR_TEST_ASSERT_EQ_INT(mr_flash_erase(&drv, 0U, 4096U), 0);
    MR_TEST_ASSERT_EQ_INT((int)mr_flash_read(&drv, 0U, rbuf, sizeof(rbuf)), 8);
    MR_TEST_EXPECT_EQ_UINT(rbuf[0], 0xFFU);

    /* Power-loss injection with partial write (4 of 8 bytes) + longjmp reset */
    MR_TEST_ASSERT_EQ_INT(
        mr_sim_flash_power_loss_arm(&drv, 0U, 4U, win_test_flash_pl_cb, MR_NULL),
        0);
    if (setjmp(__win_flash_pl_jmp) == 0) {
        (void)mr_flash_program(&drv, 0U, wbuf, sizeof(wbuf));
        MR_TEST_EXPECT_EQ_INT(1, 0);
    }
    MR_TEST_EXPECT_EQ_INT(mr_sim_flash_power_loss_triggered(&drv), MR_TRUE);

    /* Re-open file to verify persistence & torn write */
    MR_TEST_ASSERT_EQ_INT(mr_sim_flash_deinit(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_sim_flash_init(&drv, &sim, &cfg), 0);
    MR_TEST_ASSERT_EQ_INT((int)mr_flash_read(&drv, 0U, rbuf, sizeof(rbuf)), 8);
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(rbuf, wbuf, 4U), 0);
    MR_TEST_EXPECT_EQ_UINT(rbuf[4], 0xFFU);
    MR_TEST_EXPECT_EQ_UINT(rbuf[7], 0xFFU);

    MR_TEST_ASSERT_EQ_INT(mr_sim_flash_deinit(&drv), 0);
    remove(path);
}
#endif /* defined(MR_USE_DRIVER_FLASH) */

/* ================================ UART Tests ============================== */
#if defined(MR_USE_DRIVER_UART)
MR_TEST(win_sim, uart_mem_and_tcp) {
    mr_driver_t drv;
    mr_sim_uart_t sim;
    mr_sim_uart_config_t mem_cfg = {
        .mode = MR_SIM_UART_MODE_MEM,
    };
    char buf[32] = {0};

    MR_TEST_ASSERT_EQ_INT(mr_sim_uart_init(&drv, &sim, &mem_cfg), 0);
    MR_TEST_ASSERT_EQ_INT((int)mr_uart_write(&drv, "hello", 5U), 5);
    MR_TEST_EXPECT_EQ_INT((int)mr_sim_uart_tx_pop(&drv, buf, sizeof(buf)), 5);
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(buf, "hello", 5U), 0);
    MR_TEST_ASSERT_EQ_INT((int)mr_sim_uart_rx_push(&drv, "world", 5U), 5);
    MR_TEST_EXPECT_EQ_INT((int)mr_uart_read(&drv, buf, sizeof(buf)), 5);
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(buf, "world", 5U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_sim_uart_deinit(&drv), 0);

#if defined(MR_USE_WIN_UART_TCP)
    {
        mr_sim_uart_config_t tcp_cfg = {
            .mode = MR_SIM_UART_MODE_TCP,
            .host = "127.0.0.1",
            .port = 0U,
        };
        mr_uint16_t port;
        SOCKET cli;
        struct sockaddr_in sa;
        int tries;

        MR_TEST_ASSERT_EQ_INT(mr_sim_uart_init(&drv, &sim, &tcp_cfg), 0);
        port = mr_sim_uart_tcp_port(&drv);
        MR_TEST_ASSERT_NE_UINT(port, 0U);

        cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        MR_TEST_ASSERT_NE_INT((int)cli, (int)INVALID_SOCKET);
        mr_memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
        MR_TEST_ASSERT_EQ_INT(connect(cli, (struct sockaddr *)&sa, sizeof(sa)),
                              0);

        for (tries = 0; tries < 50 && !mr_sim_uart_tcp_connected(&drv);
             tries++) {
            Sleep(10);
        }
        MR_TEST_ASSERT_EQ_INT(mr_sim_uart_tcp_connected(&drv), MR_TRUE);

        MR_TEST_ASSERT_EQ_INT(send(cli, "ping", 4, 0), 4);
        mr_memset(buf, 0, sizeof(buf));
        for (tries = 0; tries < 50; tries++) {
            mr_ssize_t n = mr_uart_read(&drv, buf, 4U);
            if (n == 4) {
                break;
            }
            Sleep(10);
        }
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buf, "ping", 4U), 0);

        MR_TEST_ASSERT_EQ_INT((int)mr_uart_write(&drv, "pong", 4U), 4);
        mr_memset(buf, 0, sizeof(buf));
        MR_TEST_EXPECT_EQ_INT(recv(cli, buf, 4, 0), 4);
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buf, "pong", 4U), 0);

        closesocket(cli);
        MR_TEST_ASSERT_EQ_INT(mr_sim_uart_deinit(&drv), 0);
    }
#endif /* defined(MR_USE_WIN_UART_TCP) */
}
#endif /* defined(MR_USE_DRIVER_UART) */

/* ============================ I2C & SPI Tests ============================= */
#if defined(MR_USE_DRIVER_I2C) && defined(MR_USE_DRIVER_SPI)
MR_TEST(win_sim, i2c_and_spi_eeprom) {
    mr_driver_t i2c_drv, spi_drv;
    mr_sim_i2c_t i2c_sim;
    mr_sim_spi_t spi_sim;
    mr_sim_i2c_eeprom_t i2c_ee;
    mr_sim_spi_eeprom_t spi_ee;
    mr_uint8_t ee_mem[256];
    mr_uint8_t wcmd[5] = {0x10, 0xDE, 0xAD, 0xBE, 0xEF};
    mr_uint8_t raddr = 0x10;
    mr_uint8_t rdata[4] = {0};
    mr_i2c_msg_t i2c_wr
        = {.addr = 0x50, .flags = MR_I2C_MSG_WRITE, .buf = wcmd, .len = 5};
    mr_i2c_msg_t i2c_rd[2] = {
        {.addr = 0x50, .flags = MR_I2C_MSG_WRITE, .buf = &raddr, .len = 1},
        {.addr = 0x50, .flags = MR_I2C_MSG_READ, .buf = rdata, .len = 4},
    };
    mr_i2c_msg_t i2c_nack
        = {.addr = 0x51, .flags = MR_I2C_MSG_READ, .buf = rdata, .len = 1};
    mr_spi_config_t scfg = {
        .clock_rate = 1000000U,
        .data_bits = 8U,
        .mode = MR_SPI_MODE_0,
        .bit_order = MR_SPI_BIT_ORDER_MSB,
        .role = MR_SPI_ROLE_MASTER,
    };
    mr_uint8_t spi_wren = 0x06U;
    mr_uint8_t spi_wr_hdr[2] = {0x02U, 0x20U};
    mr_uint8_t spi_wr_data[4] = {0x11, 0x22, 0x33, 0x44};
    mr_uint8_t spi_rd_hdr[2] = {0x03U, 0x20U};
    mr_spi_msg_t spi_m_wren = {.tx_buf = &spi_wren, .tx_len = 1};
    mr_spi_msg_t spi_m_wr[2] = {
        {.tx_buf = spi_wr_hdr, .tx_len = 2},
        {.tx_buf = spi_wr_data, .tx_len = 4},
    };
    mr_spi_msg_t spi_m_rd[2] = {
        {.tx_buf = spi_rd_hdr, .tx_len = 2},
        {.rx_buf = rdata, .rx_len = 4},
    };

    mr_memset(ee_mem, 0xFF, sizeof(ee_mem));
    MR_TEST_ASSERT_EQ_INT(mr_sim_i2c_init(&i2c_drv, &i2c_sim), 0);
    MR_TEST_ASSERT_EQ_INT(mr_sim_i2c_eeprom_attach(&i2c_drv, &i2c_ee, 0x50,
                                                   ee_mem, sizeof(ee_mem), 1U),
                          0);
    MR_TEST_ASSERT_EQ_INT((int)mr_i2c_transfer(&i2c_drv, &i2c_wr, 1U), 1);
    MR_TEST_ASSERT_EQ_INT((int)mr_i2c_transfer(&i2c_drv, i2c_rd, 2U), 2);
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(rdata, &wcmd[1], 4U), 0);
    MR_TEST_EXPECT_EQ_INT((int)mr_i2c_transfer(&i2c_drv, &i2c_nack, 1U),
                          -MR_EIO);
    mr_driver_shutdown(&i2c_drv);
    mr_i2c_del(&i2c_drv);

    mr_memset(ee_mem, 0xFF, sizeof(ee_mem));
    mr_memset(rdata, 0, sizeof(rdata));
    MR_TEST_ASSERT_EQ_INT(mr_sim_spi_init(&spi_drv, &spi_sim, 4U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_spi_configure(&spi_drv, 0U, &scfg), 0);
    MR_TEST_ASSERT_EQ_INT(mr_sim_spi_eeprom_attach(&spi_drv, &spi_ee, 0U,
                                                   ee_mem, sizeof(ee_mem), 1U),
                          0);
    MR_TEST_ASSERT_EQ_INT((int)mr_spi_transfer(&spi_drv, 0U, &spi_m_wren, 1U),
                          1);
    MR_TEST_ASSERT_EQ_INT((int)mr_spi_transfer(&spi_drv, 0U, spi_m_wr, 2U), 2);
    MR_TEST_ASSERT_EQ_INT((int)mr_spi_transfer(&spi_drv, 0U, spi_m_rd, 2U), 2);
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(rdata, spi_wr_data, 4U), 0);
    mr_driver_shutdown(&spi_drv);
    mr_spi_del(&spi_drv);
}
#endif /* defined(MR_USE_DRIVER_I2C) && defined(MR_USE_DRIVER_SPI) */
#endif /* defined(MR_USE_TEST) && defined(MR_USE_WIN) */

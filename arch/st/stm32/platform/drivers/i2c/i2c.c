/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First generic STM32 I2C version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/port/mr_port_irq.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_i2c
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "STM32 I2C requires the GPIO driver"
#endif /* !defined(MR_USE_DRIVER_GPIO) */

/* Driver I2C operations */
static mr_err_t i2c_configure(const mr_driver_t *drv,
                              const mr_i2c_config_t *config);
static mr_err_t i2c_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config);
static mr_ssize_t i2c_transfer(const mr_driver_t *drv,
                               const mr_i2c_msg_t *msgs, mr_size_t num);
static mr_ssize_t i2c_slave_read(const mr_driver_t *drv, void *buf,
                                 mr_size_t size);
static mr_ssize_t i2c_slave_write(const mr_driver_t *drv, const void *buf,
                                  mr_size_t size);
static mr_err_t i2c_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
MR_UNUSED static const mr_i2c_ops_t __i2c_ops = {
    .configure = i2c_configure,
    .configuration = i2c_configuration,
    .transfer = i2c_transfer,
    .slave_read = i2c_slave_read,
    .slave_write = i2c_slave_write,
    .event_control = i2c_event_control,
};

/* I2C IP capability definition */
#if defined(I2C_TIMINGR_PRESC) || defined(I2C_TIMINGR_PRESC_Msk)
#define STM32_I2C_V2
#endif /* defined(I2C_TIMINGR_PRESC) || defined(I2C_TIMINGR_PRESC_Msk) */

/* I2C definition */
#define I2C_DEFAULT_TIMEOUT_MS          (25U)
#define I2C_STANDARD_MAX_HZ             (100000U)
#define I2C_FAST_MAX_HZ                 (400000U)
#define I2C_FAST_PLUS_MAX_HZ            (1000000U)
#define I2C_DEFAULT_SLAVE_FIFO_SIZE     (32U)

#define I2C_SLAVE_EVENT_MASK                                                   \
    (MR_I2C_WAIT_RX_START | MR_I2C_WAIT_RX_READY | MR_I2C_WAIT_TX_START        \
     | MR_I2C_WAIT_TX_READY | MR_I2C_WAIT_STOP | MR_I2C_WAIT_ERROR)

/* I2C pin type */
typedef struct i2c_pin {
    mr_ht_gpio_spec_t spec;
    uint32_t alternate;
} i2c_pin_t;

/* I2C slave FIFO type */
typedef struct i2c_slave_fifo {
    mr_uint8_t *data;
    mr_size_t cap;
    mr_size_t head;
    mr_size_t tail;
    mr_size_t count;
} i2c_slave_fifo_t;

/* I2C slave state type */
typedef struct i2c_slave {
    i2c_slave_fifo_t rx_fifo;
    i2c_slave_fifo_t tx_fifo;
    mr_uint32_t event_mask;
    mr_uint8_t active: 1;
    mr_uint8_t sending: 1;
} i2c_slave_t;

/* I2C private data type */
typedef struct i2c_priv {
    I2C_HandleTypeDef handle;
    void (*clk_enable)(void);
    i2c_pin_t scl;
    i2c_pin_t sda;
    IRQn_Type ev_irqn;
    IRQn_Type er_irqn;
    mr_uint32_t irq_priority;
    mr_uint32_t timeout_ms;
    mr_driver_t *driver;
#if defined(STM32_I2C_V2)
    uint32_t timing_override;
    mr_uint8_t has_timing_override: 1;
#endif /* defined(STM32_I2C_V2) */
    mr_i2c_config_t config;
    i2c_slave_t slave;
    mr_uint8_t ev_irq_valid: 1;
    mr_uint8_t er_irq_valid: 1;
    mr_uint8_t pins_inited: 1;
    mr_uint8_t initialized: 1;
    mr_uint8_t addr10_active: 1;
} i2c_priv_t;

static mr_bool_t i2c_fifo_push(i2c_slave_fifo_t *fifo, mr_uint8_t byte) {
    if ((fifo->data == MR_NULL) || (fifo->cap == 0U)
        || (fifo->count >= fifo->cap)) {
        return MR_FALSE;
    }
    fifo->data[fifo->head] = byte;
    fifo->head = (fifo->head + 1U) % fifo->cap;
    fifo->count++;
    return MR_TRUE;
}

static mr_bool_t i2c_fifo_pop(i2c_slave_fifo_t *fifo, mr_uint8_t *byte) {
    if ((fifo->data == MR_NULL) || (fifo->cap == 0U) || (fifo->count == 0U)) {
        return MR_FALSE;
    }
    *byte = fifo->data[fifo->tail];
    fifo->tail = (fifo->tail + 1U) % fifo->cap;
    fifo->count--;
    return MR_TRUE;
}

static void i2c_fifo_reset(i2c_slave_fifo_t *fifo) {
    fifo->head = 0U;
    fifo->tail = 0U;
    fifo->count = 0U;
}

static void i2c_slave_notify(const i2c_priv_t *priv, mr_uint32_t events) {
    events &= priv->slave.event_mask;
    if ((events != 0U) && (priv->driver != MR_NULL)) {
        mr_i2c_event_raise(priv->driver, events);
    }
}

static void i2c_slave_irq_control(i2c_priv_t *priv, mr_bool_t enable) {
#if defined(STM32_I2C_V2)
    uint32_t cr1_it = I2C_CR1_ADDRIE | I2C_CR1_RXIE | I2C_CR1_TXIE
                    | I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
    if (enable) {
        SET_BIT(priv->handle.Instance->CR1, cr1_it);
    } else {
        CLEAR_BIT(priv->handle.Instance->CR1, cr1_it);
    }
#else
    uint32_t cr2_it = I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN;
    if (enable) {
        SET_BIT(priv->handle.Instance->CR1, I2C_CR1_ACK);
        SET_BIT(priv->handle.Instance->CR2, cr2_it);
    } else {
        CLEAR_BIT(priv->handle.Instance->CR2, cr2_it);
        CLEAR_BIT(priv->handle.Instance->CR1, I2C_CR1_ACK);
    }
#endif /* defined(STM32_I2C_V2) */
}

static void i2c_slave_disable(i2c_priv_t *priv) {
    int mask;

    if (!priv->slave.active) {
        return;
    }

    mask = mr_port_irq_disable();
    priv->slave.active = 0U;
    priv->slave.sending = 0U;
    if (priv->initialized) {
        i2c_slave_irq_control(priv, MR_FALSE);
    }
    if (priv->ev_irq_valid) {
        HAL_NVIC_DisableIRQ(priv->ev_irqn);
    }
    if (priv->er_irq_valid && (priv->er_irqn != priv->ev_irqn)) {
        HAL_NVIC_DisableIRQ(priv->er_irqn);
    }
    i2c_fifo_reset(&priv->slave.rx_fifo);
    i2c_fifo_reset(&priv->slave.tx_fifo);
    mr_port_irq_enable(mask);
}

#if defined(STM32_I2C_V2)
static mr_err_t i2c_compute_timing(mr_uint32_t clock_rate, uint32_t *timing) {
    mr_uint32_t pclk, presc, scl_ticks, scll, sclh, sdadel, scldel;

    pclk = HAL_RCC_GetPCLK1Freq();
    if ((pclk == 0U) || (clock_rate == 0U)
        || (clock_rate > I2C_FAST_PLUS_MAX_HZ)) {
        return -MR_ERANGE;
    }

    /* Find prescaler so total SCL period fits within 8-bit SCLL + SCLH */
    for (presc = 0U; presc < 16U; presc++) {
        mr_uint32_t t_presc_clk = pclk / (presc + 1U);

        if (t_presc_clk < (clock_rate * 8U)) {
            break;
        }
        scl_ticks = (t_presc_clk + (clock_rate / 2U)) / clock_rate;
        if (scl_ticks <= 512U) {
            /* Standard mode uses ~50/50 duty, fast mode uses ~62/38 low/high */
            if (clock_rate <= I2C_STANDARD_MAX_HZ) {
                scll = scl_ticks / 2U;
                sclh = scl_ticks - scll;
                scldel = 4U;
                sdadel = 2U;
            } else {
                scll = (scl_ticks * 5U) / 8U;
                sclh = scl_ticks - scll;
                scldel = 3U;
                sdadel = 1U;
            }
            if ((scll == 0U) || (sclh == 0U) || (scll > 256U)
                || (sclh > 256U)) {
                continue;
            }
            *timing = ((presc & 0x0FU) << 28U) | ((scldel & 0x0FU) << 20U)
                    | ((sdadel & 0x0FU) << 16U)
                    | (((sclh - 1U) & 0xFFU) << 8U) | ((scll - 1U) & 0xFFU);
            return 0;
        }
    }
    return -MR_ERANGE;
}
#endif /* defined(STM32_I2C_V2) */

static mr_err_t i2c_error_status(const i2c_priv_t *priv,
                                 HAL_StatusTypeDef status) {
    uint32_t err;

    if (status == HAL_OK) {
        return 0;
    }
    if (status == HAL_TIMEOUT) {
        return -MR_ETIMEDOUT;
    }
    if (status == HAL_BUSY) {
        return -MR_EBUSY;
    }

    err = HAL_I2C_GetError(&priv->handle);
    if ((err & HAL_I2C_ERROR_ARLO) != 0U) {
        return -MR_EAGAIN;
    }
    if ((err & HAL_I2C_ERROR_TIMEOUT) != 0U) {
        return -MR_ETIMEDOUT;
    }
    return -MR_EIO;
}

static mr_err_t i2c_hw_init(i2c_priv_t *priv, mr_uint32_t clock_rate,
                            mr_uint16_t own_addr, mr_bool_t addr10) {
    I2C_InitTypeDef init = {0};
#if defined(STM32_I2C_V2)
    uint32_t timing;
#endif /* defined(STM32_I2C_V2) */
    mr_err_t ret;

#if defined(STM32_I2C_V2)
    if (priv->has_timing_override) {
        timing = priv->timing_override;
    } else {
        ret = i2c_compute_timing(clock_rate, &timing);
        if (ret != 0) {
            return ret;
        }
    }
    init.Timing = timing;
#else
    if ((clock_rate == 0U) || (clock_rate > I2C_FAST_MAX_HZ)) {
        return -MR_ERANGE;
    }
    init.ClockSpeed = clock_rate;
    init.DutyCycle = I2C_DUTYCYCLE_2;
#endif /* defined(STM32_I2C_V2) */

    init.OwnAddress1
        = addr10 ? (uint32_t)(own_addr & 0x03FFU)
                 : (uint32_t)((own_addr & 0x7FU) << 1U);
    init.AddressingMode
        = addr10 ? I2C_ADDRESSINGMODE_10BIT : I2C_ADDRESSINGMODE_7BIT;
    init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    init.OwnAddress2 = 0U;
#if defined(I2C_OA2_NOMASK)
    init.OwnAddress2Masks = I2C_OA2_NOMASK;
#endif /* defined(I2C_OA2_NOMASK) */
    init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    /* Enable clock and initialize peripheral */
    priv->clk_enable();
    if (priv->initialized) {
        HAL_I2C_DeInit(&priv->handle);
        priv->initialized = 0U;
    }
    priv->handle.Init = init;
    ret = mr_stm32_hal_status(HAL_I2C_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }

    priv->addr10_active = addr10 ? 1U : 0U;
    priv->initialized = 1U;
    return 0;
}

static mr_err_t i2c_pins_init(i2c_priv_t *priv) {
    mr_err_t ret;

    if (priv->pins_inited) {
        return 0;
    }
    if ((priv->scl.spec.drv == MR_NULL) || (priv->sda.spec.drv == MR_NULL)) {
        return -MR_ENODEV;
    }

    /* Configure SCL and SDA as open-drain alternate function with pull-up */
    ret = mr_stm32_gpio_pin_alternate(&priv->scl.spec, priv->scl.alternate,
                                      MR_GPIO_PULL_UP, MR_GPIO_OTYPE_OD);
    if (ret != 0) {
        return ret;
    }
    ret = mr_stm32_gpio_pin_alternate(&priv->sda.spec, priv->sda.alternate,
                                      MR_GPIO_PULL_UP, MR_GPIO_OTYPE_OD);
    if (ret != 0) {
        mr_stm32_gpio_pin_release(&priv->scl.spec);
        return ret;
    }
    priv->pins_inited = 1U;
    return 0;
}

static mr_err_t i2c_configure(const mr_driver_t *drv,
                              const mr_i2c_config_t *config) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_bool_t addr10 = (config->addr10 != 0U);
    mr_err_t ret;

    /* Check controller resources */
    if ((priv->handle.Instance == MR_NULL) || (priv->clk_enable == MR_NULL)) {
        return -MR_ENODEV;
    }

    /* Check role */
    if (config->role > MR_I2C_ROLE_SLAVE) {
        return -MR_EINVAL;
    }

    /* Stop controller when clock_rate == 0 */
    if (config->clock_rate == 0U) {
        i2c_slave_disable(priv);
        if (priv->initialized) {
            HAL_I2C_DeInit(&priv->handle);
            priv->initialized = 0U;
        }
        priv->config = *config;
        return 0;
    }

    /* Validate slave resources and address */
    if (config->role == MR_I2C_ROLE_SLAVE) {
        if (!priv->ev_irq_valid) {
            return -MR_ENODEV;
        }
        if ((!addr10 && (config->own_addr > 0x7FU))
            || (addr10 && (config->own_addr > 0x3FFU))) {
            return -MR_EINVAL;
        }
    }

    /* Disable active slave before reconfiguring */
    i2c_slave_disable(priv);

    /* Configure SCL/SDA pins */
    ret = i2c_pins_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Init I2C peripheral */
    ret = i2c_hw_init(priv, config->clock_rate,
                      (config->role == MR_I2C_ROLE_SLAVE) ? config->own_addr
                                                          : 0U,
                      addr10);
    if (ret != 0) {
        return ret;
    }

    priv->driver = (mr_driver_t *)drv;
    priv->config = *config;

    /* Enable slave interrupts and NVIC vectors */
    if (config->role == MR_I2C_ROLE_SLAVE) {
        i2c_fifo_reset(&priv->slave.rx_fifo);
        i2c_fifo_reset(&priv->slave.tx_fifo);
        priv->slave.sending = 0U;
        priv->slave.active = 1U;

        HAL_NVIC_SetPriority(priv->ev_irqn, priv->irq_priority, 0U);
        HAL_NVIC_ClearPendingIRQ(priv->ev_irqn);
        HAL_NVIC_EnableIRQ(priv->ev_irqn);
        if (priv->er_irq_valid && (priv->er_irqn != priv->ev_irqn)) {
            HAL_NVIC_SetPriority(priv->er_irqn, priv->irq_priority, 0U);
            HAL_NVIC_ClearPendingIRQ(priv->er_irqn);
            HAL_NVIC_EnableIRQ(priv->er_irqn);
        }
        i2c_slave_irq_control(priv, MR_TRUE);
    }
    return 0;
}

static mr_err_t i2c_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);

    if (!priv->initialized) {
        return -MR_ENODEV;
    }
    *config = priv->config;
    return 0;
}

static mr_err_t i2c_ensure_addr_mode(i2c_priv_t *priv, mr_bool_t addr10) {
    if ((priv->addr10_active != 0U) == (addr10 != MR_FALSE)) {
        return 0;
    }
    return i2c_hw_init(priv, priv->config.clock_rate, 0U, addr10);
}

static uint16_t i2c_hal_addr(mr_uint16_t addr, mr_bool_t addr10) {
    return addr10 ? (uint16_t)(addr & 0x03FFU)
                  : (uint16_t)((addr & 0x7FU) << 1U);
}

static mr_err_t i2c_check_msg(const mr_i2c_msg_t *msg) {
    mr_bool_t addr10 = (msg->flags & MR_I2C_MSG_ADDR10) != 0U;

    if ((msg->flags & MR_I2C_MSG_STOP) && (msg->flags & MR_I2C_MSG_NOSTOP)) {
        return -MR_EINVAL;
    }
    if ((msg->len != 0U) && (msg->buf == MR_NULL)) {
        return -MR_EINVAL;
    }
    if (msg->len > 0xFFFFU) {
        return -MR_ERANGE;
    }
    if (!addr10 && (msg->addr > 0x7FU)) {
        return -MR_EINVAL;
    }
    if (addr10 && (msg->addr > 0x3FFU)) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_ssize_t i2c_transfer(const mr_driver_t *drv,
                               const mr_i2c_msg_t *msgs, mr_size_t num) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    HAL_StatusTypeDef status;
    mr_size_t i;
    mr_err_t ret;

    if (!priv->initialized) {
        return -MR_ENODEV;
    }
    if (priv->slave.active) {
        return -MR_EPERM;
    }

    /* Validate all messages before touching the bus */
    for (i = 0U; i < num; i++) {
        ret = i2c_check_msg(&msgs[i]);
        if (ret != 0) {
            return ret;
        }
    }

    for (i = 0U; i < num; i++) {
        const mr_i2c_msg_t *msg = &msgs[i];
        mr_bool_t is_read = (msg->flags & MR_I2C_MSG_READ) != 0U;
        mr_bool_t addr10 = (msg->flags & MR_I2C_MSG_ADDR10) != 0U;
        uint16_t dev_addr = i2c_hal_addr(msg->addr, addr10);

        ret = i2c_ensure_addr_mode(priv, addr10);
        if (ret != 0) {
            return (i != 0U) ? (mr_ssize_t)i : ret;
        }

        /* Combine Write(1..2B reg, NOSTOP) + Read(same addr) via Mem_Read */
        if (!is_read && ((msg->flags & MR_I2C_MSG_NOSTOP) != 0U)
            && ((i + 1U) < num) && ((msg->len == 1U) || (msg->len == 2U))) {
            const mr_i2c_msg_t *next = &msgs[i + 1U];
            mr_bool_t next_read = (next->flags & MR_I2C_MSG_READ) != 0U;
            mr_bool_t next_addr10 = (next->flags & MR_I2C_MSG_ADDR10) != 0U;

            if (next_read && (next->addr == msg->addr)
                && (next_addr10 == addr10) && (next->len != 0U)
                && ((next->flags & MR_I2C_MSG_NOSTOP) == 0U)) {
                uint16_t mem_addr = (msg->len == 1U)
                                      ? (uint16_t)msg->buf[0]
                                      : (uint16_t)(((uint16_t)msg->buf[0] << 8U)
                                                   | msg->buf[1]);
                uint16_t mem_size = (msg->len == 1U) ? I2C_MEMADD_SIZE_8BIT
                                                     : I2C_MEMADD_SIZE_16BIT;
                status = HAL_I2C_Mem_Read(&priv->handle, dev_addr, mem_addr,
                                          mem_size, next->buf,
                                          (uint16_t)next->len,
                                          priv->timeout_ms);
                ret = i2c_error_status(priv, status);
                if (ret != 0) {
                    return (i != 0U) ? (mr_ssize_t)i : ret;
                }
                i++;
                continue;
            }
        }

        /* Zero-length write serves as device address probe */
        if (msg->len == 0U) {
            if (is_read) {
                return (i != 0U) ? (mr_ssize_t)i : -MR_ENOSYS;
            }
            status = HAL_I2C_IsDeviceReady(&priv->handle, dev_addr, 1U,
                                           priv->timeout_ms);
            ret = i2c_error_status(priv, status);
            if (ret != 0) {
                return (i != 0U) ? (mr_ssize_t)i : ret;
            }
            continue;
        }

        /* Standard master read or write transaction */
        if (is_read) {
            status = HAL_I2C_Master_Receive(&priv->handle, dev_addr, msg->buf,
                                            (uint16_t)msg->len,
                                            priv->timeout_ms);
        } else {
            status = HAL_I2C_Master_Transmit(&priv->handle, dev_addr, msg->buf,
                                             (uint16_t)msg->len,
                                             priv->timeout_ms);
        }
        ret = i2c_error_status(priv, status);
        if (ret != 0) {
            return (i != 0U) ? (mr_ssize_t)i : ret;
        }
    }
    return (mr_ssize_t)num;
}

static mr_ssize_t i2c_slave_read(const mr_driver_t *drv, void *buf,
                                 mr_size_t size) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t *dst = (mr_uint8_t *)buf;
    mr_size_t count = 0U;
    int mask;

    if (!priv->slave.active) {
        return -MR_ENODEV;
    }

    mask = mr_port_irq_disable();
    while ((count < size) && i2c_fifo_pop(&priv->slave.rx_fifo, &dst[count])) {
        count++;
    }
    mr_port_irq_enable(mask);
    return (mr_ssize_t)count;
}

static mr_ssize_t i2c_slave_write(const mr_driver_t *drv, const void *buf,
                                  mr_size_t size) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uint8_t *src = (const mr_uint8_t *)buf;
    mr_size_t count = 0U;
    int mask;

    if (!priv->slave.active) {
        return -MR_ENODEV;
    }

    mask = mr_port_irq_disable();
    while ((count < size) && i2c_fifo_push(&priv->slave.tx_fifo, src[count])) {
        count++;
    }
    mr_port_irq_enable(mask);
    return (mr_ssize_t)count;
}

static mr_err_t i2c_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    if ((events == 0U) || ((events & ~I2C_SLAVE_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }
    if (!priv->slave.active) {
        return -MR_ENODEV;
    }

    mask = mr_port_irq_disable();
    if (enable) {
        priv->slave.event_mask |= events;
    } else {
        priv->slave.event_mask &= ~events;
    }
    mr_port_irq_enable(mask);
    return 0;
}

MR_UNUSED static void i2c_slave_irq(i2c_priv_t *priv) {
    I2C_TypeDef *inst = priv->handle.Instance;
    mr_uint32_t events = 0U;
    mr_uint8_t byte;

    if (!priv->slave.active) {
        return;
    }

#if defined(STM32_I2C_V2)
    uint32_t isr = inst->ISR;

    /* Handle bus errors */
    if ((isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) != 0U) {
        inst->ICR = isr & (I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF);
        events |= MR_I2C_WAIT_ERROR;
    }

    /* Handle address match */
    if ((isr & I2C_ISR_ADDR) != 0U) {
        priv->slave.sending = ((isr & I2C_ISR_DIR) != 0U) ? 1U : 0U;
        if (priv->slave.sending) {
            /* Flush stale TXDR byte before starting new master-read */
            SET_BIT(inst->ISR, I2C_ISR_TXE);
            events |= MR_I2C_WAIT_TX_START | MR_I2C_WAIT_TX_READY;
        } else {
            events |= MR_I2C_WAIT_RX_START;
        }
        inst->ICR = I2C_ICR_ADDRCF;
    }

    /* Handle received byte */
    if ((isr & I2C_ISR_RXNE) != 0U) {
        byte = (mr_uint8_t)inst->RXDR;
        if (i2c_fifo_push(&priv->slave.rx_fifo, byte)) {
            events |= MR_I2C_WAIT_RX_READY;
        } else {
            events |= MR_I2C_WAIT_ERROR;
        }
    }

    /* Handle transmit request */
    if ((isr & I2C_ISR_TXIS) != 0U) {
        if (!i2c_fifo_pop(&priv->slave.tx_fifo, &byte)) {
            byte = 0xFFU;
        }
        inst->TXDR = byte;
        if (priv->slave.tx_fifo.count < priv->slave.tx_fifo.cap) {
            events |= MR_I2C_WAIT_TX_READY;
        }
    }

    /* Handle NACK (normal end of master-read) */
    if ((isr & I2C_ISR_NACKF) != 0U) {
        inst->ICR = I2C_ICR_NACKCF;
        priv->slave.sending = 0U;
    }

    /* Handle STOP */
    if ((isr & I2C_ISR_STOPF) != 0U) {
        inst->ICR = I2C_ICR_STOPCF;
        priv->slave.sending = 0U;
        events |= MR_I2C_WAIT_STOP;
    }
#else
    uint32_t sr1 = inst->SR1;

    /* Handle bus errors */
    if ((sr1 & (I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR)) != 0U) {
        CLEAR_BIT(inst->SR1, I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR);
        events |= MR_I2C_WAIT_ERROR;
    }
    if ((sr1 & I2C_SR1_AF) != 0U) {
        CLEAR_BIT(inst->SR1, I2C_SR1_AF);
        if (!priv->slave.sending) {
            events |= MR_I2C_WAIT_ERROR;
        }
        priv->slave.sending = 0U;
    }

    /* Handle address match */
    if ((sr1 & I2C_SR1_ADDR) != 0U) {
        uint32_t sr2 = inst->SR2;
        priv->slave.sending = ((sr2 & I2C_SR2_TRA) != 0U) ? 1U : 0U;
        if (priv->slave.sending) {
            events |= MR_I2C_WAIT_TX_START | MR_I2C_WAIT_TX_READY;
        } else {
            events |= MR_I2C_WAIT_RX_START;
        }
    }

    /* Handle received byte */
    if ((sr1 & I2C_SR1_RXNE) != 0U) {
        byte = (mr_uint8_t)inst->DR;
        if (i2c_fifo_push(&priv->slave.rx_fifo, byte)) {
            events |= MR_I2C_WAIT_RX_READY;
        } else {
            events |= MR_I2C_WAIT_ERROR;
        }
    }

    /* Handle transmit request */
    if (priv->slave.sending && ((sr1 & I2C_SR1_TXE) != 0U)) {
        if (!i2c_fifo_pop(&priv->slave.tx_fifo, &byte)) {
            byte = 0xFFU;
        }
        inst->DR = byte;
        if (priv->slave.tx_fifo.count < priv->slave.tx_fifo.cap) {
            events |= MR_I2C_WAIT_TX_READY;
        }
    }

    /* Handle STOP */
    if ((sr1 & I2C_SR1_STOPF) != 0U) {
        SET_BIT(inst->CR1, I2C_CR1_PE);
        priv->slave.sending = 0U;
        events |= MR_I2C_WAIT_STOP;
    }
#endif /* defined(STM32_I2C_V2) */

    if (events != 0U) {
        i2c_slave_notify(priv, events);
    }
}

/* Instance install */
#define __ST_HT_I2C_PIN(_id, _prop, _af_prop)                                  \
    {                                                                          \
        .spec = MR_HT_GPIO_SPEC_OR(_id, _prop, {0}),                           \
        .alternate = MR_HT_DRIVER_PROP_OR(_id, _af_prop, 0U),                  \
    }
#define __ST_HT_I2C_HAS_SINGLE_IRQ(_id)                                        \
    MR_MACRO_AND(MR_HT_DRIVER_HAS_PROP(_id, st_irqn),                          \
                 MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler))
#define __ST_HT_I2C_HAS_EV_IRQ(_id)                                            \
    MR_MACRO_AND(MR_HT_DRIVER_HAS_PROP(_id, st_ev_irqn),                       \
                 MR_HT_DRIVER_HAS_PROP(_id, st_ev_irq_handler))
#define __ST_HT_I2C_HAS_ER_IRQ(_id)                                            \
    MR_MACRO_AND(MR_HT_DRIVER_HAS_PROP(_id, st_er_irqn),                       \
                 MR_HT_DRIVER_HAS_PROP(_id, st_er_irq_handler))
#define __ST_HT_I2C_EV_IRQN(_id)                                               \
    MR_MACRO_IF(__ST_HT_I2C_HAS_EV_IRQ(_id),                                   \
                (MR_HT_DRIVER_PROP(_id, st_ev_irqn)),                          \
                (MR_HT_DRIVER_PROP_OR(_id, st_irqn, (IRQn_Type)0)))
#define __ST_HT_I2C_ER_IRQN(_id)                                               \
    MR_MACRO_IF(__ST_HT_I2C_HAS_ER_IRQ(_id),                                   \
                (MR_HT_DRIVER_PROP(_id, st_er_irqn)),                          \
                (MR_HT_DRIVER_PROP_OR(_id, st_irqn, (IRQn_Type)0)))
#define __ST_HT_I2C_IRQ_HANDLERS(_id)                                          \
    MR_MACRO_IF(__ST_HT_I2C_HAS_SINGLE_IRQ(_id),                               \
                (void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {           \
                    i2c_slave_irq(&MR_CAT(__i2c_priv_, _id));                  \
                }),                                                            \
                ())                                                            \
    MR_MACRO_IF(__ST_HT_I2C_HAS_EV_IRQ(_id),                                   \
                (void MR_HT_DRIVER_PROP(_id, st_ev_irq_handler)(void) {        \
                    i2c_slave_irq(&MR_CAT(__i2c_priv_, _id));                  \
                }),                                                            \
                ())                                                            \
    MR_MACRO_IF(__ST_HT_I2C_HAS_ER_IRQ(_id),                                   \
                (void MR_HT_DRIVER_PROP(_id, st_er_irq_handler)(void) {        \
                    i2c_slave_irq(&MR_CAT(__i2c_priv_, _id));                  \
                }),                                                            \
                ())
#if defined(STM32_I2C_V2)
#define __ST_HT_I2C_TIMING_INIT(_id)                                           \
    .timing_override = MR_HT_DRIVER_PROP_OR(_id, st_timing, 0U),               \
    .has_timing_override = MR_HT_DRIVER_HAS_PROP(_id, st_timing),
#else
#define __ST_HT_I2C_TIMING_INIT(_id)
#endif /* defined(STM32_I2C_V2) */
#define __ST_HT_I2C_HAS_SLAVE_IRQ(_id)                                         \
    MR_MACRO_OR(__ST_HT_I2C_HAS_EV_IRQ(_id), __ST_HT_I2C_HAS_SINGLE_IRQ(_id))
#define __ST_HT_I2C_SLAVE_RX_CAP(_id)                                          \
    MR_HT_DRIVER_PROP_OR(_id, st_slave_rx_fifo, I2C_DEFAULT_SLAVE_FIFO_SIZE)
#define __ST_HT_I2C_SLAVE_TX_CAP(_id)                                          \
    MR_HT_DRIVER_PROP_OR(_id, st_slave_tx_fifo, I2C_DEFAULT_SLAVE_FIFO_SIZE)
#define __ST_HT_I2C_SLAVE_FIFO_DEFINE(_id)                                     \
    MR_MACRO_IF(__ST_HT_I2C_HAS_SLAVE_IRQ(_id),                                \
                (static mr_uint8_t MR_CAT(                                     \
                     __i2c_slave_rx_buf_, _id)[__ST_HT_I2C_SLAVE_RX_CAP(_id)]; \
                 static mr_uint8_t MR_CAT(                                     \
                     __i2c_slave_tx_buf_,                                      \
                     _id)[__ST_HT_I2C_SLAVE_TX_CAP(_id)];),                    \
                ())
#define __ST_HT_I2C_SLAVE_INIT(_id)                                            \
    MR_MACRO_IF(__ST_HT_I2C_HAS_SLAVE_IRQ(_id),                                \
                (.slave =                                                      \
                     {                                                         \
                         .rx_fifo =                                            \
                             {                                                 \
                                 .data = MR_CAT(__i2c_slave_rx_buf_, _id),     \
                                 .cap = __ST_HT_I2C_SLAVE_RX_CAP(_id),         \
                             },                                                \
                         .tx_fifo =                                            \
                             {                                                 \
                                 .data = MR_CAT(__i2c_slave_tx_buf_, _id),     \
                                 .cap = __ST_HT_I2C_SLAVE_TX_CAP(_id),         \
                             },                                                \
                     },),                                                      \
                ())
#define ST_HT_I2C_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__i2c_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    __ST_HT_I2C_SLAVE_FIFO_DEFINE(_id)                                         \
    static i2c_priv_t MR_CAT(__i2c_priv_, _id) = {                             \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_i2cx),                       \
        },                                                                     \
        .clk_enable = MR_CAT(__i2c_clk_enable_, _id),                          \
        .scl = __ST_HT_I2C_PIN(_id, st_scl, st_scl_af),                        \
        .sda = __ST_HT_I2C_PIN(_id, st_sda, st_sda_af),                        \
        .ev_irqn = __ST_HT_I2C_EV_IRQN(_id),                                   \
        .er_irqn = __ST_HT_I2C_ER_IRQN(_id),                                   \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, st_irq_priority, 0U),        \
        .ev_irq_valid = __ST_HT_I2C_HAS_SLAVE_IRQ(_id),                        \
        .er_irq_valid =                                                        \
            MR_MACRO_OR(__ST_HT_I2C_HAS_ER_IRQ(_id),                           \
                        __ST_HT_I2C_HAS_SINGLE_IRQ(_id)),                      \
        .timeout_ms =                                                          \
            MR_HT_DRIVER_PROP_OR(_id, st_timeout_ms, I2C_DEFAULT_TIMEOUT_MS),  \
        __ST_HT_I2C_SLAVE_INIT(_id)                                            \
        __ST_HT_I2C_TIMING_INIT(_id)                                           \
    };                                                                         \
    __ST_HT_I2C_IRQ_HANDLERS(_id)                                              \
    MR_HT_I2C_INSTALL(_id, &MR_CAT(__i2c_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_I2C_INSTALL, &__i2c_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_I2C) */

/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#include <mrX/kernel/mr_fifo.h>
#include <mrX/port/mr_port_clock.h>
#include <mrX/port/mr_port_irq.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_i2c
#if MR_HT_HAS_DRIVER()

/* Driver I2C operations */
static mr_err_t i2c_configure(const mr_driver_t *drv,
                              const mr_i2c_config_t *config);
static mr_err_t i2c_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config);
static mr_ssize_t i2c_transfer(const mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                               mr_size_t num);
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

/* I2C status flag definition */
#define I2C_ERRORS                                                             \
    (I2C_STAR1_ARLO | I2C_STAR1_BERR | I2C_STAR1_AF | I2C_STAR1_OVR)

/* I2C slave event mask definition */
#define I2C_SLAVE_EVENTS                                                       \
    (MR_I2C_WAIT_RX_START | MR_I2C_WAIT_RX_READY | MR_I2C_WAIT_TX_START        \
     | MR_I2C_WAIT_TX_READY | MR_I2C_WAIT_STOP | MR_I2C_WAIT_ERROR)

/* I2C slave IRQ mask definition */
#define I2C_SLAVE_IRQS                                                         \
    (I2C_CTLR2_ITERREN | I2C_CTLR2_ITEVTEN | I2C_CTLR2_ITBUFEN)

/* I2C slave default FIFO size definition */
#define I2C_SLAVE_FIFO_SIZE             (32U)

/* I2C IRQ definition */
#define I2C_NO_IRQ                      (-1)

#if defined(MR_USE_CH32V10X) || defined(MR_USE_CH32V20X)                       \
    || defined(MR_USE_CH32V205) || defined(MR_USE_CH32V30X)                    \
    || defined(MR_USE_CH32V4X7)
#define CH32_I2C_HAS_RTR
#endif /* CH32 EVT SDK family */

/* I2C slave type */
typedef struct i2c_slave {
    mr_fifo_t rx, tx;
    mr_uint8_t *rx_buffer, *tx_buffer;
    mr_size_t rx_size, tx_size;
    mr_uint32_t events;
    mr_bool_t sending;
} i2c_slave_t;

/* I2C private data type */
typedef struct i2c_priv {
    I2C_TypeDef *instance;
    void (*prepare)(void);
    mr_uint32_t timeout;
    mr_i2c_config_t config;
    mr_uint16_t freq, ccr;
#if defined(CH32_I2C_HAS_RTR)
    mr_uint16_t rise;
#endif /* defined(CH32_I2C_HAS_RTR) */
    int ev_irq, er_irq;
    mr_uint8_t irq_priority;
    i2c_slave_t *slave;
    mr_atomic_t busy;
} i2c_priv_t;

static void i2c_slave_disable(i2c_priv_t *priv) {
    /* Disable slave IRQs */
    priv->instance->CTLR2 &= ~I2C_SLAVE_IRQS;
    priv->instance->CTLR1 = 0U;
    NVIC_DisableIRQ((IRQn_Type)priv->ev_irq);
    NVIC_DisableIRQ((IRQn_Type)priv->er_irq);
    NVIC_ClearPendingIRQ((IRQn_Type)priv->ev_irq);
    NVIC_ClearPendingIRQ((IRQn_Type)priv->er_irq);
    if (priv->slave != MR_NULL) {
        priv->slave->sending = MR_FALSE;
    }
}

static void i2c_slave_enable(i2c_priv_t *priv) {
    /* Enable slave IRQs */
    NVIC_ClearPendingIRQ((IRQn_Type)priv->ev_irq);
    NVIC_ClearPendingIRQ((IRQn_Type)priv->er_irq);
    NVIC_SetPriority((IRQn_Type)priv->ev_irq, priv->irq_priority);
    NVIC_SetPriority((IRQn_Type)priv->er_irq, priv->irq_priority);
    NVIC_EnableIRQ((IRQn_Type)priv->ev_irq);
    NVIC_EnableIRQ((IRQn_Type)priv->er_irq);
    priv->instance->CTLR2 |= I2C_SLAVE_IRQS;
    priv->instance->CTLR1 |= I2C_CTLR1_ACK;
}

static void i2c_restore(const i2c_priv_t *priv) {
    I2C_TypeDef *bus;

    bus = priv->instance;
    /* Reset the controller state machine before restoring timing */
    bus->CTLR1 = I2C_CTLR1_SWRST;
    bus->CTLR1 = 0U;
    bus->CTLR2 = priv->freq;
    bus->CKCFGR = priv->ccr;
#if defined(CH32_I2C_HAS_RTR)
    bus->RTR = priv->rise;
#endif /* defined(CH32_I2C_HAS_RTR) */
    bus->OADDR1 = (1U << 14)
                | (priv->config.addr10
                   ? (I2C_OADDR1_ADDMODE | priv->config.own_addr)
                   : (priv->config.own_addr << 1));
    bus->CTLR1 = I2C_CTLR1_PE;
}

static mr_err_t i2c_configure(const mr_driver_t *drv,
                             const mr_i2c_config_t *config) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t pclk, mhz, ccr, divisor;
    RCC_ClocksTypeDef clocks;
    mr_bool_t fast;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((priv->instance == MR_NULL) || (priv->timeout == 0U)) {
        return -MR_EINVAL;
    }
    if (config->own_addr > (config->addr10 ? 1023U : 127U)) {
        return -MR_EINVAL;
    }
    if ((config->clock_rate > 400000U)
        || ((config->clock_rate != 0U) && (config->role == MR_I2C_ROLE_SLAVE)
            && ((priv->slave == MR_NULL) || !mr_ch32_irq_valid(priv->ev_irq)
                || !mr_ch32_irq_valid(priv->er_irq)))) {
        return -MR_ENOSYS;
    }

    /* Lock */
    if (mr_atomic_exchange(&priv->busy, 1)) {
        return -MR_EBUSY;
    }

    ret = 0;
    if (config->clock_rate == 0U) {
        /* Disable the current bus role */
        mask = mr_port_irq_disable();
        if (priv->config.clock_rate != 0U) {
            if (priv->config.role == MR_I2C_ROLE_SLAVE) {
                i2c_slave_disable(priv);
            } else {
                priv->instance->CTLR1 = 0U;
            }
        }
        priv->config = *config;
        mr_port_irq_enable(mask);
        goto _exit;
    }

    /* Calculate bus timing */
    RCC_GetClocksFreq(&clocks);
    pclk = clocks.PCLK1_Frequency;
    mhz = pclk / 1000000U;
    fast = config->clock_rate > 100000U;
#if defined(MR_USE_CH32V4X7)
    if ((pclk < 4000000U) || (pclk > 60000000U)) {
#elif defined(MR_USE_CH32V205)
    if ((mhz < (fast ? 4U : 2U)) || (pclk > 60000000U)) {
#elif defined(MR_USE_CH32V10X)
    if ((mhz < (fast ? 4U : 2U)) || (pclk > 36000000U)) {
#else
    if ((mhz < (fast ? 4U : 2U)) || (mhz > 63U)) {
#endif /* CH32 EVT SDK family */
        ret = -MR_ENOSYS;
        goto _exit;
    }
    divisor = config->clock_rate * (fast ? 3U : 2U);
    ccr = (pclk + divisor - 1U) / divisor;
    if (!fast && (ccr < 4U)) {
        ccr = 4U;
    }
    if (ccr > I2C_CKCFGR_CCR) {
        ret = -MR_ENOSYS;
        goto _exit;
    }

    /* Prepare board pins before enabling the controller */
    if (priv->config.clock_rate == 0U) {
        priv->prepare();
    }
    mask = mr_port_irq_disable();
    /* Read STAR2 before STAR1 to avoid acknowledging a pending slave ADDR */
    if ((priv->config.clock_rate != 0U)
        && ((priv->instance->STAR2 & I2C_STAR2_BUSY) != 0U)) {
        ret = -MR_EBUSY;
        mr_port_irq_enable(mask);
        goto _exit;
    }
    if ((priv->config.clock_rate != 0U)
        && (priv->config.role == MR_I2C_ROLE_SLAVE)) {
        i2c_slave_disable(priv);
    }

    /* Save I2C configuration */
    priv->freq = (mr_uint16_t)mhz;
    priv->ccr = (mr_uint16_t)(ccr | (fast ? I2C_CKCFGR_FS : 0U));
#if defined(CH32_I2C_HAS_RTR)
    priv->rise = (mr_uint16_t)((fast ? mhz * 3U / 10U : mhz) + 1U);
#endif /* defined(CH32_I2C_HAS_RTR) */
    priv->config = *config;
    priv->config.clock_rate = pclk / (ccr * (fast ? 3U : 2U));

    /* Init I2C */
    i2c_restore(priv);
    if (config->role == MR_I2C_ROLE_SLAVE) {
        /* Enable slave */
        mr_fifo_init(&priv->slave->rx, priv->slave->rx_buffer,
                     priv->slave->rx_size, 1U);
        mr_fifo_init(&priv->slave->tx, priv->slave->tx_buffer,
                     priv->slave->tx_size, 1U);
        i2c_slave_enable(priv);
    }
    mr_port_irq_enable(mask);

_exit:
    /* Unlock */
    mr_atomic_store(&priv->busy, 0);
    return ret;
}

static mr_err_t i2c_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Get I2C configuration */
    *config = priv->config;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t i2c_wait(const i2c_priv_t *priv, mr_uint16_t flags) {
    mr_uint32_t elapsed;
    mr_uint16_t status;

    /* Wait I2C flags */
    for (elapsed = 0U; ; elapsed++) {
        status = priv->instance->STAR1;
        if (status & I2C_STAR1_ARLO) {
            return -MR_EAGAIN;
        }
        if (status & (I2C_STAR1_BERR | I2C_STAR1_OVR)) {
            return -MR_EIO;
        }
        if (status & I2C_STAR1_AF) {
            return -MR_EIO;
        }
        if ((status & flags) == flags) {
            return 0;
        }
        if (elapsed == priv->timeout) {
            return -MR_ETIMEDOUT;
        }
        mr_port_clock_busy_wait(1U);
    }
}

static mr_err_t i2c_wait_stop(const i2c_priv_t *priv) {
    mr_uint32_t elapsed;

    /* Wait I2C stop */
    for (elapsed = 0U; ; elapsed++) {
        if ((priv->instance->CTLR1 & I2C_CTLR1_STOP) == 0U) {
            return 0;
        }
        if (elapsed == priv->timeout) {
            return -MR_ETIMEDOUT;
        }
        mr_port_clock_busy_wait(1U);
    }
}

static void i2c_clear_addr(I2C_TypeDef *bus) {
    /* Clear address match */
    MR_IGNORE(bus->STAR1);
    MR_IGNORE(bus->STAR2);
}

static mr_err_t i2c_address(const i2c_priv_t *priv, const mr_i2c_msg_t *msg,
                           mr_bool_t started) {
    I2C_TypeDef *bus;
    mr_uint16_t addr;
    mr_bool_t read;
    mr_err_t ret;

    /* Address the slave */
    bus = priv->instance;
    read = (msg->flags & MR_I2C_MSG_READ) != 0U;
    if (!started) {
        bus->CTLR1 |= I2C_CTLR1_START;
    }
    ret = i2c_wait(priv, I2C_STAR1_SB);
    if (ret != 0) {
        return ret;
    }
    /* START has completed, so ACK/POS can change before addressing */
    bus->CTLR1 = (bus->CTLR1 & ~I2C_CTLR1_POS) | I2C_CTLR1_ACK;
    if ((msg->flags & MR_I2C_MSG_ADDR10) == 0U) {
        bus->DATAR = (msg->addr << 1) | read;
        return i2c_wait(priv, I2C_STAR1_ADDR);
    }

    /* Address in ten-bit mode */
    addr = 0xf0U | ((msg->addr >> 7) & 0x6U);
    bus->DATAR = addr;
    ret = i2c_wait(priv, I2C_STAR1_ADD10);
    if (ret != 0) {
        return ret;
    }
    bus->DATAR = msg->addr & 0xffU;
    ret = i2c_wait(priv, I2C_STAR1_ADDR);
    if ((ret != 0) || !read) {
        return ret;
    }

    /* Ten-bit reads repeat START and resend the header with R/W set */
    i2c_clear_addr(bus);
    bus->CTLR1 |= I2C_CTLR1_START;
    ret = i2c_wait(priv, I2C_STAR1_SB);
    if (ret != 0) {
        return ret;
    }
    bus->DATAR = addr | 1U;
    return i2c_wait(priv, I2C_STAR1_ADDR);
}

static mr_err_t i2c_write(const i2c_priv_t *priv, const mr_i2c_msg_t *msg,
                         mr_uint16_t end) {
    I2C_TypeDef *bus;
    mr_err_t ret;
    mr_size_t i;

    /* Write message bytes */
    bus = priv->instance;
    i2c_clear_addr(bus);
    for (i = 0U; i < msg->len; i++) {
        ret = i2c_wait(priv, I2C_STAR1_TXE);
        if (ret != 0) {
            return ret;
        }
        bus->DATAR = msg->buf[i];
    }
    if (msg->len != 0U) {
        ret = i2c_wait(priv, I2C_STAR1_BTF);
        if (ret != 0) {
            return ret;
        }
    }
    bus->CTLR1 |= end;
    return 0;
}

static mr_err_t i2c_read(const i2c_priv_t *priv, const mr_i2c_msg_t *msg,
                        mr_uint16_t end) {
    I2C_TypeDef *bus;
    mr_err_t ret;
    mr_size_t i;
    int mask;

    /* Configure the short-read sequence while ADDR holds SCL low */
    bus = priv->instance;
    mask = mr_port_irq_disable();
    if (msg->len == 1U) {
        bus->CTLR1 &= ~I2C_CTLR1_ACK;
        i2c_clear_addr(bus);
        bus->CTLR1 |= end;
    } else if (msg->len == 2U) {
        bus->CTLR1 |= I2C_CTLR1_POS;
        i2c_clear_addr(bus);
        bus->CTLR1 &= ~I2C_CTLR1_ACK;
    } else {
        i2c_clear_addr(bus);
    }
    mr_port_irq_enable(mask);

    /* Read message bytes */
    i = 0U;
    while (msg->len - i > 3U) {
        ret = i2c_wait(priv, I2C_STAR1_BTF);
        if (ret != 0) {
            return ret;
        }
        msg->buf[i++] = (mr_uint8_t)bus->DATAR;
    }
    if (msg->len - i == 3U) {
        ret = i2c_wait(priv, I2C_STAR1_BTF);
        if (ret != 0) {
            return ret;
        }
        bus->CTLR1 &= ~I2C_CTLR1_ACK;
        msg->buf[i++] = (mr_uint8_t)bus->DATAR;
    }
    if (msg->len - i == 2U) {
        ret = i2c_wait(priv, I2C_STAR1_BTF);
        if (ret != 0) {
            return ret;
        }
        /* Fetch the last two bytes with STOP armed */
        mask = mr_port_irq_disable();
        bus->CTLR1 |= end;
        msg->buf[i++] = (mr_uint8_t)bus->DATAR;
        msg->buf[i] = (mr_uint8_t)bus->DATAR;
        mr_port_irq_enable(mask);
        return 0;
    }

    ret = i2c_wait(priv, I2C_STAR1_RXNE);
    if (ret == 0) {
        msg->buf[i] = (mr_uint8_t)bus->DATAR;
    }
    return ret;
}

static mr_err_t i2c_validate(const mr_i2c_msg_t *msgs, mr_size_t num) {
    const mr_i2c_msg_t *msg;
    mr_size_t i;

    /* Check messages */
    if (num > (mr_size_t)MR_SSIZE_MAX) {
        return -MR_EINVAL;
    }
    for (i = 0U; i < num; i++) {
        msg = &msgs[i];
        if (((msg->flags & (MR_I2C_MSG_STOP | MR_I2C_MSG_NOSTOP))
                == (MR_I2C_MSG_STOP | MR_I2C_MSG_NOSTOP))
            || (msg->addr > ((msg->flags & MR_I2C_MSG_ADDR10) ? 1023U : 127U))
            || ((msg->len != 0U) && !msg->buf)) {
            return -MR_EINVAL;
        }
        if (((msg->flags & MR_I2C_MSG_READ) && (msg->len == 0U))
            || ((msg->flags & MR_I2C_MSG_NOSTOP) && (i + 1U == num))) {
            return -MR_ENOSYS;
        }
    }
    return 0;
}

static mr_ssize_t i2c_transfer(const mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                               mr_size_t num) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_i2c_msg_t *msg;
    mr_bool_t started;
    I2C_TypeDef *bus;
    mr_uint16_t end;
    mr_err_t ret;
    mr_size_t i;

    /* Check messages */
    ret = i2c_validate(msgs, num);
    if ((ret != 0) || (num == 0U)) {
        return ret;
    }

    /* Lock */
    if (mr_atomic_exchange(&priv->busy, 1)) {
        return -MR_EBUSY;
    }
    bus = priv->instance;

    /* Check I2C state */
    if (priv->config.clock_rate == 0U) {
        ret = -MR_ENODEV;
        goto _exit;
    }
    if (priv->config.role != MR_I2C_ROLE_MASTER) {
        ret = -MR_ENOSYS;
        goto _exit;
    }

    /* Transfer messages */
    started = MR_FALSE;
    for (i = 0U; i < num; i++) {
        if (!started && (bus->STAR2 & I2C_STAR2_BUSY)) {
            ret = -MR_EBUSY;
            goto _exit;
        }
        msg = &msgs[i];
        end = ((i + 1U == num) || (msg->flags & MR_I2C_MSG_STOP))
              ? I2C_CTLR1_STOP : I2C_CTLR1_START;
        ret = i2c_address(priv, msg, started);
        if (ret == 0) {
            ret = (msg->flags & MR_I2C_MSG_READ)
                  ? i2c_read(priv, msg, end) : i2c_write(priv, msg, end);
        }
        if (ret == 0 && end == I2C_CTLR1_STOP) {
            ret = i2c_wait_stop(priv);
        }
        if (ret != 0) {
            /* Never drive STOP after arbitration passes to another master */
            if (ret == -MR_EAGAIN) {
                bus->STAR1 = (mr_uint16_t)~I2C_ERRORS;
                bus->CTLR1 = I2C_CTLR1_PE;
            } else {
                if (bus->STAR2 & I2C_STAR2_MSL) {
                    bus->CTLR1 |= I2C_CTLR1_STOP;
                    i2c_wait_stop(priv);
                }
                i2c_restore(priv);
            }
            goto _exit;
        }
        started = end == I2C_CTLR1_START;
    }

_exit:
    /* Unlock */
    mr_atomic_store(&priv->busy, 0);
    return (ret == 0) ? (mr_ssize_t)num : ret;
}

static mr_bool_t i2c_slave_active(const i2c_priv_t *priv) {
    /* Check slave state */
    return (priv->slave != MR_NULL) && (priv->config.clock_rate != 0U)
           && (priv->config.role == MR_I2C_ROLE_SLAVE)
           && ((priv->instance->CTLR1 & I2C_CTLR1_PE) != 0U);
}

static mr_ssize_t i2c_slave_read(const mr_driver_t *drv, void *buf,
                                  mr_size_t size) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_ssize_t count;
    int mask;

    /* Check parameter */
    if (size > (mr_size_t)MR_SSIZE_MAX) {
        return -MR_EINVAL;
    }

    /* Read slave FIFO */
    mask = mr_port_irq_disable();
    count = i2c_slave_active(priv)
            ? (mr_ssize_t)mr_fifo_read(&priv->slave->rx, buf, size)
            : -MR_ENODEV;
    mr_port_irq_enable(mask);
    return count;
}

static mr_ssize_t i2c_slave_write(const mr_driver_t *drv, const void *buf,
                                   mr_size_t size) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_ssize_t count;
    int mask;

    /* Check parameter */
    if (size > (mr_size_t)MR_SSIZE_MAX) {
        return -MR_EINVAL;
    }

    /* Write slave FIFO */
    mask = mr_port_irq_disable();
    count = i2c_slave_active(priv)
            ? (mr_ssize_t)mr_fifo_write(&priv->slave->tx, buf, size)
            : -MR_ENODEV;
    mr_port_irq_enable(mask);
    return count;
}

static mr_err_t i2c_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                    mr_bool_t enable) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if ((events == 0U) || ((events & ~I2C_SLAVE_EVENTS) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check slave capability */
    if (priv->slave == MR_NULL) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Update event mask */
    if (enable) {
        priv->slave->events |= events;
    } else {
        priv->slave->events &= ~events;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

MR_UNUSED static void i2c_slave_irq(mr_driver_t *drv) {
    i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_i2c_data_t *data;
    mr_uint16_t status, clear;
    mr_uint32_t events;
    i2c_slave_t *slave;
    I2C_TypeDef *bus;
    mr_uint8_t byte;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check slave state */
    if (!i2c_slave_active(priv)) {
        mr_port_irq_enable(mask);
        return;
    }
    data = MR_DRIVER_DATA(drv);
    if (!mr_driver_is_ready(drv) || !MR_OBJECT_IS_INITED(&data->event)) {
        i2c_slave_disable(priv);
        mr_port_irq_enable(mask);
        return;
    }

    /* Handle slave IRQ */
    slave = priv->slave;
    bus = priv->instance;
    status = bus->STAR1;
    clear = status & I2C_ERRORS;
    events = 0U;
    if (clear != 0U) {
        bus->STAR1 = (mr_uint16_t)~clear;
        /* The master's final NACK ends a normal slave transmit */
        if ((clear & ~I2C_STAR1_AF) != 0U) {
            events |= MR_I2C_WAIT_ERROR;
        }
        if ((clear & I2C_STAR1_AF) != 0U) {
            slave->sending = MR_FALSE;
            bus->CTLR2 &= ~I2C_CTLR2_ITBUFEN;
        }
    }
    if ((status & I2C_STAR1_RXNE) != 0U) {
        byte = (mr_uint8_t)bus->DATAR;
        if (mr_fifo_push(&slave->rx, &byte) == 0U) {
            events |= MR_I2C_WAIT_ERROR;
        } else {
            events |= MR_I2C_WAIT_RX_READY;
        }
    }
    if ((status & I2C_STAR1_STOPF) != 0U) {
        /* A read of STAR1 followed by a write to CTLR1 releases STOPF */
        bus->CTLR1 = I2C_CTLR1_PE | I2C_CTLR1_ACK;
        slave->sending = MR_FALSE;
        events |= MR_I2C_WAIT_STOP;
    }
    if ((status & I2C_STAR1_ADDR) != 0U) {
        /* STAR2 clears the address match and identifies the new direction */
        slave->sending = (bus->STAR2 & I2C_STAR2_TRA) != 0U;
        bus->CTLR2 |= I2C_CTLR2_ITBUFEN;
        events |= slave->sending ? MR_I2C_WAIT_TX_START : MR_I2C_WAIT_RX_START;
    }
    if (slave->sending && ((bus->STAR1 & I2C_STAR1_TXE) != 0U)) {
        events |= MR_I2C_WAIT_TX_READY;
    }

    /* Raise I2C event */
    if ((events & slave->events) != 0U) {
        mr_i2c_event_raise(drv, events & slave->events);
    }

    /* TX callbacks can supply a response before the hardware needs the byte */
    if (!mr_driver_is_ready(drv)) {
        i2c_slave_disable(priv);
    } else if (i2c_slave_active(priv) && slave->sending
        && ((bus->STAR1 & I2C_STAR1_TXE) != 0U)) {
        if (mr_fifo_pop(&slave->tx, &byte) == 0U) {
            byte = 0xffU;
        }
        bus->DATAR = byte;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

/* Instance install */
#define __CH32_HT_I2C_SLAVE_RX_FIFO(_id)                                       \
    MR_HT_DRIVER_PROP_OR(_id, wch_slave_rx_fifo, I2C_SLAVE_FIFO_SIZE)
#define __CH32_HT_I2C_SLAVE_TX_FIFO(_id)                                       \
    MR_HT_DRIVER_PROP_OR(_id, wch_slave_tx_fifo, I2C_SLAVE_FIFO_SIZE)
#define __CH32_HT_I2C_SLAVE_DEFINE(_id)                                        \
    MR_MACRO_IF(                                                               \
        MR_HT_DRIVER_HAS_PROP(_id, wch_ev_irq_handler),                        \
        (MR_MACRO_IF(                                                          \
            MR_HT_DRIVER_HAS_PROP(_id, wch_er_irq_handler),                    \
            (static mr_uint8_t MR_CAT(__i2c_slave_rx_, _id)                    \
                 [__CH32_HT_I2C_SLAVE_RX_FIFO(_id)];                           \
             static mr_uint8_t MR_CAT(__i2c_slave_tx_, _id)                    \
                 [__CH32_HT_I2C_SLAVE_TX_FIFO(_id)];                           \
             static i2c_slave_t MR_CAT(__i2c_slave_, _id) = {                  \
                 .rx_buffer = MR_CAT(__i2c_slave_rx_, _id),                    \
                 .tx_buffer = MR_CAT(__i2c_slave_tx_, _id),                    \
                 .rx_size = sizeof(MR_CAT(__i2c_slave_rx_, _id)),              \
                 .tx_size = sizeof(MR_CAT(__i2c_slave_tx_, _id)),              \
             };),                                                              \
            ())),                                                              \
        ())
#define __CH32_HT_I2C_SLAVE_PTR(_id)                                           \
    MR_MACRO_IF(                                                               \
        MR_HT_DRIVER_HAS_PROP(_id, wch_ev_irq_handler),                        \
        (MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_er_irq_handler),           \
                     (&MR_CAT(__i2c_slave_, _id)), (MR_NULL))),                \
        (MR_NULL))
#define CH32_HT_I2C_INSTALL(_id, _ops)                                         \
    extern void MR_HT_DRIVER_PROP(_id, wch_prepare)(void);                     \
    __CH32_HT_I2C_SLAVE_DEFINE(_id)                                            \
    static i2c_priv_t MR_CAT(__i2c_priv_, _id) = {                             \
        .instance = MR_HT_DRIVER_PROP(_id, wch_i2cx),                          \
        .prepare = MR_HT_DRIVER_PROP(_id, wch_prepare),                        \
        .timeout = MR_HT_DRIVER_PROP_OR(_id, wch_timeout_us, 25000U),          \
        .ev_irq = __CH32_HT_I2C_IRQ_NUMBER(_id, wch_ev_irqn),                  \
        .er_irq = __CH32_HT_I2C_IRQ_NUMBER(_id, wch_er_irqn),                  \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U),       \
        .slave = __CH32_HT_I2C_SLAVE_PTR(_id),                                 \
        .busy = MR_ATOMIC_INIT(0),                                             \
    };                                                                         \
    __CH32_HT_I2C_IRQ(_id, wch_ev_irq_handler)                                 \
    __CH32_HT_I2C_IRQ(_id, wch_er_irq_handler)                                 \
    MR_HT_I2C_INSTALL(_id, &MR_CAT(__i2c_priv_, _id), _ops)
#define __CH32_HT_I2C_IRQ(_id, _prop)                                          \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, _prop),                             \
                (MR_CH32_INTERRUPT void                                        \
                     MR_HT_DRIVER_PROP(_id, _prop)(void) {                     \
                     i2c_slave_irq(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));      \
                 }),                                                           \
                ())
#define __CH32_HT_I2C_IRQ_NUMBER(_id, _prop)                                   \
    MR_HT_DRIVER_PROP_OR(_id, _prop, I2C_NO_IRQ)
MR_HT_DRIVER_FOREACH(CH32_HT_I2C_INSTALL, &__i2c_ops);

#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_I2C) */

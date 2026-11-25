/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#if defined(MR_USE_DRIVER_SPI_STREAM)
#define SPI_USE_STREAM
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/kernel/mr_fifo.h>
#include <mrX/port/mr_port_clock.h>
#include <mrX/port/mr_port_irq.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_spi
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "CH32 SPI requires GPIO chip selects"
#endif /* !defined(MR_USE_DRIVER_GPIO) */

/* Driver SPI operations */
static mr_err_t spi_configure(const mr_driver_t *drv, mr_uint32_t cs,
                              const mr_spi_config_t *config);
static mr_err_t spi_configuration(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_spi_config_t *config);
static mr_ssize_t spi_transfer(const mr_driver_t *drv, mr_uint32_t cs,
                               const mr_spi_msg_t *msgs, mr_size_t num);
#if defined(SPI_USE_STREAM)
static mr_err_t spi_stream_start(const mr_driver_t *drv, mr_uint32_t cs,
                                 const mr_spi_msg_t *msg,
                                 const mr_spi_stream_config_t *config);
static mr_err_t spi_stream_stop(const mr_driver_t *drv);
static mr_err_t spi_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t spi_rx_stream_offset(const mr_driver_t *drv,
                                     mr_size_t *offset);
static mr_err_t spi_tx_stream_offset(const mr_driver_t *drv,
                                     mr_size_t *offset);
#endif /* defined(SPI_USE_STREAM) */
static mr_ssize_t spi_slave_read(const mr_driver_t *drv, mr_uint32_t cs,
                                 void *buf, mr_size_t size);
static mr_ssize_t spi_slave_write(const mr_driver_t *drv, mr_uint32_t cs,
                                  const void *buf, mr_size_t size);
static mr_err_t spi_event_control(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_uint32_t events, mr_bool_t enable);
static mr_err_t spi_slave_configure(const mr_driver_t *drv,
                                    const mr_spi_config_t *config);
MR_UNUSED static const mr_spi_ops_t __spi_ops = {
    .configure = spi_configure,
    .configuration = spi_configuration,
    .transfer = spi_transfer,
#if defined(SPI_USE_STREAM)
    .stream_start = spi_stream_start,
    .stream_stop = spi_stream_stop,
    .stream_is_busy = spi_stream_is_busy,
    .rx_stream_offset = spi_rx_stream_offset,
    .tx_stream_offset = spi_tx_stream_offset,
#endif /* defined(SPI_USE_STREAM) */
    .slave_read = spi_slave_read,
    .slave_write = spi_slave_write,
    .event_control = spi_event_control,
};

/* SPI IRQ definition */
#define SPI_NO_IRQ                      (-1)

/* SPI slave event mask definition */
#define SPI_SLAVE_EVENTS                                                       \
    (MR_SPI_WAIT_SELECT | MR_SPI_WAIT_RX_READY | MR_SPI_WAIT_TX_READY          \
     | MR_SPI_WAIT_DESELECT | MR_SPI_WAIT_ERROR)

/* SPI slave default FIFO frame count definition */
#define SPI_SLAVE_FIFO_FRAMES           (16U)

/* SPI slave type */
typedef struct spi_slave {
    mr_waiter_t nss;
    mr_fifo_t rx, tx;
    mr_uint16_t *rx_buffer, *tx_buffer;
    mr_size_t rx_size, tx_size;
    mr_uint16_t events, mode;
    mr_bool_t enabled, selected;
} spi_slave_t;

#if defined(SPI_USE_STREAM)
/* SPI stream event mask definition */
#define SPI_STREAM_EVENTS                                                      \
    (MR_SPI_WAIT_RX_STREAM_HALF | MR_SPI_WAIT_RX_STREAM_FULL                   \
     | MR_SPI_WAIT_TX_STREAM_HALF | MR_SPI_WAIT_TX_STREAM_FULL                 \
     | MR_SPI_WAIT_STREAM_ERROR | MR_SPI_WAIT_STREAM_DONE)

/* SPI final frame status poll count definition */
#define SPI_FINAL_POLLS                 (16U)

/* SPI master stream type */
typedef struct spi_stream {
    mr_spi_msg_t msg;
    mr_size_t rx_offset, tx_offset, frames_left, width;
    mr_uint32_t cs, generation;
    mr_uint16_t mode;
    mr_bool_t active, circular;
} spi_stream_t;
#endif /* defined(SPI_USE_STREAM) */

/* SPI private data type */
typedef struct spi_priv {
    SPI_TypeDef *instance;
    void (*prepare)(void);
    mr_ht_gpio_spec_t *selects;
    mr_spi_config_t *configs;
    mr_uint16_t *dividers;
    mr_size_t count;
    mr_uint32_t timeout;
    mr_bool_t ready;
    mr_atomic_t busy;
    spi_slave_t *slave;
    int irq;
    mr_uint8_t irq_priority;
#if defined(SPI_USE_STREAM)
    spi_stream_t stream;
    mr_uint16_t *events;
#endif /* defined(SPI_USE_STREAM) */
} spi_priv_t;

static mr_err_t spi_divider(const spi_priv_t *priv, mr_uint32_t target,
                            mr_uint32_t *rate, mr_uint16_t *bits) {
    mr_uint32_t pclk, divider;
    RCC_ClocksTypeDef clocks;
    mr_uint16_t shift;

    /* Get APB clock */
    RCC_GetClocksFreq(&clocks);
    pclk = (priv->instance == SPI1) ? clocks.PCLK2_Frequency
                                    : clocks.PCLK1_Frequency;
    if (pclk == 0U) {
        return -MR_EIO;
    }

    /* Find SPI divider */
    for (shift = 0U, divider = 2U; shift < 8U; shift++, divider *= 2U) {
        if ((pclk + divider - 1U) / divider <= target) {
            *rate = pclk / divider;
            *bits = shift << 3;
            return 0;
        }
    }
    return -MR_ENOSYS;
}

static mr_err_t spi_select(mr_ht_gpio_spec_t *spec, mr_uint8_t level) {
    mr_ssize_t ret;

    /* Write chip select level */
    if (!mr_driver_is_ready(spec->drv)) {
        return -MR_ENODEV;
    }
    ret = mr_ht_gpio_write(spec, level);
    if (ret != (mr_ssize_t)sizeof(level)) {
        return (ret < 0) ? (mr_err_t)ret : -MR_EIO;
    }
    return 0;
}

static mr_err_t spi_configure(const mr_driver_t *drv, mr_uint32_t cs,
                              const mr_spi_config_t *config) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_gpio_config_t gpio;
    mr_uint32_t rate;
    mr_uint16_t bits;
    mr_err_t ret;
    mr_size_t i;

    /* Check parameter */
    if ((cs >= priv->count) || (priv->instance == MR_NULL)
        || (priv->timeout == 0U)) {
        return -MR_EINVAL;
    }
    if (priv->slave != MR_NULL) {
        return (cs == 0U) ? spi_slave_configure(drv, config) : -MR_EINVAL;
    }

    /* Translate clock rate */
    rate = 0U;
    bits = 0U;
    if (config->clock_rate != 0U) {
        if ((config->role != MR_SPI_ROLE_MASTER)
            || ((config->data_bits != 8U) && (config->data_bits != 16U))) {
            return -MR_ENOSYS;
        }
        ret = spi_divider(priv, config->clock_rate, &rate, &bits);
        if (ret != 0) {
            return ret;
        }
    }

    /* Lock */
    if (mr_atomic_exchange(&priv->busy, 1)) {
        return -MR_EBUSY;
    }
    ret = 0;
    if (!priv->ready && (rate != 0U)) {
        /* Init SPI controller */
        gpio = (mr_gpio_config_t){
            .mode = MR_GPIO_MODE_OUTPUT,
            .level = MR_GPIO_LEVEL_HIGH,
        };
        /* Deassert all endpoints before the board starts driving SCK */
        for (i = 0U; i < priv->count; i++) {
            if (!mr_driver_is_ready(priv->selects[i].drv)) {
                ret = -MR_ENODEV;
                goto _exit;
            }
            ret = mr_ht_gpio_configure(&priv->selects[i], &gpio);
            if (ret != 0) {
                goto _exit;
            }
        }
        priv->prepare();
        priv->instance->CTLR1 = 0U;
        priv->instance->CTLR2 = 0U;
#if defined(MR_USE_CH32V4X7)
        /* Disable the audio interface before selecting SPI mode */
        priv->instance->I2SCFGR &= ~SPI_I2SCFGR_I2SE;
        priv->instance->I2SCFGR &= ~SPI_I2SCFGR_I2SMOD;
#endif /* defined(MR_USE_CH32V4X7) */
        priv->ready = MR_TRUE;
    }

    /* Save SPI configuration */
    priv->configs[cs] = *config;
    priv->configs[cs].clock_rate = rate;
    priv->dividers[cs] = bits;

_exit:
    /* Unlock */
    mr_atomic_store(&priv->busy, 0);
    return ret;
}

static mr_err_t spi_configuration(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_spi_config_t *config) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if (cs >= priv->count) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Get SPI configuration */
    *config = priv->configs[cs];

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t spi_wait(const spi_priv_t *priv, mr_uint16_t bits,
                         mr_uint16_t expected) {
    mr_uint32_t elapsed;
    mr_uint16_t status;

    /* Wait SPI status */
    for (elapsed = 0U; ; elapsed++) {
        status = priv->instance->STATR;
        if ((status & (SPI_STATR_MODF | SPI_STATR_OVR)) != 0U) {
            return -MR_EIO;
        }
        if ((status & bits) == expected) {
            return 0;
        }
        if (elapsed == priv->timeout) {
            return -MR_ETIMEDOUT;
        }
        mr_port_clock_busy_wait(1U);
    }
}

static mr_err_t spi_message(const spi_priv_t *priv, const mr_spi_msg_t *msg,
                             mr_size_t width) {
    mr_size_t count, offset;
    mr_uint16_t data;
    mr_err_t ret;

    /* Transfer one message */
    count = (msg->tx_len > msg->rx_len) ? msg->tx_len : msg->rx_len;
    for (offset = 0U; offset < count; offset += width) {
        ret = spi_wait(priv, SPI_STATR_TXE, SPI_STATR_TXE);
        if (ret != 0) {
            return ret;
        }
        data = 0xffffU;
        if (offset < msg->tx_len) {
            mr_memcpy(&data, (const mr_uint8_t *)msg->tx_buf + offset, width);
        }
        priv->instance->DATAR = data;
        ret = spi_wait(priv, SPI_STATR_RXNE, SPI_STATR_RXNE);
        if (ret != 0) {
            return ret;
        }
        data = priv->instance->DATAR;
        if (offset < msg->rx_len) {
            mr_memcpy((mr_uint8_t *)msg->rx_buf + offset, &data, width);
        }
    }
    return 0;
}

static mr_uint16_t spi_frame_mode(const mr_spi_config_t *config) {
    mr_uint16_t mode;

    /* Translate SPI frame mode */
    mode = config->mode;
    if (config->data_bits == 16U) {
        mode |= SPI_CTLR1_DFF;
    }
    if (config->bit_order == MR_SPI_BIT_ORDER_LSB) {
        mode |= SPI_CTLR1_LSBFIRST;
    }
    return mode;
}

static mr_uint16_t spi_mode(const spi_priv_t *priv, mr_uint32_t cs) {
    /* Build master mode value */
    return SPI_CTLR1_MSTR | SPI_CTLR1_SSM | SPI_CTLR1_SSI | priv->dividers[cs]
           | spi_frame_mode(&priv->configs[cs]);
}

static void spi_discard(const spi_priv_t *priv, mr_uint16_t mode) {
    /* Clear OVR with a data/status read and MODF with a control write */
    MR_IGNORE(priv->instance->DATAR);
    MR_IGNORE(priv->instance->STATR);
    priv->instance->CTLR1 = mode;
}

static mr_ssize_t spi_transfer(const mr_driver_t *drv, mr_uint32_t cs,
                               const mr_spi_msg_t *msgs, mr_size_t num) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_spi_config_t *config;
    mr_size_t width, i;
    mr_err_t ret, stop;
    mr_uint16_t mode;

    /* Check parameter */
    if ((cs >= priv->count) || (num > (mr_size_t)MR_SSIZE_MAX)) {
        return -MR_EINVAL;
    }
    if (priv->slave != MR_NULL) {
        return -MR_ENOSYS;
    }

    /* Lock */
    if (mr_atomic_exchange(&priv->busy, 1)) {
        return -MR_EBUSY;
    }
    config = &priv->configs[cs];

    /* Check configuration */
    if (config->clock_rate == 0U) {
        ret = -MR_ENODEV;
        goto _exit;
    }

    /* Check messages */
    width = config->data_bits / 8U;
    for (i = 0U; i < num; i++) {
        if (((msgs[i].tx_len | msgs[i].rx_len) & (width - 1U))
            || ((msgs[i].tx_len != 0U) && !msgs[i].tx_buf)
            || ((msgs[i].rx_len != 0U) && !msgs[i].rx_buf)) {
            ret = -MR_EINVAL;
            goto _exit;
        }
    }
    ret = 0;
    if (num == 0U) {
        goto _exit;
    }

    /* Enable the selected idle clock polarity before asserting CS */
    mode = spi_mode(priv, cs);
    priv->instance->CTLR1 = mode | SPI_CTLR1_SPE;
    ret = spi_select(&priv->selects[cs], MR_GPIO_LEVEL_LOW);
    if (ret == 0) {
        /* Transfer messages */
        for (i = 0U; i < num; i++) {
            ret = spi_message(priv, &msgs[i], width);
            if (ret != 0) {
                break;
            }
        }
        if (ret == 0) {
            ret = spi_wait(priv, SPI_STATR_TXE | SPI_STATR_BSY, SPI_STATR_TXE);
        }
    }
    if (ret != 0) {
        /* Discard on error */
        spi_discard(priv, mode);
    }

    /* Deselect chip */
    stop = spi_select(&priv->selects[cs], MR_GPIO_LEVEL_HIGH);
    if (ret == 0) {
        ret = stop;
    }

_exit:
    /* Unlock */
    mr_atomic_store(&priv->busy, 0);
    return (ret == 0) ? (mr_ssize_t)num : ret;
}

static mr_bool_t spi_event_ready(mr_driver_t *drv) {
    const mr_spi_data_t *data;

    /* Check event state */
    data = MR_DRIVER_DATA(drv);
    return mr_driver_is_ready(drv) && MR_OBJECT_IS_INITED(&data->event);
}

static void spi_slave_notify(mr_driver_t *drv, mr_uint32_t events) {
    const spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_spi_event_data_t event = {.cs = 0U};

    /* Raise SPI slave events */
    events &= priv->slave->events;
    if (events != 0U) {
        mr_spi_event_raise(drv, events, &event);
    }
}

static mr_uint32_t spi_slave_drain(spi_priv_t *priv, mr_uint16_t status) {
    spi_slave_t *slave;
    mr_uint32_t events;
    mr_uint16_t frame;

    /* Drain received frames */
    slave = priv->slave;
    events = 0U;
    if ((status & SPI_STATR_RXNE) != 0U) {
        frame = priv->instance->DATAR;
        if (mr_fifo_push(&slave->rx, &frame) == 0U) {
            events |= MR_SPI_WAIT_ERROR;
        } else {
            events |= MR_SPI_WAIT_RX_READY;
        }
    }
    if ((status & (SPI_STATR_OVR | SPI_STATR_MODF)) != 0U) {
        MR_IGNORE(priv->instance->DATAR);
        MR_IGNORE(priv->instance->STATR);
        events |= MR_SPI_WAIT_ERROR;
    }
    return events;
}

static void spi_slave_send(spi_priv_t *priv) {
    mr_uint16_t frame;

    /* Send one queued frame */
    if (mr_fifo_pop(&priv->slave->tx, &frame) == 0U) {
        frame = 0xffffU;
    }
    priv->instance->DATAR = frame;
}

static void spi_slave_nss(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t events, const void *data) {
    spi_priv_t *priv;
    spi_slave_t *slave;
    mr_driver_t *drv;
    mr_uint8_t level;
    int mask;

    drv = (mr_driver_t *)param;
    MR_IGNORE(waiter);
    MR_IGNORE(events);
    MR_IGNORE(data);

    priv = MR_DRIVER_PRIV(drv);
    slave = priv->slave;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check slave state */
    if (!slave->enabled || !spi_event_ready(drv)
        || (mr_ht_gpio_read(&priv->selects[0], &level)
            != (mr_ssize_t)sizeof(level))) {
        mr_port_irq_enable(mask);
        return;
    }

    /* Handle NSS level */
    if ((level == MR_GPIO_LEVEL_LOW) && !slave->selected) {
        /* Assert internal software NSS before notifying waiters */
        priv->instance->CTLR1 &= ~SPI_CTLR1_SSI;
        slave->selected = MR_TRUE;
        spi_slave_notify(drv, MR_SPI_WAIT_SELECT | MR_SPI_WAIT_TX_READY);
        if (slave->enabled && slave->selected) {
            if ((priv->instance->STATR & SPI_STATR_TXE) != 0U) {
                spi_slave_send(priv);
            }
            priv->instance->CTLR2 |= SPI_CTLR2_TXEIE;
        }
    } else if ((level == MR_GPIO_LEVEL_HIGH) && slave->selected) {
        /* Deassert internal software NSS to release MISO */
        priv->instance->CTLR1 |= SPI_CTLR1_SSI;
        slave->selected = MR_FALSE;
        priv->instance->CTLR2 &= ~SPI_CTLR2_TXEIE;
        spi_slave_notify(drv, MR_SPI_WAIT_DESELECT
                              | spi_slave_drain(priv, priv->instance->STATR));
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

static mr_err_t spi_slave_nss_release(spi_priv_t *priv) {
    const mr_gpio_config_t gpio = {.mode = MR_GPIO_MODE_INPUT};

    /* Release NSS pin */
    if (!mr_driver_is_ready(priv->selects[0].drv)) {
        return -MR_ENODEV;
    }
    return mr_ht_gpio_configure(&priv->selects[0], &gpio);
}

static mr_err_t spi_slave_configure(const mr_driver_t *drv,
                                    const mr_spi_config_t *config) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_gpio_config_t gpio;
    spi_slave_t *slave;
    mr_uint8_t level;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((config->clock_rate != 0U)
        && ((config->role != MR_SPI_ROLE_SLAVE)
            || ((config->data_bits != 8U) && (config->data_bits != 16U))
            || !mr_ch32_irq_valid(priv->irq))) {
        return -MR_ENOSYS;
    }

    /* Lock */
    if (mr_atomic_exchange(&priv->busy, 1)) {
        return -MR_EBUSY;
    }
    slave = priv->slave;
    mask = mr_port_irq_disable();
    ret = 0;

    /* Check slave state */
    if (slave->selected && ((priv->instance->CTLR1 & SPI_CTLR1_SPE) != 0U)) {
        ret = -MR_EBUSY;
        goto _exit;
    }
    if (slave->enabled) {
        /* Disable slave */
        ret = spi_slave_nss_release(priv);
        if (ret != 0) {
            goto _exit;
        }
        priv->instance->CTLR2 = 0U;
        NVIC_DisableIRQ((IRQn_Type)priv->irq);
        NVIC_ClearPendingIRQ((IRQn_Type)priv->irq);
        spi_discard(priv, slave->mode);
        slave->enabled = MR_FALSE;
        slave->selected = MR_FALSE;
        priv->configs[0].clock_rate = 0U;
    }
    if (config->clock_rate == 0U) {
        /* Save SPI configuration */
        priv->configs[0] = *config;
        goto _exit;
    }

    /* Check NSS pin */
    if (!mr_driver_is_ready(priv->selects[0].drv)) {
        ret = -MR_ENODEV;
        goto _exit;
    }
    /* The board prepares master-facing pins and the hardware NSS input */
    priv->prepare();
    gpio = (mr_gpio_config_t){
        .mode = MR_GPIO_MODE_IRQ_EDGE,
        .pull = MR_GPIO_PULL_NONE,
    };
    ret = mr_ht_gpio_configure(&priv->selects[0], &gpio);
    if (ret != 0) {
        goto _exit;
    }
    if ((mr_ht_gpio_read(&priv->selects[0], &level)
        != (mr_ssize_t)sizeof(level))
        || (level != MR_GPIO_LEVEL_HIGH)) {
        ret = -MR_EBUSY;
        goto _release_nss;
    }

    /* Init NSS event */
    if (!MR_OBJECT_IS_INITED(&slave->nss)) {
        ret = mr_waiter_init(&slave->nss, spi_slave_nss, (void *)drv,
                             MR_GPIO_WAIT_EXTI);
        if (ret != 0) {
            goto _release_nss;
        }
    }
    ret = mr_ht_gpio_event_attach(&priv->selects[0], &slave->nss);
    if (ret != 0) {
        goto _release_nss;
    }
    ret = mr_ht_gpio_event_enable(&priv->selects[0], MR_GPIO_WAIT_EXTI);
    if (ret != 0) {
        goto _release_nss;
    }

#if defined(MR_USE_CH32V4X7)
    /* Disable the audio interface before selecting SPI mode */
    priv->instance->I2SCFGR &= ~SPI_I2SCFGR_I2SE;
    priv->instance->I2SCFGR &= ~SPI_I2SCFGR_I2SMOD;
#endif /* defined(MR_USE_CH32V4X7) */

    /* Enable slave */
    mr_fifo_init(&slave->rx, slave->rx_buffer, slave->rx_size,
                 sizeof(mr_uint16_t));
    mr_fifo_init(&slave->tx, slave->tx_buffer, slave->tx_size,
                 sizeof(mr_uint16_t));
    slave->mode = spi_frame_mode(config) | SPI_CTLR1_SSM;
    priv->configs[0] = *config;
    slave->enabled = MR_TRUE;
    NVIC_ClearPendingIRQ((IRQn_Type)priv->irq);
    NVIC_SetPriority((IRQn_Type)priv->irq, priv->irq_priority);
    NVIC_EnableIRQ((IRQn_Type)priv->irq);
    priv->instance->CTLR1 = slave->mode | SPI_CTLR1_SSI | SPI_CTLR1_SPE;
    priv->instance->CTLR2 = SPI_CTLR2_RXNEIE | SPI_CTLR2_ERRIE;
    goto _exit;

_release_nss:
    /* Release NSS */
    spi_slave_nss_release(priv);
_exit:
    /* Unlock */
    mr_port_irq_enable(mask);
    mr_atomic_store(&priv->busy, 0);
    return ret;
}

static mr_ssize_t spi_slave_io(const mr_driver_t *drv, mr_uint32_t cs,
                                void *rx, const void *tx, mr_size_t size) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_size_t offset, width;
    spi_slave_t *slave;
    mr_uint16_t frame;
    int mask;

    /* Check slave state */
    slave = priv->slave;
    if (slave == MR_NULL) {
        return -MR_ENOSYS;
    }

    /* Check parameter */
    if ((cs != 0U) || (size > (mr_size_t)MR_SSIZE_MAX)) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check slave state */
    if (!slave->enabled || ((priv->instance->CTLR1 & SPI_CTLR1_SPE) == 0U)) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Check buffer alignment */
    width = priv->configs[0].data_bits / 8U;
    if ((size & (width - 1U)) != 0U) {
        mr_port_irq_enable(mask);
        return -MR_EINVAL;
    }

    /* Transfer FIFO data */
    for (offset = 0U; offset < size; offset += width) {
        if (rx != MR_NULL) {
            if (mr_fifo_pop(&slave->rx, &frame) == 0U) {
                break;
            }
            mr_memcpy((mr_uint8_t *)rx + offset, &frame, width);
        } else {
            frame = 0U;
            mr_memcpy(&frame, (const mr_uint8_t *)tx + offset, width);
            if (mr_fifo_push(&slave->tx, &frame) == 0U) {
                break;
            }
        }
    }
    if ((tx != MR_NULL) && (offset != 0U)
        && ((priv->instance->STATR & SPI_STATR_TXE) != 0U)) {
        /* Preload queued data while NSS is high to meet the first clock edge */
        spi_slave_send(priv);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return (mr_ssize_t)offset;
}

static mr_ssize_t spi_slave_read(const mr_driver_t *drv, mr_uint32_t cs,
                                  void *buf, mr_size_t size) {
    return spi_slave_io(drv, cs, buf, MR_NULL, size);
}

static mr_ssize_t spi_slave_write(const mr_driver_t *drv, mr_uint32_t cs,
                                   const void *buf, mr_size_t size) {
    return spi_slave_io(drv, cs, MR_NULL, buf, size);
}

static void spi_slave_irq(mr_driver_t *drv, spi_priv_t *priv) {
    mr_uint32_t events;
    mr_uint16_t status;

    /* Disable slave for an unusable driver */
    if (!spi_event_ready(drv)) {
        priv->instance->CTLR2 = 0U;
        priv->instance->CTLR1 = 0U;
        priv->slave->selected = MR_FALSE;
        return;
    }

    /* Handle slave IRQ */
    status = priv->instance->STATR;
    events = spi_slave_drain(priv, status);
    if ((status & SPI_STATR_MODF) != 0U) {
        priv->instance->CTLR1 = priv->slave->mode
                                | (priv->slave->selected ? 0U : SPI_CTLR1_SSI)
                                | SPI_CTLR1_SPE;
    }
    if (priv->slave->selected && ((status & SPI_STATR_TXE) != 0U)) {
        events |= MR_SPI_WAIT_TX_READY;
    }

    /* Raise SPI event */
    spi_slave_notify(drv, events);
    if (priv->slave->enabled && priv->slave->selected
        && ((priv->instance->STATR & SPI_STATR_TXE) != 0U)) {
        spi_slave_send(priv);
    }
}

#if defined(SPI_USE_STREAM)
static void spi_stream_send(const spi_priv_t *priv,
                            const spi_stream_t *stream) {
    mr_uint16_t frame;

    /* Send one stream frame */
    frame = 0xffffU;
    if (stream->tx_offset < stream->msg.tx_len) {
        mr_memcpy(&frame, (const mr_uint8_t *)stream->msg.tx_buf
                          + stream->tx_offset, stream->width);
    }
    priv->instance->DATAR = frame;
}

static mr_err_t spi_stream_release(spi_priv_t *priv) {
    spi_stream_t *stream;
    mr_err_t ret;

    /* Release stream */
    stream = &priv->stream;
    priv->instance->CTLR2 = 0U;
    NVIC_DisableIRQ((IRQn_Type)priv->irq);
    NVIC_ClearPendingIRQ((IRQn_Type)priv->irq);
    spi_discard(priv, stream->mode);
    ret = spi_select(&priv->selects[stream->cs], MR_GPIO_LEVEL_HIGH);
    stream->active = MR_FALSE;
    mr_atomic_store(&priv->busy, 0);
    return ret;
}

static mr_err_t spi_stream_start(const mr_driver_t *drv, mr_uint32_t cs,
                                  const mr_spi_msg_t *msg,
                                  const mr_spi_stream_config_t *config) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    spi_stream_t *stream;
    mr_size_t width, span;
    mr_uint16_t events;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (cs >= priv->count) {
        return -MR_EINVAL;
    }

    /* Check stream support */
    if (!mr_ch32_irq_valid(priv->irq) || (priv->slave != MR_NULL)) {
        return -MR_ENOSYS;
    }

    /* Lock */
    if (mr_atomic_exchange(&priv->busy, 1)) {
        return -MR_EBUSY;
    }

    /* Check configuration */
    if (priv->configs[cs].clock_rate == 0U) {
        ret = -MR_ENODEV;
        goto _exit;
    }

    /* Check messages */
    width = priv->configs[cs].data_bits / 8U;
    span = (msg->tx_len > msg->rx_len) ? msg->tx_len : msg->rx_len;
    events = priv->events[cs];
    if ((span == 0U) || (((msg->tx_len | msg->rx_len) & (width - 1U)) != 0U)
        || ((msg->tx_len != 0U) && !msg->tx_buf)
        || ((msg->rx_len != 0U) && !msg->rx_buf)
        || (((events & MR_SPI_WAIT_TX_STREAM_HALF) != 0U)
            && ((msg->tx_len / width % 2U) != 0U))
        || (((events & MR_SPI_WAIT_RX_STREAM_HALF) != 0U)
            && ((msg->rx_len / width % 2U) != 0U))) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Select chip */
    stream = &priv->stream;
    stream->mode = spi_mode(priv, cs);
    priv->instance->CTLR1 = stream->mode | SPI_CTLR1_SPE;
    ret = spi_select(&priv->selects[cs], MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        spi_discard(priv, stream->mode);
        goto _exit;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Init stream state */
    stream->msg = *msg;
    stream->cs = cs;
    stream->generation++;
    stream->width = width;
    stream->frames_left = span / width;
    stream->tx_offset = 0U;
    stream->rx_offset = 0U;
    stream->circular = config->circular;
    stream->active = MR_TRUE;
    NVIC_ClearPendingIRQ((IRQn_Type)priv->irq);
    NVIC_SetPriority((IRQn_Type)priv->irq, priv->irq_priority);
    NVIC_EnableIRQ((IRQn_Type)priv->irq);
    priv->instance->CTLR2 = SPI_CTLR2_RXNEIE | SPI_CTLR2_ERRIE;
    spi_stream_send(priv, stream);

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;

_exit:
    /* Unlock */
    mr_atomic_store(&priv->busy, 0);
    return ret;
}

static mr_err_t spi_stream_stop(const mr_driver_t *drv) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret, rel;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
    if (!priv->stream.active) {
        mr_port_irq_enable(mask);
        return 0;
    }

    /* Stop further IRQ-driven frames before waiting for the in-flight shift */
    priv->instance->CTLR2 = 0U;
    NVIC_DisableIRQ((IRQn_Type)priv->irq);
    NVIC_ClearPendingIRQ((IRQn_Type)priv->irq);
    mr_port_irq_enable(mask);

    /* One frame can remain in flight because TX advances from RXNE */
    ret = spi_wait(priv, SPI_STATR_TXE | SPI_STATR_BSY, SPI_STATR_TXE);

    /* Always release CS and busy state even if the bus timed out */
    mask = mr_port_irq_disable();
    rel = spi_stream_release(priv);
    mr_port_irq_enable(mask);
    return (ret == -MR_ETIMEDOUT) ? ret : rel;
}

static mr_err_t spi_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get stream state */
    mask = mr_port_irq_disable();
    *busy = priv->stream.active;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t spi_rx_stream_offset(const mr_driver_t *drv,
                                     mr_size_t *offset) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get stream offset */
    mask = mr_port_irq_disable();
    *offset = priv->stream.rx_offset;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t spi_tx_stream_offset(const mr_driver_t *drv,
                                     mr_size_t *offset) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get stream offset */
    mask = mr_port_irq_disable();
    *offset = priv->stream.tx_offset;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t spi_stream_events(const mr_driver_t *drv, mr_uint32_t cs,
                                   mr_uint32_t events, mr_bool_t enable) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if ((cs >= priv->count) || (events == 0U)) {
        return -MR_EINVAL;
    }

    /* Check stream support */
    if (!mr_ch32_irq_valid(priv->irq)
        || ((events & ~SPI_STREAM_EVENTS) != 0U)) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
    if (enable && priv->stream.active && (priv->stream.cs == cs)) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Update event mask */
    if (enable) {
        priv->events[cs] |= events;
    } else {
        priv->events[cs] &= ~events;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_uint32_t spi_stream_advance(spi_stream_t *stream, mr_uint16_t frame) {
    mr_uint32_t events;

    /* Advance stream offsets */
    events = 0U;
    if (stream->rx_offset < stream->msg.rx_len) {
        mr_memcpy((mr_uint8_t *)stream->msg.rx_buf + stream->rx_offset,
                  &frame, stream->width);
        stream->rx_offset += stream->width;
        if (stream->rx_offset == stream->msg.rx_len) {
            events |= MR_SPI_WAIT_RX_STREAM_FULL;
            if (stream->circular) {
                stream->rx_offset = 0U;
            }
        } else if (stream->rx_offset
                   == stream->msg.rx_len - stream->rx_offset) {
            events |= MR_SPI_WAIT_RX_STREAM_HALF;
        }
    }
    if (stream->tx_offset < stream->msg.tx_len) {
        stream->tx_offset += stream->width;
        if (stream->tx_offset == stream->msg.tx_len) {
            events |= MR_SPI_WAIT_TX_STREAM_FULL;
            if (stream->circular) {
                stream->tx_offset = 0U;
            }
        } else if (stream->tx_offset
                   == stream->msg.tx_len - stream->tx_offset) {
            events |= MR_SPI_WAIT_TX_STREAM_HALF;
        }
    }
    return events;
}

static void spi_stream_irq(mr_driver_t *drv) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t events, generation;
    mr_spi_event_data_t event;
    spi_stream_t *stream;
    mr_bool_t send_next;
    mr_uint16_t status;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();
    stream = &priv->stream;

    /* Check stream state */
    if (!stream->active) {
        mr_port_irq_enable(mask);
        return;
    }

    /* Release stream for an unusable driver */
    if (!spi_event_ready(drv)) {
        spi_stream_release(priv);
        mr_port_irq_enable(mask);
        return;
    }

    /* Handle stream IRQ */
    event.cs = stream->cs;
    generation = stream->generation;
    status = priv->instance->STATR;
    events = 0U;
    send_next = MR_FALSE;
    if ((status & (SPI_STATR_OVR | SPI_STATR_MODF)) != 0U) {
        spi_stream_release(priv);
        events = MR_SPI_WAIT_STREAM_ERROR;
    } else if ((status & SPI_STATR_RXNE) != 0U) {
        events = spi_stream_advance(stream, priv->instance->DATAR);
        if (stream->circular || (--stream->frames_left != 0U)) {
            send_next = MR_TRUE;
        } else {
            /* RXNE accounts for the final shift, including dummy frames.
             * BSY can lag the last RXNE by a few peripheral clocks */
            ret = -MR_ETIMEDOUT;
            for (status = 0U; status < SPI_FINAL_POLLS; status++) {
                if ((priv->instance->STATR & (SPI_STATR_TXE | SPI_STATR_BSY))
                    == SPI_STATR_TXE) {
                    ret = 0;
                    break;
                }
            }
            if (spi_stream_release(priv) != 0) {
                ret = -MR_EIO;
            }
            events |= (ret == 0) ? MR_SPI_WAIT_STREAM_DONE
                                 : MR_SPI_WAIT_STREAM_ERROR;
        }
    }

    /* Raise SPI event */
    events &= priv->events[event.cs];
    if (events != 0U) {
        mr_spi_event_raise(drv, events, &event);
    }
    if (send_next && stream->active && (stream->generation == generation)) {
        if (spi_event_ready(drv)) {
            spi_stream_send(priv, stream);
        } else {
            spi_stream_release(priv);
        }
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}
#endif /* defined(SPI_USE_STREAM) */

static mr_err_t spi_event_control(const mr_driver_t *drv, mr_uint32_t cs,
                                   mr_uint32_t events, mr_bool_t enable) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    spi_slave_t *slave;
    int mask;

    /* Check event source */
    slave = priv->slave;
    if (slave == MR_NULL) {
#if defined(SPI_USE_STREAM)
        return spi_stream_events(drv, cs, events, enable);
#else
        return -MR_ENOSYS;
#endif /* defined(SPI_USE_STREAM) */
    }

    /* Check parameter */
    if ((cs != 0U) || (events == 0U)) {
        return -MR_EINVAL;
    }
    if ((events & ~SPI_SLAVE_EVENTS) != 0U) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Update event mask */
    if (enable) {
        slave->events |= events;
    } else {
        slave->events &= ~events;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

MR_UNUSED static void spi_irq(mr_driver_t *drv) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Dispatch SPI IRQ */
    if (priv->slave != MR_NULL) {
        mask = mr_port_irq_disable();
        spi_slave_irq(drv, priv);
        mr_port_irq_enable(mask);
        return;
    }
#if defined(SPI_USE_STREAM)
    spi_stream_irq(drv);
#endif /* defined(SPI_USE_STREAM) */
}

/* Instance install */
#define __CH32_HT_SPI_SLAVE_RX_FIFO(_id)                                       \
    MR_HT_DRIVER_PROP_OR(_id, wch_slave_rx_fifo, SPI_SLAVE_FIFO_FRAMES)
#define __CH32_HT_SPI_SLAVE_TX_FIFO(_id)                                       \
    MR_HT_DRIVER_PROP_OR(_id, wch_slave_tx_fifo, SPI_SLAVE_FIFO_FRAMES)
#define __CH32_HT_SPI_SLAVE_DEFINE(_id)                                        \
    MR_MACRO_IF(                                                               \
        MR_HT_DRIVER_HAS_PROP(_id, wch_slave),                                 \
        (static mr_uint16_t MR_CAT(__spi_slave_rx_, _id)                       \
             [__CH32_HT_SPI_SLAVE_RX_FIFO(_id)];                               \
         static mr_uint16_t MR_CAT(__spi_slave_tx_, _id)                       \
             [__CH32_HT_SPI_SLAVE_TX_FIFO(_id)];                               \
         static spi_slave_t MR_CAT(__spi_slave_, _id) = {                      \
             .rx_buffer = MR_CAT(__spi_slave_rx_, _id),                        \
             .tx_buffer = MR_CAT(__spi_slave_tx_, _id),                        \
             .rx_size = sizeof(MR_CAT(__spi_slave_rx_, _id)),                  \
             .tx_size = sizeof(MR_CAT(__spi_slave_tx_, _id)),                  \
         };),                                                                  \
        ())
#define CH32_HT_SPI_INSTALL(_id, _ops)                                         \
    extern void MR_HT_DRIVER_PROP(_id, wch_prepare)(void);                     \
    static mr_ht_gpio_spec_t MR_CAT(__spi_cs_, _id)[] = {                      \
        MR_HT_GPIO_SPEC_FOREACH(_id, cs),                                      \
    };                                                                         \
    __CH32_HT_SPI_SLAVE_DEFINE(_id)                                            \
    static mr_spi_config_t MR_CAT(__spi_configs_, _id)                         \
        [MR_HT_DRIVER_PROP_ELEM_NUM(_id, cs)];                                 \
    static mr_uint16_t MR_CAT(__spi_dividers_, _id)                            \
        [MR_HT_DRIVER_PROP_ELEM_NUM(_id, cs)];                                 \
    __CH32_HT_SPI_STREAM_DEFINE(_id)                                           \
    static spi_priv_t MR_CAT(__spi_priv_, _id) = {                             \
        .instance = MR_HT_DRIVER_PROP(_id, wch_spix),                          \
        .prepare = MR_HT_DRIVER_PROP(_id, wch_prepare),                        \
        .selects = MR_CAT(__spi_cs_, _id),                                     \
        .configs = MR_CAT(__spi_configs_, _id),                                \
        .dividers = MR_CAT(__spi_dividers_, _id),                              \
        .count = MR_HT_DRIVER_PROP_ELEM_NUM(_id, cs),                          \
        .timeout = MR_HT_DRIVER_PROP_OR(_id, wch_timeout_us, 1000U),           \
        .busy = MR_ATOMIC_INIT(0),                                             \
        .slave = MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_slave),            \
                             (&MR_CAT(__spi_slave_, _id)), (MR_NULL)),         \
        .irq = MR_HT_DRIVER_PROP_OR(_id, wch_irqn, SPI_NO_IRQ),                \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U),       \
        __CH32_HT_SPI_STREAM_INIT(_id)                                         \
    };                                                                         \
    __CH32_HT_SPI_IRQ(_id)                                                     \
    MR_HT_SPI_INSTALL(_id, &MR_CAT(__spi_priv_, _id), _ops)
#if defined(SPI_USE_STREAM)
#define __CH32_HT_SPI_STREAM_DEFINE(_id)                                       \
    static mr_uint16_t MR_CAT(__spi_events_, _id)                              \
        [MR_HT_DRIVER_PROP_ELEM_NUM(_id, cs)];
#define __CH32_HT_SPI_STREAM_INIT(_id)                                         \
    .events = MR_CAT(__spi_events_, _id),
#else
#define __CH32_HT_SPI_STREAM_DEFINE(_id)
#define __CH32_HT_SPI_STREAM_INIT(_id)
#endif /* defined(SPI_USE_STREAM) */
#define __CH32_HT_SPI_IRQ(_id)                                                 \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_irq_handler),                   \
                (MR_CH32_INTERRUPT void                                        \
                     MR_HT_DRIVER_PROP(_id, wch_irq_handler)(void) {           \
                     spi_irq(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));            \
                 }),                                                           \
                ())
MR_HT_DRIVER_FOREACH(CH32_HT_SPI_INSTALL, &__spi_ops);

#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_SPI) */

/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First generic STM32 SPI version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/libc/mr_string.h>
#include <mrX/port/mr_port_irq.h>
#if defined(MR_USE_DRIVER_SPI_STREAM) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#define SPI_USE_STREAM
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) && defined(MR_USE_DRIVER_DMA) */

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_spi
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "STM32 SPI requires the GPIO driver"
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

/* SPI IP capability definition */
#if defined(SPI_CFG1_DSIZE) || defined(SPI_CFG1_DSIZE_Msk)
#define STM32_SPI_V2
#endif /* defined(SPI_CFG1_DSIZE) || defined(SPI_CFG1_DSIZE_Msk) */

/* SPI register macros */
#if defined(STM32_SPI_V2)
#define SPI_RX_DATA(_priv)              ((_priv)->handle.Instance->RXDR)
#define SPI_TX_DATA(_priv)              ((_priv)->handle.Instance->TXDR)
#else
#define SPI_RX_DATA(_priv)              ((_priv)->handle.Instance->DR)
#define SPI_TX_DATA(_priv)              ((_priv)->handle.Instance->DR)
#endif /* defined(STM32_SPI_V2) */

/* SPI definition */
#define SPI_DEFAULT_TIMEOUT_MS          (100U)
#define SPI_DUMMY_CHUNK_SIZE            (16U)
#define SPI_CS_NONE                     (0xFFFFFFFFU)
#define SPI_DEFAULT_SLAVE_FIFO_FRAMES   (16U)

#define SPI_SLAVE_EVENT_MASK                                                   \
    (MR_SPI_WAIT_SELECT | MR_SPI_WAIT_RX_READY | MR_SPI_WAIT_TX_READY          \
     | MR_SPI_WAIT_DESELECT | MR_SPI_WAIT_ERROR)
#if defined(SPI_USE_STREAM)
#define SPI_STREAM_EVENT_MASK                                                  \
    (MR_SPI_WAIT_RX_STREAM_HALF | MR_SPI_WAIT_RX_STREAM_FULL                   \
     | MR_SPI_WAIT_TX_STREAM_HALF | MR_SPI_WAIT_TX_STREAM_FULL                 \
     | MR_SPI_WAIT_STREAM_ERROR | MR_SPI_WAIT_STREAM_DONE)
#define SPI_DMA_EVENT_MASK                                                     \
    (MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR)
#define SPI_EVENT_MASK (SPI_SLAVE_EVENT_MASK | SPI_STREAM_EVENT_MASK)
#else
#define SPI_EVENT_MASK SPI_SLAVE_EVENT_MASK
#endif /* defined(SPI_USE_STREAM) */

/* SPI pin type */
typedef struct spi_pin {
    mr_ht_gpio_spec_t spec;
    uint32_t alternate;
} spi_pin_t;

/* SPI chip-select slot type */
typedef struct spi_cs_slot {
    mr_ht_gpio_spec_t gpio;
    mr_spi_config_t config;
    uint32_t prescaler;
    mr_uint8_t configured: 1;
} spi_cs_slot_t;

/* SPI slave FIFO type */
typedef struct spi_slave_fifo {
    mr_uint16_t *data;
    mr_size_t cap;
    mr_size_t head;
    mr_size_t tail;
    mr_size_t count;
} spi_slave_fifo_t;

/* SPI slave state type */
typedef struct spi_slave {
    mr_waiter_t nss_waiter;
    spi_slave_fifo_t rx_fifo;
    spi_slave_fifo_t tx_fifo;
    mr_spi_config_t config;
    mr_uint8_t active: 1;
    mr_uint8_t selected: 1;
    mr_uint8_t nss_attached: 1;
} spi_slave_t;

#if defined(SPI_USE_STREAM)
/* SPI stream type */
typedef struct spi_stream {
    mr_ht_dma_spec_t rx_dma;
    mr_ht_dma_spec_t tx_dma;
    mr_dma_transfer_t rx_transfer;
    mr_dma_transfer_t tx_transfer;
    mr_waiter_t rx_waiter;
    mr_waiter_t tx_waiter;
    mr_uint32_t cs;
    mr_uint16_t dummy_tx;
    mr_uint16_t dummy_rx;
    volatile mr_bool_t rx_done;
    volatile mr_bool_t tx_done;
    volatile mr_bool_t complete;
    volatile mr_bool_t active;
    mr_uint8_t use_rx: 1;
    mr_uint8_t use_tx: 1;
    mr_uint8_t circular: 1;
} spi_stream_t;
#endif /* defined(SPI_USE_STREAM) */

/* SPI private data type */
typedef struct spi_priv {
    SPI_HandleTypeDef handle;
    void (*clk_enable)(void);
    spi_pin_t sck;
    spi_pin_t mosi;
    spi_pin_t miso;
    spi_cs_slot_t *cs_slots;
    mr_size_t cs_num;
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    mr_uint32_t timeout_ms;
    mr_uint32_t active_cs;
    mr_uint32_t event_mask;
    mr_driver_t *driver;
    spi_slave_t slave;
    mr_uint8_t irq_valid: 1;
    mr_uint8_t pins_inited: 1;
    mr_uint8_t hw_inited: 1;
#if defined(SPI_USE_STREAM)
    spi_stream_t stream;
#endif /* defined(SPI_USE_STREAM) */
} spi_priv_t;

/* SPI prescaler map */
typedef struct spi_prescaler {
    uint32_t value;
    mr_uint32_t divider;
} spi_prescaler_t;

static const spi_prescaler_t __spi_prescalers[] = {
    {SPI_BAUDRATEPRESCALER_2, 2U},     {SPI_BAUDRATEPRESCALER_4, 4U},
    {SPI_BAUDRATEPRESCALER_8, 8U},     {SPI_BAUDRATEPRESCALER_16, 16U},
    {SPI_BAUDRATEPRESCALER_32, 32U},   {SPI_BAUDRATEPRESCALER_64, 64U},
    {SPI_BAUDRATEPRESCALER_128, 128U}, {SPI_BAUDRATEPRESCALER_256, 256U},
};

static const mr_uint8_t __spi_dummy_tx[SPI_DUMMY_CHUNK_SIZE] = {
    0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
    0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
};

static mr_bool_t spi_fifo_push(spi_slave_fifo_t *fifo, mr_uint16_t value) {
    if ((fifo->data == MR_NULL) || (fifo->cap == 0U)
        || (fifo->count >= fifo->cap)) {
        return MR_FALSE;
    }
    fifo->data[fifo->head] = value;
    fifo->head = (fifo->head + 1U) % fifo->cap;
    fifo->count++;
    return MR_TRUE;
}

static mr_bool_t spi_fifo_pop(spi_slave_fifo_t *fifo, mr_uint16_t *value) {
    if ((fifo->data == MR_NULL) || (fifo->cap == 0U) || (fifo->count == 0U)) {
        return MR_FALSE;
    }
    *value = fifo->data[fifo->tail];
    fifo->tail = (fifo->tail + 1U) % fifo->cap;
    fifo->count--;
    return MR_TRUE;
}

static void spi_fifo_reset(spi_slave_fifo_t *fifo) {
    fifo->head = 0U;
    fifo->tail = 0U;
    fifo->count = 0U;
}

static void spi_event_notify(const spi_priv_t *priv, mr_uint32_t cs,
                             mr_uint32_t events) {
    mr_spi_event_data_t data;

    events &= priv->event_mask;
    if ((events != 0U) && (priv->driver != MR_NULL)) {
        data.cs = cs;
        mr_spi_event_raise(priv->driver, events, &data);
    }
}

#if defined(RCC_CLOCKTYPE_PCLK2)
static mr_bool_t spi_is_apb2(const SPI_TypeDef *instance) {
#if defined(SPI1)
    if (instance == SPI1) {
        return MR_TRUE;
    }
#endif /* defined(SPI1) */
#if defined(SPI4)
    if (instance == SPI4) {
        return MR_TRUE;
    }
#endif /* defined(SPI4) */
#if defined(SPI5)
    if (instance == SPI5) {
        return MR_TRUE;
    }
#endif /* defined(SPI5) */
#if defined(SPI6)
    if (instance == SPI6) {
        return MR_TRUE;
    }
#endif /* defined(SPI6) */
    return MR_FALSE;
}
#endif /* defined(RCC_CLOCKTYPE_PCLK2) */

static mr_uint32_t spi_clock_freq(const SPI_TypeDef *instance) {
#if defined(RCC_CLOCKTYPE_PCLK2)
    if (spi_is_apb2(instance)) {
        return HAL_RCC_GetPCLK2Freq();
    }
#else
    MR_IGNORE(instance);
#endif /* defined(RCC_CLOCKTYPE_PCLK2) */
    return HAL_RCC_GetPCLK1Freq();
}

static mr_err_t spi_select_prescaler(const SPI_TypeDef *instance,
                                     mr_uint32_t target_rate,
                                     uint32_t *prescaler,
                                     mr_uint32_t *actual_rate) {
    mr_uint32_t clk, rate;
    mr_size_t i;

    /* Check target rate */
    if (target_rate == 0U) {
        return -MR_EINVAL;
    }
    clk = spi_clock_freq(instance);
    if (clk == 0U) {
        return -MR_ERANGE;
    }

    /* Pick fastest divider not exceeding target clock rate */
    for (i = 0U; i < MR_ARRAY_SIZE(__spi_prescalers); i++) {
        rate = clk / __spi_prescalers[i].divider;
        if (rate <= target_rate) {
            *prescaler = __spi_prescalers[i].value;
            *actual_rate = rate;
            return 0;
        }
    }
    return -MR_ERANGE;
}

static mr_err_t spi_pin_configure_opt(const spi_pin_t *pin,
                                      mr_bool_t required) {
    if (pin->spec.drv == MR_NULL) {
        return required ? -MR_ENODEV : 0;
    }
    return mr_stm32_gpio_pin_alternate(&pin->spec, pin->alternate,
                                       MR_GPIO_PULL_NONE, MR_GPIO_OTYPE_PP);
}

static mr_err_t spi_bus_pins_init(spi_priv_t *priv) {
    mr_err_t ret;

    /* Configure SCK, MOSI, MISO alternate functions */
    ret = spi_pin_configure_opt(&priv->sck, MR_TRUE);
    if (ret != 0) {
        return ret;
    }
    ret = spi_pin_configure_opt(&priv->mosi, MR_FALSE);
    if (ret != 0) {
        return ret;
    }
    return spi_pin_configure_opt(&priv->miso, MR_FALSE);
}

static mr_err_t spi_pins_init(spi_priv_t *priv) {
    mr_gpio_config_t cs_cfg;
    mr_size_t i;
    mr_err_t ret;

    if (priv->pins_inited) {
        return 0;
    }

    ret = spi_bus_pins_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Configure software CS pins high (deselected) */
    cs_cfg = (mr_gpio_config_t){0};
    cs_cfg.mode = MR_GPIO_MODE_OUTPUT;
    cs_cfg.otype = MR_GPIO_OTYPE_PP;
    cs_cfg.pull = MR_GPIO_PULL_NONE;
    cs_cfg.level = 1U;
    for (i = 0U; i < priv->cs_num; i++) {
        if (priv->cs_slots[i].gpio.drv == MR_NULL) {
            return -MR_ENODEV;
        }
        ret = mr_gpio_configure(priv->cs_slots[i].gpio.drv,
                                priv->cs_slots[i].gpio.pin, &cs_cfg);
        if (ret != 0) {
            return ret;
        }
    }

    priv->pins_inited = 1U;
    return 0;
}

static void spi_cs_set(const spi_priv_t *priv, mr_uint32_t cs,
                       mr_bool_t select) {
    const mr_ht_gpio_spec_t *gpio = &priv->cs_slots[cs].gpio;

    /* Active-low chip select */
    mr_gpio_write(gpio->drv, gpio->pin, select ? 0U : 1U);
}

static void spi_slave_hw_select(spi_priv_t *priv, mr_bool_t select) {
    /* Control internal software NSS (SSI: 0=selected, 1=deselected) */
#if defined(SPI_CR1_SSI)
    if (select) {
        CLEAR_BIT(priv->handle.Instance->CR1, SPI_CR1_SSI);
    } else {
        SET_BIT(priv->handle.Instance->CR1, SPI_CR1_SSI);
    }
#else
    MR_IGNORE(priv);
    MR_IGNORE(select);
#endif /* defined(SPI_CR1_SSI) */
}

static void spi_slave_tx_it_control(spi_priv_t *priv, mr_bool_t enable) {
#if defined(STM32_SPI_V2)
    if (enable) {
        __HAL_SPI_ENABLE_IT(&priv->handle, SPI_IT_TXP);
    } else {
        __HAL_SPI_DISABLE_IT(&priv->handle, SPI_IT_TXP);
    }
#else
    if (enable) {
        __HAL_SPI_ENABLE_IT(&priv->handle, SPI_IT_TXE);
    } else {
        __HAL_SPI_DISABLE_IT(&priv->handle, SPI_IT_TXE);
    }
#endif /* defined(STM32_SPI_V2) */
}

static void spi_slave_rx_err_it_control(spi_priv_t *priv, mr_bool_t enable) {
#if defined(STM32_SPI_V2)
    uint32_t it = SPI_IT_RXP | SPI_IT_OVR | SPI_IT_UDR | SPI_IT_FRE;
#else
    uint32_t it = SPI_IT_RXNE | SPI_IT_ERR;
#endif /* defined(STM32_SPI_V2) */

    if (enable) {
        __HAL_SPI_ENABLE_IT(&priv->handle, it);
    } else {
        __HAL_SPI_DISABLE_IT(&priv->handle, it);
    }
}

static mr_uint16_t spi_slave_hw_read(const spi_priv_t *priv) {
    if (priv->slave.config.data_bits == 16U) {
        return *(volatile mr_uint16_t *)&SPI_RX_DATA(priv);
    }
    return *(volatile mr_uint8_t *)&SPI_RX_DATA(priv);
}

static void spi_slave_hw_write(const spi_priv_t *priv, mr_uint16_t value) {
    if (priv->slave.config.data_bits == 16U) {
        *(volatile mr_uint16_t *)&SPI_TX_DATA(priv) = value;
    } else {
        *(volatile mr_uint8_t *)&SPI_TX_DATA(priv) = (mr_uint8_t)value;
    }
}

static mr_bool_t spi_slave_hw_rx_ready(const spi_priv_t *priv) {
#if defined(STM32_SPI_V2)
    return (__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_RXP) != 0U);
#else
    return (__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_RXNE) != 0U);
#endif /* defined(STM32_SPI_V2) */
}

static mr_bool_t spi_slave_hw_tx_ready(const spi_priv_t *priv) {
#if defined(STM32_SPI_V2)
    return (__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_TXP) != 0U);
#else
    return (__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_TXE) != 0U);
#endif /* defined(STM32_SPI_V2) */
}

static mr_uint32_t spi_slave_drain_rx(spi_priv_t *priv) {
    mr_uint32_t events = 0U;
    mr_uint16_t frame;

    while (spi_slave_hw_rx_ready(priv)) {
        frame = spi_slave_hw_read(priv);
        if (spi_fifo_push(&priv->slave.rx_fifo, frame)) {
            events |= MR_SPI_WAIT_RX_READY;
        } else {
            events |= MR_SPI_WAIT_ERROR;
        }
    }
    return events;
}

static mr_uint32_t spi_slave_feed_tx(spi_priv_t *priv) {
    mr_uint32_t events = 0U;
    mr_uint16_t frame;

    while (spi_slave_hw_tx_ready(priv)) {
        if (!spi_fifo_pop(&priv->slave.tx_fifo, &frame)) {
            spi_slave_tx_it_control(priv, MR_FALSE);
            events |= MR_SPI_WAIT_TX_READY;
            break;
        }
        spi_slave_hw_write(priv, frame);
        if (priv->slave.tx_fifo.count < priv->slave.tx_fifo.cap) {
            events |= MR_SPI_WAIT_TX_READY;
        }
    }
    return events;
}

static void spi_slave_nss_event(mr_waiter_t *waiter, void *param,
                                mr_wait_event_t event, const void *data) {
    spi_priv_t *priv = (spi_priv_t *)param;
    mr_uint32_t spi_events = 0U;
    mr_uint8_t level = 1U;
    int mask;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);

    if (!priv->slave.active) {
        return;
    }

    mr_gpio_read(priv->cs_slots[0].gpio.drv, priv->cs_slots[0].gpio.pin,
                 &level);

    mask = mr_port_irq_disable();
    if (level == 0U) {
        if (!priv->slave.selected) {
            priv->slave.selected = 1U;
            spi_slave_hw_select(priv, MR_TRUE);
            if (priv->slave.tx_fifo.count != 0U) {
                spi_slave_feed_tx(priv);
                if (priv->slave.tx_fifo.count != 0U) {
                    spi_slave_tx_it_control(priv, MR_TRUE);
                }
            } else {
                /* Preload all-ones when TX FIFO is empty */
                if (spi_slave_hw_tx_ready(priv)) {
                    spi_slave_hw_write(priv, 0xFFFFU);
                }
            }
            spi_events = MR_SPI_WAIT_SELECT | MR_SPI_WAIT_TX_READY;
        }
    } else {
        if (priv->slave.selected) {
            spi_events = spi_slave_drain_rx(priv);
            spi_slave_tx_it_control(priv, MR_FALSE);
            spi_slave_hw_select(priv, MR_FALSE);
            priv->slave.selected = 0U;
            spi_events |= MR_SPI_WAIT_DESELECT;
        }
    }
    mr_port_irq_enable(mask);

    if (spi_events != 0U) {
        spi_event_notify(priv, 0U, spi_events);
    }
}

static void spi_slave_disable(spi_priv_t *priv) {
    mr_gpio_config_t cfg;
    int mask;

    if (!priv->slave.active && !priv->slave.nss_attached) {
        return;
    }

    mask = mr_port_irq_disable();
    priv->slave.active = 0U;
    priv->slave.selected = 0U;
    if (priv->hw_inited) {
        spi_slave_tx_it_control(priv, MR_FALSE);
        spi_slave_rx_err_it_control(priv, MR_FALSE);
        spi_slave_hw_select(priv, MR_FALSE);
        __HAL_SPI_DISABLE(&priv->handle);
    }
    if (priv->irq_valid) {
        HAL_NVIC_DisableIRQ(priv->irqn);
    }
    spi_fifo_reset(&priv->slave.rx_fifo);
    spi_fifo_reset(&priv->slave.tx_fifo);
    mr_port_irq_enable(mask);

    if (priv->slave.nss_attached) {
        mr_gpio_event_disable(priv->cs_slots[0].gpio.drv,
                              priv->cs_slots[0].gpio.pin, MR_GPIO_WAIT_EXTI);
        mr_waiter_del(&priv->slave.nss_waiter);
        cfg = (mr_gpio_config_t){0};
        cfg.mode = MR_GPIO_MODE_INPUT;
        cfg.pull = MR_GPIO_PULL_UP;
        mr_gpio_configure(priv->cs_slots[0].gpio.drv,
                          priv->cs_slots[0].gpio.pin, &cfg);
        priv->slave.nss_attached = 0U;
    }
    if (priv->hw_inited) {
        HAL_SPI_DeInit(&priv->handle);
        priv->hw_inited = 0U;
    }
    priv->pins_inited = 0U;
}

static mr_err_t spi_slave_configure(const mr_driver_t *drv,
                                    const mr_spi_config_t *config) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    SPI_InitTypeDef init = {0};
    mr_gpio_config_t nss_cfg;
    mr_uint8_t level = 1U;
    mr_err_t ret;

    /* Exit slave mode when clock_rate == 0 */
    if (config->clock_rate == 0U) {
        spi_slave_disable(priv);
        priv->slave.config = *config;
        return 0;
    }

    if (!priv->irq_valid || (priv->cs_num == 0U)
        || (priv->cs_slots[0].gpio.drv == MR_NULL)) {
        return -MR_ENODEV;
    }

    /* Reset previous slave state */
    spi_slave_disable(priv);

    /* Configure SCK/MOSI/MISO pins */
    ret = spi_bus_pins_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Configure CS[0] as dual-edge EXTI input for slave NSS tracking */
    nss_cfg = (mr_gpio_config_t){0};
    nss_cfg.mode = MR_GPIO_MODE_IRQ_EDGE;
    nss_cfg.pull = MR_GPIO_PULL_UP;
    ret = mr_gpio_configure(priv->cs_slots[0].gpio.drv,
                            priv->cs_slots[0].gpio.pin, &nss_cfg);
    if (ret != 0) {
        return ret;
    }

    /* Enable SPI clock and init in slave software-NSS mode */
    priv->clk_enable();
    init.Mode = SPI_MODE_SLAVE;
    init.Direction = SPI_DIRECTION_2LINES;
    init.DataSize
        = (config->data_bits == 16U) ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
    init.CLKPolarity = ((config->mode == MR_SPI_MODE_2)
                        || (config->mode == MR_SPI_MODE_3))
                         ? SPI_POLARITY_HIGH
                         : SPI_POLARITY_LOW;
    init.CLKPhase = ((config->mode == MR_SPI_MODE_1)
                     || (config->mode == MR_SPI_MODE_3))
                      ? SPI_PHASE_2EDGE
                      : SPI_PHASE_1EDGE;
    init.NSS = SPI_NSS_SOFT;
    init.FirstBit = (config->bit_order == MR_SPI_BIT_ORDER_LSB)
                      ? SPI_FIRSTBIT_LSB
                      : SPI_FIRSTBIT_MSB;
    init.TIMode = SPI_TIMODE_DISABLE;
    init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
#if defined(SPI_FIFO_THRESHOLD_01DATA)
    init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
#endif /* defined(SPI_FIFO_THRESHOLD_01DATA) */

    priv->handle.Init = init;
    ret = mr_stm32_hal_status(HAL_SPI_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }
    priv->hw_inited = 1U;
    priv->active_cs = SPI_CS_NONE;
    priv->driver = (mr_driver_t *)drv;
    priv->slave.config = *config;
    spi_fifo_reset(&priv->slave.rx_fifo);
    spi_fifo_reset(&priv->slave.tx_fifo);

    /* Attach EXTI waiter on CS[0] */
    ret = mr_waiter_init(&priv->slave.nss_waiter, spi_slave_nss_event, priv,
                         MR_GPIO_WAIT_EXTI);
    if (ret != 0) {
        spi_slave_disable(priv);
        return ret;
    }
    ret = mr_gpio_event_attach(priv->cs_slots[0].gpio.drv,
                               priv->cs_slots[0].gpio.pin,
                               &priv->slave.nss_waiter);
    if (ret != 0) {
        mr_waiter_del(&priv->slave.nss_waiter);
        spi_slave_disable(priv);
        return ret;
    }
    priv->slave.nss_attached = 1U;

    /* Sync initial NSS level and enable interrupts */
    mr_gpio_read(priv->cs_slots[0].gpio.drv, priv->cs_slots[0].gpio.pin,
                 &level);
    priv->slave.selected = (level == 0U) ? 1U : 0U;
    spi_slave_hw_select(priv, priv->slave.selected != 0U);
    priv->slave.active = 1U;

    HAL_NVIC_SetPriority(priv->irqn, priv->irq_priority, 0U);
    HAL_NVIC_ClearPendingIRQ(priv->irqn);
    HAL_NVIC_EnableIRQ(priv->irqn);
    spi_slave_rx_err_it_control(priv, MR_TRUE);
    __HAL_SPI_ENABLE(&priv->handle);
    ret = mr_gpio_event_enable(priv->cs_slots[0].gpio.drv,
                               priv->cs_slots[0].gpio.pin, MR_GPIO_WAIT_EXTI);
    if (ret != 0) {
        spi_slave_disable(priv);
        return ret;
    }
    return 0;
}

static mr_err_t spi_apply_slot(spi_priv_t *priv, mr_uint32_t cs) {
    const spi_cs_slot_t *slot = &priv->cs_slots[cs];
    SPI_InitTypeDef init = {0};
    mr_err_t ret;

    /* Reuse active hardware configuration when CS slot is unchanged */
    if (priv->hw_inited && (priv->active_cs == cs)) {
        return 0;
    }

    /* Enable SPI clock */
    priv->clk_enable();

    /* Build HAL SPI init */
    init.Mode = SPI_MODE_MASTER;
    init.Direction = SPI_DIRECTION_2LINES;
    init.DataSize = (slot->config.data_bits == 16U) ? SPI_DATASIZE_16BIT
                                                    : SPI_DATASIZE_8BIT;
    init.CLKPolarity = ((slot->config.mode == MR_SPI_MODE_2)
                        || (slot->config.mode == MR_SPI_MODE_3))
                         ? SPI_POLARITY_HIGH
                         : SPI_POLARITY_LOW;
    init.CLKPhase = ((slot->config.mode == MR_SPI_MODE_1)
                     || (slot->config.mode == MR_SPI_MODE_3))
                      ? SPI_PHASE_2EDGE
                      : SPI_PHASE_1EDGE;
    init.NSS = SPI_NSS_SOFT;
    init.BaudRatePrescaler = slot->prescaler;
    init.FirstBit = (slot->config.bit_order == MR_SPI_BIT_ORDER_LSB)
                      ? SPI_FIRSTBIT_LSB
                      : SPI_FIRSTBIT_MSB;
    init.TIMode = SPI_TIMODE_DISABLE;
    init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
#if defined(SPI_NSS_PULSE_DISABLE)
    init.NSSPMode = SPI_NSS_PULSE_DISABLE;
#endif /* defined(SPI_NSS_PULSE_DISABLE) */
#if defined(SPI_FIFO_THRESHOLD_01DATA)
    init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
#endif /* defined(SPI_FIFO_THRESHOLD_01DATA) */
#if defined(SPI_MASTER_KEEP_IO_STATE_ENABLE)
    init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
#endif /* defined(SPI_MASTER_KEEP_IO_STATE_ENABLE) */

    /* Reinitialize SPI peripheral */
    if (priv->hw_inited) {
        HAL_SPI_DeInit(&priv->handle);
        priv->hw_inited = 0U;
    }
    priv->handle.Init = init;
    ret = mr_stm32_hal_status(HAL_SPI_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }

    priv->active_cs = cs;
    priv->hw_inited = 1U;
    return 0;
}

static mr_err_t spi_configure(const mr_driver_t *drv, mr_uint32_t cs,
                              const mr_spi_config_t *config) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    spi_cs_slot_t *slot;
    mr_uint32_t actual_rate;
    uint32_t prescaler;
    mr_err_t ret;

    /* Check controller resources */
    if ((priv->handle.Instance == MR_NULL) || (priv->clk_enable == MR_NULL)
        || (priv->cs_slots == MR_NULL)) {
        return -MR_ENODEV;
    }
    if (cs >= priv->cs_num) {
        return -MR_EINVAL;
    }

#if defined(SPI_USE_STREAM)
    /* Reject reconfiguration while stream is active */
    if (priv->stream.active) {
        return -MR_EBUSY;
    }
#endif /* defined(SPI_USE_STREAM) */

    /* Check SPI parameters */
    if ((config->data_bits != 8U) && (config->data_bits != 16U)) {
        return -MR_ENOSYS;
    }
    if ((config->mode > MR_SPI_MODE_3)
        || (config->bit_order > MR_SPI_BIT_ORDER_LSB)
        || (config->role > MR_SPI_ROLE_SLAVE)) {
        return -MR_EINVAL;
    }

    /* Handle SPI slave configuration */
    if (config->role == MR_SPI_ROLE_SLAVE) {
        if (cs != 0U) {
            return -MR_EINVAL;
        }
        return spi_slave_configure(drv, config);
    }

    /* Leave slave mode if switching back to master */
    if (priv->slave.active || priv->slave.nss_attached) {
        spi_slave_disable(priv);
    }

    /* Resolve prescaler */
    ret = spi_select_prescaler(priv->handle.Instance, config->clock_rate,
                               &prescaler, &actual_rate);
    if (ret != 0) {
        return ret;
    }

    /* Configure bus and CS pins */
    ret = spi_pins_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Save CS slot configuration */
    slot = &priv->cs_slots[cs];
    slot->config = *config;
    slot->config.clock_rate = actual_rate;
    slot->prescaler = prescaler;
    slot->configured = 1U;
    priv->driver = (mr_driver_t *)drv;

    /* Force hardware reapply on next transfer if active slot changed */
    if (priv->active_cs == cs) {
        priv->active_cs = SPI_CS_NONE;
    }
    return spi_apply_slot(priv, cs);
}

static mr_err_t spi_configuration(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_spi_config_t *config) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);

    if (cs >= priv->cs_num) {
        return -MR_EINVAL;
    }
    if (priv->slave.active) {
        if (cs != 0U) {
            return -MR_EINVAL;
        }
        *config = priv->slave.config;
        return 0;
    }
    if (!priv->cs_slots[cs].configured) {
        return -MR_ENODEV;
    }

    *config = priv->cs_slots[cs].config;
    return 0;
}

static mr_err_t spi_transfer_msg(spi_priv_t *priv, const mr_spi_msg_t *msg,
                                 mr_size_t frame_bytes) {
    const mr_uint8_t *tx = (const mr_uint8_t *)msg->tx_buf;
    mr_uint8_t *rx = (mr_uint8_t *)msg->rx_buf;
    mr_size_t tx_len = (tx != MR_NULL) ? msg->tx_len : 0U;
    mr_size_t rx_len = (rx != MR_NULL) ? msg->rx_len : 0U;
    mr_size_t common, remain, chunk, items;
    mr_err_t ret;

    /* Validate frame alignment */
    if (((tx_len % frame_bytes) != 0U) || ((rx_len % frame_bytes) != 0U)) {
        return -MR_EINVAL;
    }
    if ((frame_bytes > 1U)
        && ((((mr_uintptr_t)tx % frame_bytes) != 0U)
            || (((mr_uintptr_t)rx % frame_bytes) != 0U))) {
        return -MR_EINVAL;
    }

    /* Full-duplex overlap */
    common = (tx_len < rx_len) ? tx_len : rx_len;
    while (common != 0U) {
        chunk = (common > (0xFFFFU * frame_bytes)) ? (0xFFFFU * frame_bytes)
                                                   : common;
        items = chunk / frame_bytes;
        ret = mr_stm32_hal_status(HAL_SPI_TransmitReceive(
            &priv->handle, (uint8_t *)tx, rx, (uint16_t)items,
            priv->timeout_ms));
        if (ret != 0) {
            return ret;
        }
        tx += chunk;
        rx += chunk;
        tx_len -= chunk;
        rx_len -= chunk;
        common -= chunk;
    }

    /* Remaining TX-only bytes */
    while (tx_len != 0U) {
        chunk = (tx_len > (0xFFFFU * frame_bytes)) ? (0xFFFFU * frame_bytes)
                                                   : tx_len;
        items = chunk / frame_bytes;
        ret = mr_stm32_hal_status(HAL_SPI_Transmit(
            &priv->handle, (uint8_t *)tx, (uint16_t)items, priv->timeout_ms));
        if (ret != 0) {
            return ret;
        }
        tx += chunk;
        tx_len -= chunk;
    }

    /* Remaining RX-only bytes: clock out all-ones frames per mr_spi contract */
    remain = rx_len;
    while (remain != 0U) {
        chunk = (remain > SPI_DUMMY_CHUNK_SIZE) ? SPI_DUMMY_CHUNK_SIZE : remain;
        items = chunk / frame_bytes;
        ret = mr_stm32_hal_status(HAL_SPI_TransmitReceive(
            &priv->handle, (uint8_t *)__spi_dummy_tx, rx, (uint16_t)items,
            priv->timeout_ms));
        if (ret != 0) {
            return ret;
        }
        rx += chunk;
        remain -= chunk;
    }
    return 0;
}

static mr_ssize_t spi_transfer(const mr_driver_t *drv, mr_uint32_t cs,
                               const mr_spi_msg_t *msgs, mr_size_t num) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_size_t frame_bytes, i;
    mr_err_t ret;

    if (priv->slave.active) {
        return -MR_EPERM;
    }
    if (cs >= priv->cs_num) {
        return -MR_EINVAL;
    }
    if (!priv->cs_slots[cs].configured) {
        return -MR_ENODEV;
    }
#if defined(SPI_USE_STREAM)
    if (priv->stream.active) {
        return -MR_EBUSY;
    }
#endif /* defined(SPI_USE_STREAM) */

    /* Apply CS hardware configuration */
    ret = spi_apply_slot(priv, cs);
    if (ret != 0) {
        return ret;
    }

    frame_bytes = (priv->cs_slots[cs].config.data_bits == 16U) ? 2U : 1U;

    /* Hold CS active across the entire message array */
    spi_cs_set(priv, cs, MR_TRUE);
    for (i = 0U; i < num; i++) {
        ret = spi_transfer_msg(priv, &msgs[i], frame_bytes);
        if (ret != 0) {
            spi_cs_set(priv, cs, MR_FALSE);
            return (i != 0U) ? (mr_ssize_t)i : ret;
        }
    }
    spi_cs_set(priv, cs, MR_FALSE);
    return (mr_ssize_t)num;
}

static mr_ssize_t spi_slave_read(const mr_driver_t *drv, mr_uint32_t cs,
                                 void *buf, mr_size_t size) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_size_t frame_bytes, completed;
    mr_uint8_t *dst = (mr_uint8_t *)buf;
    mr_uint16_t frame;
    int mask;

    if ((cs != 0U) || !priv->slave.active) {
        return -MR_ENODEV;
    }
    frame_bytes = (priv->slave.config.data_bits == 16U) ? 2U : 1U;
    if (((size % frame_bytes) != 0U)
        || ((frame_bytes > 1U) && (((mr_uintptr_t)dst % frame_bytes) != 0U))) {
        return -MR_EINVAL;
    }

    completed = 0U;
    mask = mr_port_irq_disable();
    while ((completed < size) && spi_fifo_pop(&priv->slave.rx_fifo, &frame)) {
        if (frame_bytes == 2U) {
            *(mr_uint16_t *)(dst + completed) = frame;
        } else {
            dst[completed] = (mr_uint8_t)frame;
        }
        completed += frame_bytes;
    }
    mr_port_irq_enable(mask);
    return (mr_ssize_t)completed;
}

static mr_ssize_t spi_slave_write(const mr_driver_t *drv, mr_uint32_t cs,
                                  const void *buf, mr_size_t size) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_size_t frame_bytes, completed;
    const mr_uint8_t *src = (const mr_uint8_t *)buf;
    mr_uint16_t frame;
    int mask;

    if ((cs != 0U) || !priv->slave.active) {
        return -MR_ENODEV;
    }
    frame_bytes = (priv->slave.config.data_bits == 16U) ? 2U : 1U;
    if (((size % frame_bytes) != 0U)
        || ((frame_bytes > 1U) && (((mr_uintptr_t)src % frame_bytes) != 0U))) {
        return -MR_EINVAL;
    }

    completed = 0U;
    mask = mr_port_irq_disable();
    while (completed < size) {
        frame = (frame_bytes == 2U) ? *(const mr_uint16_t *)(src + completed)
                                    : (mr_uint16_t)src[completed];
        if (!spi_fifo_push(&priv->slave.tx_fifo, frame)) {
            break;
        }
        completed += frame_bytes;
    }
    if ((completed != 0U) && priv->slave.selected) {
        spi_slave_feed_tx(priv);
        if (priv->slave.tx_fifo.count != 0U) {
            spi_slave_tx_it_control(priv, MR_TRUE);
        }
    }
    mr_port_irq_enable(mask);
    return (mr_ssize_t)completed;
}

#if defined(SPI_USE_STREAM)
static void spi_dma_req_control(spi_priv_t *priv, mr_bool_t rx, mr_bool_t tx,
                                mr_bool_t enable) {
#if defined(STM32_SPI_V2)
    uint32_t mask = 0U;

    if (rx) {
        mask |= SPI_CFG1_RXDMAEN;
    }
    if (tx) {
        mask |= SPI_CFG1_TXDMAEN;
    }
    if (enable) {
        SET_BIT(priv->handle.Instance->CFG1, mask);
    } else {
        CLEAR_BIT(priv->handle.Instance->CFG1, mask);
    }
#else
    uint32_t mask = 0U;

    if (rx) {
        mask |= SPI_CR2_RXDMAEN;
    }
    if (tx) {
        mask |= SPI_CR2_TXDMAEN;
    }
    if (enable) {
        SET_BIT(priv->handle.Instance->CR2, mask);
    } else {
        CLEAR_BIT(priv->handle.Instance->CR2, mask);
    }
#endif /* defined(STM32_SPI_V2) */
}

static void spi_stream_dma_event(spi_priv_t *priv, mr_wait_event_t event,
                                 mr_bool_t is_rx) {
    spi_stream_t *stream = &priv->stream;
    mr_uint32_t spi_event = 0U;

    if (!stream->active || stream->complete) {
        return;
    }

    if ((event & MR_DMA_WAIT_ERROR) != 0U) {
        spi_dma_req_control(priv, MR_TRUE, MR_TRUE, MR_FALSE);
        stream->complete = MR_TRUE;
        spi_cs_set(priv, stream->cs, MR_FALSE);
        spi_event_notify(priv, stream->cs, MR_SPI_WAIT_STREAM_ERROR);
        return;
    }

    if ((event & MR_DMA_WAIT_HALF) != 0U) {
        spi_event |= is_rx ? MR_SPI_WAIT_RX_STREAM_HALF
                           : MR_SPI_WAIT_TX_STREAM_HALF;
    }
    if ((event & MR_DMA_WAIT_DONE) != 0U) {
        spi_event |= is_rx ? MR_SPI_WAIT_RX_STREAM_FULL
                           : MR_SPI_WAIT_TX_STREAM_FULL;
        if (!stream->circular) {
            if (is_rx) {
                stream->rx_done = MR_TRUE;
            } else {
                stream->tx_done = MR_TRUE;
            }
            if ((!stream->use_rx || stream->rx_done)
                && (!stream->use_tx || stream->tx_done)) {
                spi_dma_req_control(priv, MR_TRUE, MR_TRUE, MR_FALSE);
                stream->complete = MR_TRUE;
                spi_cs_set(priv, stream->cs, MR_FALSE);
                spi_event |= MR_SPI_WAIT_STREAM_DONE;
            }
        }
    }
    if (spi_event != 0U) {
        spi_event_notify(priv, stream->cs, spi_event);
    }
}

static void spi_rx_dma_event(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(data);
    spi_stream_dma_event((spi_priv_t *)param, event, MR_TRUE);
}

static void spi_tx_dma_event(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(data);
    spi_stream_dma_event((spi_priv_t *)param, event, MR_FALSE);
}

static mr_err_t spi_stream_stop(const mr_driver_t *drv) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    spi_stream_t *stream = &priv->stream;

    if (!stream->active) {
        return 0;
    }

    /* Disable SPI DMA requests and peripheral */
    spi_dma_req_control(priv, MR_TRUE, MR_TRUE, MR_FALSE);
    __HAL_SPI_DISABLE(&priv->handle);

    /* Stop RX DMA */
    if (stream->use_rx) {
        mr_ht_dma_stop(&stream->rx_dma);
        mr_ht_dma_event_disable(&stream->rx_dma, SPI_DMA_EVENT_MASK);
        mr_waiter_del(&stream->rx_waiter);
    }

    /* Stop TX DMA */
    if (stream->use_tx) {
        mr_ht_dma_stop(&stream->tx_dma);
        mr_ht_dma_event_disable(&stream->tx_dma, SPI_DMA_EVENT_MASK);
        mr_waiter_del(&stream->tx_waiter);
    }

    /* Release CS and reset stream state */
    spi_cs_set(priv, stream->cs, MR_FALSE);
    stream->active = MR_FALSE;
    stream->complete = MR_FALSE;
    stream->rx_done = MR_FALSE;
    stream->tx_done = MR_FALSE;
    stream->use_rx = 0U;
    stream->use_tx = 0U;
    return 0;
}

static mr_err_t spi_stream_start(const mr_driver_t *drv, mr_uint32_t cs,
                                 const mr_spi_msg_t *msg,
                                 const mr_spi_stream_config_t *config) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    spi_stream_t *stream = &priv->stream;
    mr_dma_config_t dma_cfg;
    mr_size_t frame_bytes, total_bytes, count;
    mr_uint32_t width;
    mr_bool_t has_tx, has_rx;
    mr_err_t ret;

    if (priv->slave.active) {
        return -MR_EPERM;
    }
    if (cs >= priv->cs_num) {
        return -MR_EINVAL;
    }
    if (!priv->cs_slots[cs].configured) {
        return -MR_ENODEV;
    }

    /* Reclaim completed oneshot stream if needed */
    if (stream->active) {
        if (!stream->complete) {
            return -MR_EBUSY;
        }
        ret = spi_stream_stop(drv);
        if (ret != 0) {
            return ret;
        }
    }

    has_tx = (msg->tx_buf != MR_NULL) && (msg->tx_len != 0U);
    has_rx = (msg->rx_buf != MR_NULL) && (msg->rx_len != 0U);
    if (!has_tx && !has_rx) {
        return -MR_EINVAL;
    }
    if (has_tx && has_rx && (msg->tx_len != msg->rx_len)) {
        return -MR_EINVAL;
    }

    frame_bytes = (priv->cs_slots[cs].config.data_bits == 16U) ? 2U : 1U;
    total_bytes = has_tx ? msg->tx_len : msg->rx_len;
    if ((total_bytes % frame_bytes) != 0U) {
        return -MR_EINVAL;
    }
    count = total_bytes / frame_bytes;
    width = (frame_bytes == 2U) ? MR_DMA_WIDTH_16 : MR_DMA_WIDTH_8;

    /* Check DMA resources: master always needs TX DMA to generate SCK */
    if (stream->tx_dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    if (has_rx && (stream->rx_dma.drv == MR_NULL)) {
        return -MR_ENODEV;
    }

    /* Apply CS hardware configuration */
    ret = spi_apply_slot(priv, cs);
    if (ret != 0) {
        return ret;
    }

    /* Init stream state */
    stream->cs = cs;
    stream->circular = (config->circular != MR_FALSE);
    stream->dummy_tx = 0xFFFFU;
    stream->dummy_rx = 0U;
    stream->rx_done = MR_FALSE;
    stream->tx_done = MR_FALSE;
    stream->complete = MR_FALSE;
    stream->use_rx = has_rx ? 1U : 0U;
    stream->use_tx = 1U;

    /* Configure and start RX DMA first when receiving */
    if (has_rx) {
        dma_cfg = (mr_dma_config_t){
            .src_width = width,
            .dst_width = width,
            .src_adj = MR_DMA_ADJ_CONST,
            .dst_adj = MR_DMA_ADJ_INC,
            .src_burst = MR_DMA_BURST_1,
            .dst_burst = MR_DMA_BURST_1,
            .direction = MR_DMA_DIR_P2M,
        };
        ret = mr_ht_dma_configure(&stream->rx_dma, &dma_cfg);
        if (ret != 0) {
            return ret;
        }
        ret = mr_waiter_init(&stream->rx_waiter, spi_rx_dma_event, priv,
                             SPI_DMA_EVENT_MASK);
        if (ret != 0) {
            return ret;
        }
        ret = mr_ht_dma_event_attach(&stream->rx_dma, &stream->rx_waiter);
        if (ret != 0) {
            mr_waiter_del(&stream->rx_waiter);
            return ret;
        }
        ret = mr_ht_dma_event_enable(&stream->rx_dma, SPI_DMA_EVENT_MASK);
        if (ret != 0) {
            mr_waiter_del(&stream->rx_waiter);
            return ret;
        }
        stream->rx_transfer.next
            = stream->circular ? &stream->rx_transfer : MR_NULL;
        stream->rx_transfer.src = (const void *)&SPI_RX_DATA(priv);
        stream->rx_transfer.dst = msg->rx_buf;
        stream->rx_transfer.count = count;
        ret = mr_ht_dma_start(&stream->rx_dma, &stream->rx_transfer);
        if (ret != 0) {
            mr_ht_dma_event_disable(&stream->rx_dma, SPI_DMA_EVENT_MASK);
            mr_waiter_del(&stream->rx_waiter);
            return ret;
        }
    }

    /* Configure and start TX DMA (uses constant 0xFFFF for RX-only streams) */
    dma_cfg = (mr_dma_config_t){
        .src_width = width,
        .dst_width = width,
        .src_adj = has_tx ? MR_DMA_ADJ_INC : MR_DMA_ADJ_CONST,
        .dst_adj = MR_DMA_ADJ_CONST,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_M2P,
    };
    ret = mr_ht_dma_configure(&stream->tx_dma, &dma_cfg);
    if (ret != 0) {
        goto _err_rx;
    }
    ret = mr_waiter_init(&stream->tx_waiter, spi_tx_dma_event, priv,
                         SPI_DMA_EVENT_MASK);
    if (ret != 0) {
        goto _err_rx;
    }
    ret = mr_ht_dma_event_attach(&stream->tx_dma, &stream->tx_waiter);
    if (ret != 0) {
        mr_waiter_del(&stream->tx_waiter);
        goto _err_rx;
    }
    ret = mr_ht_dma_event_enable(&stream->tx_dma, SPI_DMA_EVENT_MASK);
    if (ret != 0) {
        mr_waiter_del(&stream->tx_waiter);
        goto _err_rx;
    }
    stream->tx_transfer.next
        = stream->circular ? &stream->tx_transfer : MR_NULL;
    stream->tx_transfer.src
        = has_tx ? msg->tx_buf : (const void *)&stream->dummy_tx;
    stream->tx_transfer.dst = (void *)&SPI_TX_DATA(priv);
    stream->tx_transfer.count = count;
    ret = mr_ht_dma_start(&stream->tx_dma, &stream->tx_transfer);
    if (ret != 0) {
        mr_ht_dma_event_disable(&stream->tx_dma, SPI_DMA_EVENT_MASK);
        mr_waiter_del(&stream->tx_waiter);
        goto _err_rx;
    }

    /* Assert CS and enable SPI DMA requests */
    stream->active = MR_TRUE;
    spi_cs_set(priv, cs, MR_TRUE);
    spi_dma_req_control(priv, has_rx, MR_TRUE, MR_TRUE);
#if defined(STM32_SPI_V2) && defined(SPI_CR2_TSIZE)
    CLEAR_BIT(priv->handle.Instance->CR2, SPI_CR2_TSIZE);
#endif /* defined(STM32_SPI_V2) && defined(SPI_CR2_TSIZE) */
    __HAL_SPI_ENABLE(&priv->handle);
#if defined(STM32_SPI_V2)
    SET_BIT(priv->handle.Instance->CR1, SPI_CR1_CSTART);
#endif /* defined(STM32_SPI_V2) */
    return 0;

_err_rx:
    if (has_rx) {
        mr_ht_dma_stop(&stream->rx_dma);
        mr_ht_dma_event_disable(&stream->rx_dma, SPI_DMA_EVENT_MASK);
        mr_waiter_del(&stream->rx_waiter);
    }
    stream->use_rx = 0U;
    stream->use_tx = 0U;
    return ret;
}

static mr_err_t spi_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    spi_stream_t *stream = &priv->stream;

    if (!stream->active) {
        *busy = MR_FALSE;
        return 0;
    }
    if (stream->complete) {
        *busy = MR_FALSE;
        return spi_stream_stop(drv);
    }
    *busy = MR_TRUE;
    return 0;
}

static mr_err_t spi_rx_stream_offset(const mr_driver_t *drv,
                                     mr_size_t *offset) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);

    if (priv->stream.rx_dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    return mr_ht_dma_offset(&priv->stream.rx_dma, offset);
}

static mr_err_t spi_tx_stream_offset(const mr_driver_t *drv,
                                     mr_size_t *offset) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);

    if (priv->stream.tx_dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    return mr_ht_dma_offset(&priv->stream.tx_dma, offset);
}
#endif /* defined(SPI_USE_STREAM) */

static mr_err_t spi_event_control(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_uint32_t events, mr_bool_t enable) {
    spi_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    if (cs >= priv->cs_num) {
        return -MR_EINVAL;
    }
    if ((events == 0U) || ((events & ~SPI_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }

    mask = mr_port_irq_disable();
    if (enable) {
        priv->event_mask |= events;
    } else {
        priv->event_mask &= ~events;
    }
    mr_port_irq_enable(mask);
    return 0;
}

MR_UNUSED static void spi_irq_handle(spi_priv_t *priv) {
    mr_uint32_t events = 0U;

    if (!priv->slave.active) {
        return;
    }

    /* Handle SPI slave errors */
#if defined(STM32_SPI_V2)
    if ((__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_OVR) != 0U)
        || (__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_UDR) != 0U)
        || (__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_FRE) != 0U)) {
        __HAL_SPI_CLEAR_OVRFLAG(&priv->handle);
        __HAL_SPI_CLEAR_UDRFLAG(&priv->handle);
        __HAL_SPI_CLEAR_FREFLAG(&priv->handle);
        events |= MR_SPI_WAIT_ERROR;
    }
#else
    if ((__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_OVR) != 0U)
        || (__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_MODF) != 0U)) {
        __HAL_SPI_CLEAR_OVRFLAG(&priv->handle);
        __HAL_SPI_CLEAR_MODFFLAG(&priv->handle);
        events |= MR_SPI_WAIT_ERROR;
    }
#if defined(SPI_FLAG_FRE) && defined(__HAL_SPI_CLEAR_FREFLAG)
    if (__HAL_SPI_GET_FLAG(&priv->handle, SPI_FLAG_FRE) != 0U) {
        __HAL_SPI_CLEAR_FREFLAG(&priv->handle);
        events |= MR_SPI_WAIT_ERROR;
    }
#endif /* defined(SPI_FLAG_FRE) && defined(__HAL_SPI_CLEAR_FREFLAG) */
#endif /* defined(STM32_SPI_V2) */

    /* Drain received frames */
    events |= spi_slave_drain_rx(priv);

    /* Refill transmit register when selected */
    if (priv->slave.selected) {
        events |= spi_slave_feed_tx(priv);
    }

    if (events != 0U) {
        spi_event_notify(priv, 0U, events);
    }
}

/* Instance install */
#define __ST_HT_SPI_PIN(_id, _prop, _af_prop)                                  \
    {                                                                          \
        .spec = MR_HT_GPIO_SPEC_OR(_id, _prop, {0}),                           \
        .alternate = MR_HT_DRIVER_PROP_OR(_id, _af_prop, 0U),                  \
    }
#define __ST_HT_SPI_CS_IMPL(_elem, _node, _prop)                               \
    {                                                                          \
        .gpio = {                                                              \
            .drv = MR_HT_DRIVER_FIND(                                          \
                MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),                 \
            .pin = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),   \
        },                                                                     \
    }
#define __ST_HT_SPI_HAS_IRQ(_id)                                               \
    MR_MACRO_AND(MR_HT_DRIVER_HAS_PROP(_id, st_irqn),                          \
                 MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler))
#define __ST_HT_SPI_IRQ(_id)                                                   \
    MR_MACRO_IF(__ST_HT_SPI_HAS_IRQ(_id),                                      \
                (void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {           \
                    spi_irq_handle(&MR_CAT(__spi_priv_, _id));                 \
                }),                                                            \
                ())
#if defined(SPI_USE_STREAM)
#define __ST_HT_SPI_STREAM_INIT(_id)                                           \
    .stream = {                                                                \
        .rx_dma = MR_HT_DMA_SPEC_OR(_id, st_rx_dma, {0}),                      \
        .tx_dma = MR_HT_DMA_SPEC_OR(_id, st_tx_dma, {0}),                      \
    },
#else
#define __ST_HT_SPI_STREAM_INIT(_id)
#endif /* defined(SPI_USE_STREAM) */
#define __ST_HT_SPI_SLAVE_RX_CAP(_id)                                          \
    MR_HT_DRIVER_PROP_OR(_id, st_slave_rx_fifo, SPI_DEFAULT_SLAVE_FIFO_FRAMES)
#define __ST_HT_SPI_SLAVE_TX_CAP(_id)                                          \
    MR_HT_DRIVER_PROP_OR(_id, st_slave_tx_fifo, SPI_DEFAULT_SLAVE_FIFO_FRAMES)
#define __ST_HT_SPI_SLAVE_FIFO_DEFINE(_id)                                     \
    MR_MACRO_IF(__ST_HT_SPI_HAS_IRQ(_id),                                      \
                (static mr_uint16_t MR_CAT(                                    \
                     __spi_slave_rx_buf_, _id)[__ST_HT_SPI_SLAVE_RX_CAP(_id)]; \
                 static mr_uint16_t MR_CAT(                                    \
                     __spi_slave_tx_buf_,                                      \
                     _id)[__ST_HT_SPI_SLAVE_TX_CAP(_id)];),                    \
                ())
#define __ST_HT_SPI_SLAVE_INIT(_id)                                            \
    MR_MACRO_IF(__ST_HT_SPI_HAS_IRQ(_id),                                      \
                (.slave =                                                      \
                     {                                                         \
                         .rx_fifo =                                            \
                             {                                                 \
                                 .data = MR_CAT(__spi_slave_rx_buf_, _id),     \
                                 .cap = __ST_HT_SPI_SLAVE_RX_CAP(_id),         \
                             },                                                \
                         .tx_fifo =                                            \
                             {                                                 \
                                 .data = MR_CAT(__spi_slave_tx_buf_, _id),     \
                                 .cap = __ST_HT_SPI_SLAVE_TX_CAP(_id),         \
                             },                                                \
                     },),                                                      \
                ())
#define ST_HT_SPI_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__spi_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static spi_cs_slot_t MR_CAT(__spi_cs_, _id)[] = {                          \
        MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_cs, __ST_HT_SPI_CS_IMPL),       \
    };                                                                         \
    __ST_HT_SPI_SLAVE_FIFO_DEFINE(_id)                                         \
    static spi_priv_t MR_CAT(__spi_priv_, _id) = {                             \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_spix),                       \
        },                                                                     \
        .clk_enable = MR_CAT(__spi_clk_enable_, _id),                          \
        .sck = __ST_HT_SPI_PIN(_id, st_sck, st_sck_af),                        \
        .mosi = __ST_HT_SPI_PIN(_id, st_mosi, st_mosi_af),                     \
        .miso = __ST_HT_SPI_PIN(_id, st_miso, st_miso_af),                     \
        .cs_slots = MR_CAT(__spi_cs_, _id),                                    \
        .cs_num = MR_HT_DRIVER_PROP_ELEM_NUM(_id, st_cs),                      \
        .irqn = MR_HT_DRIVER_PROP_OR(_id, st_irqn, (IRQn_Type)0),              \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, st_irq_priority, 0U),        \
        .irq_valid = __ST_HT_SPI_HAS_IRQ(_id),                                 \
        .timeout_ms =                                                          \
            MR_HT_DRIVER_PROP_OR(_id, st_timeout_ms, SPI_DEFAULT_TIMEOUT_MS),  \
        .active_cs = SPI_CS_NONE,                                              \
        __ST_HT_SPI_SLAVE_INIT(_id)                                            \
        __ST_HT_SPI_STREAM_INIT(_id)                                           \
    };                                                                         \
    __ST_HT_SPI_IRQ(_id)                                                       \
    MR_HT_SPI_INSTALL(_id, &MR_CAT(__spi_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_SPI_INSTALL, &__spi_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_SPI) */

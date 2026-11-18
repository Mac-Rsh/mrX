/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_UART)
#include <mrX/drivers/uart/mr_uart.h>
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/port/mr_port_irq.h>
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#define UART_USE_STREAM
#endif /* defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA) */

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_uart
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "CH32 UART requires the GPIO driver"
#endif /* !defined(MR_USE_DRIVER_GPIO) */

/* Driver UART operations */
static mr_err_t uart_configure(const mr_driver_t *drv,
                               const mr_uart_config_t *config);
static mr_err_t uart_configuration(const mr_driver_t *drv,
                                   mr_uart_config_t *config);
static mr_ssize_t uart_read(const mr_driver_t *drv, void *buf, mr_size_t size);
static mr_ssize_t uart_write(const mr_driver_t *drv, const void *buf,
                             mr_size_t size);
#if defined(UART_USE_STREAM)
static mr_err_t uart_rx_stream_start(const mr_driver_t *drv, void *buf,
                                     mr_size_t size,
                                     const mr_uart_rx_stream_config_t *config);
static mr_err_t uart_rx_stream_stop(const mr_driver_t *drv);
static mr_err_t uart_rx_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t uart_rx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset);
static mr_err_t uart_tx_stream_start(const mr_driver_t *drv, const void *buf,
                                     mr_size_t size,
                                     const mr_uart_tx_stream_config_t *config);
static mr_err_t uart_tx_stream_stop(const mr_driver_t *drv);
static mr_err_t uart_tx_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t uart_tx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset);
#endif /* defined(UART_USE_STREAM) */
static mr_err_t uart_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable);
MR_UNUSED static const mr_uart_ops_t __uart_ops = {
    .configure = uart_configure,
    .configuration = uart_configuration,
    .read = uart_read,
    .write = uart_write,
#if defined(UART_USE_STREAM)
    .rx_stream_start = uart_rx_stream_start,
    .rx_stream_stop = uart_rx_stream_stop,
    .rx_stream_is_busy = uart_rx_stream_is_busy,
    .rx_stream_offset = uart_rx_stream_offset,
    .tx_stream_start = uart_tx_stream_start,
    .tx_stream_stop = uart_tx_stream_stop,
    .tx_stream_is_busy = uart_tx_stream_is_busy,
    .tx_stream_offset = uart_tx_stream_offset,
#endif /* defined(UART_USE_STREAM) */
    .event_control = uart_event_control,
};

/* UART pin type */
typedef enum uart_pin {
    UART_PIN_RX,
    UART_PIN_TX,
    UART_PIN_RTS,
    UART_PIN_CTS,
    UART_PIN_COUNT,
} uart_pin_t;

#if defined(UART_USE_STREAM)
/* UART DMA stream type */
typedef struct uart_dma_stream {
    mr_ht_dma_spec_t dma;
    mr_dma_transfer_t transfer;
    mr_waiter_t waiter;
    mr_driver_t *driver;
    mr_bool_t active, complete, draining;
} uart_dma_stream_t;
#endif /* defined(UART_USE_STREAM) */

/* UART IRQ definition */
#define UART_NO_IRQ                     (-1)

/* UART event mask definition */
#define UART_RX_EVENTS                                                         \
    (MR_UART_WAIT_RX_READY | MR_UART_WAIT_RX_IDLE | MR_UART_WAIT_RX_ERROR)
#define UART_EVENT_MASK                                                        \
    (UART_RX_EVENTS | MR_UART_WAIT_TX_DONE)
#define UART_KNOWN_EVENTS                                                      \
    (UART_EVENT_MASK | MR_UART_WAIT_TX_ERROR | MR_UART_WAIT_RX_STREAM_HALF     \
     | MR_UART_WAIT_RX_STREAM_FULL | MR_UART_WAIT_TX_STREAM_HALF               \
     | MR_UART_WAIT_TX_STREAM_DONE)

/* UART RX IRQ mask definition */
#define UART_RX_IRQ_MASK                                                       \
    (USART_CTLR1_RXNEIE | USART_CTLR1_IDLEIE | USART_CTLR1_PEIE)
#define UART_RX_ERROR_MASK                                                     \
    (USART_STATR_PE | USART_STATR_FE | USART_STATR_NE | USART_STATR_ORE)

#if defined(UART_USE_STREAM)
/* UART DMA event definition */
#define UART_DMA_EVENTS                                                        \
    (MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR)

/* UART stream event mask definition */
#define UART_DMA_RX_EVENTS                                                     \
    (MR_UART_WAIT_RX_STREAM_HALF | MR_UART_WAIT_RX_STREAM_FULL                 \
     | MR_UART_WAIT_RX_ERROR)
#define UART_DMA_TX_EVENTS                                                     \
    (MR_UART_WAIT_TX_STREAM_HALF | MR_UART_WAIT_TX_STREAM_DONE                 \
     | MR_UART_WAIT_TX_ERROR)
#endif /* defined(UART_USE_STREAM) */

/* UART private data type */
typedef struct uart_priv {
    USART_TypeDef *instance;
    void (*clk_enable)(void);
    mr_ht_gpio_spec_t pins[UART_PIN_COUNT];
    int irqn;
    mr_uint8_t irq_priority;
    mr_uart_config_t config;
    mr_uint32_t events;
    mr_bool_t initialized;
    mr_bool_t rx_pending;
    mr_uint8_t rx_byte;
#if defined(UART_USE_STREAM)
    uart_dma_stream_t rx_dma, tx_dma;
#endif /* defined(UART_USE_STREAM) */
} uart_priv_t;

/* UART frame type */
typedef struct uart_frame {
    mr_uint16_t ctlr1, ctlr2, ctlr3, brr;
} uart_frame_t;

/* UART stop bit map */
static const mr_uint16_t __uart_stop_bits[] = {
    0U,
    USART_CTLR2_STOP,
    USART_CTLR2_STOP_1,
};

static mr_err_t uart_make_frame(const USART_TypeDef *instance,
                                const mr_uart_config_t *config,
                                uart_frame_t *frame) {
    RCC_ClocksTypeDef clocks;
    mr_uint32_t pclk, brr;

    /* Check parameter */
    if ((config->baud_rate == 0U) || (config->stop_bits > MR_UART_STOP_BITS_2)
        || (config->parity > MR_UART_PARITY_EVEN)
        || (config->mode > MR_UART_MODE_TX_RX)) {
        return -MR_EINVAL;
    }
    if ((config->data_bits < MR_UART_DATA_BITS_7)
        || ((config->data_bits == MR_UART_DATA_BITS_7)
            && (config->parity == MR_UART_PARITY_NONE))) {
        return -MR_ENOSYS;
    }

    /* BRR encodes the mantissa and fraction of the 16x oversampling divider */
    RCC_GetClocksFreq(&clocks);
#if defined(MR_USE_CH32V00X) || defined(MR_USE_CH32VM00X)
    MR_IGNORE(instance);
    pclk = clocks.PCLK2_Frequency;
#else
    pclk = (instance == USART1)
        ? clocks.PCLK2_Frequency : clocks.PCLK1_Frequency;
#endif /* defined(MR_USE_CH32V00X) || defined(MR_USE_CH32VM00X) */
    if (config->baud_rate > pclk / 16U) {
        return -MR_EINVAL;
    }
    brr = (pclk + config->baud_rate / 2U) / config->baud_rate;
    if (brr > 0xFFFFU) {
        return -MR_EINVAL;
    }

    /* The hardware word length includes the parity bit */
    frame->ctlr1 = USART_CTLR1_UE;
    if (config->parity != MR_UART_PARITY_NONE) {
        frame->ctlr1 |= USART_CTLR1_PCE;
        if (config->data_bits == MR_UART_DATA_BITS_8) {
            frame->ctlr1 |= USART_CTLR1_M;
        }
        if (config->parity == MR_UART_PARITY_ODD) {
            frame->ctlr1 |= USART_CTLR1_PS;
        }
    }
    if (config->mode != MR_UART_MODE_RX) {
        frame->ctlr1 |= USART_CTLR1_TE;
    }
    if (config->mode != MR_UART_MODE_TX) {
        frame->ctlr1 |= USART_CTLR1_RE;
    }

    /* Translate flow control */
    frame->ctlr3 = 0U;
    if (config->flow_ctrl & MR_UART_FLOW_CTRL_RTS) {
        if (config->mode == MR_UART_MODE_TX) {
            return -MR_EINVAL;
        }
        frame->ctlr3 |= USART_CTLR3_RTSE;
    }
    if (config->flow_ctrl & MR_UART_FLOW_CTRL_CTS) {
        if (config->mode == MR_UART_MODE_RX) {
            return -MR_EINVAL;
        }
        frame->ctlr3 |= USART_CTLR3_CTSE;
    }
    frame->ctlr2 = __uart_stop_bits[config->stop_bits];
    frame->brr = (mr_uint16_t)brr;
    return 0;
}

static mr_uint32_t uart_pin_mask(mr_uint16_t ctlr1, mr_uint16_t ctlr3) {
    mr_uint32_t mask;

    /* Translate required pins */
    mask = 0U;
    if (ctlr1 & USART_CTLR1_RE) {
        mask |= 1U << UART_PIN_RX;
    }
    if (ctlr1 & USART_CTLR1_TE) {
        mask |= 1U << UART_PIN_TX;
    }
    if (ctlr3 & USART_CTLR3_RTSE) {
        mask |= 1U << UART_PIN_RTS;
    }
    if (ctlr3 & USART_CTLR3_CTSE) {
        mask |= 1U << UART_PIN_CTS;
    }
    return mask;
}

static mr_err_t uart_pin_apply(const uart_priv_t *priv, uart_pin_t pin,
                               mr_bool_t enable) {
    /* Configure or release the pin */
    if (!enable) {
        return mr_ch32_gpio_pin_release(&priv->pins[pin]);
    }
    return mr_ch32_gpio_pin_alternate(
        &priv->pins[pin], (pin == UART_PIN_TX) || (pin == UART_PIN_RTS),
        MR_GPIO_PULL_NONE, MR_GPIO_OTYPE_PP);
}

static mr_err_t uart_configure_pins(const uart_priv_t *priv,
                                   const uart_frame_t *frame) {
    const mr_ht_gpio_spec_t *spec, *other;
    mr_uint32_t previous, next, changed;
    uart_pin_t pin, prior;
    mr_err_t ret;

    /* Check pin specifications */
    previous = priv->initialized
        ? uart_pin_mask(priv->instance->CTLR1, priv->instance->CTLR3) : 0U;
    next = uart_pin_mask(frame->ctlr1, frame->ctlr3);
    for (pin = UART_PIN_RX; pin < UART_PIN_COUNT; pin++) {
        spec = &priv->pins[pin];
        if (spec->drv == MR_NULL) {
            if (next & (1U << pin)) {
                return -MR_EINVAL;
            }
            continue;
        }
        for (prior = UART_PIN_RX; prior < pin; prior++) {
            other = &priv->pins[prior];
            if ((spec->drv == other->drv) && (spec->pin == other->pin)) {
                return -MR_EINVAL;
            }
        }
    }

    /* Apply changed pins */
    changed = previous ^ next;
    for (pin = UART_PIN_RX; pin < UART_PIN_COUNT; pin++) {
        if ((changed & (1U << pin)) == 0U) {
            continue;
        }
        ret = uart_pin_apply(priv, pin, (next & (1U << pin)) != 0U);
        if (ret != 0) {
            goto _rollback;
        }
    }
    return 0;

_rollback:
    /* Restore the old pin routing before returning the first error */
    while (pin != UART_PIN_RX) {
        pin--;
        if (changed & (1U << pin)) {
            uart_pin_apply(priv, pin, (previous & (1U << pin)) != 0U);
        }
    }
    return ret;
}

static void uart_rx_irq_update(const uart_priv_t *priv) {
    mr_uint16_t ctlr1, ctlr3;
    USART_TypeDef *instance;
    mr_bool_t armed;

    /* Update RX interrupts */
    instance = priv->instance;
    ctlr1 = instance->CTLR1 & ~UART_RX_IRQ_MASK;
    ctlr3 = instance->CTLR3 & ~USART_CTLR3_EIE;
    armed = ((ctlr1 & USART_CTLR1_RE) != 0U) && !priv->rx_pending;
#if defined(UART_USE_STREAM)
    armed = armed && !priv->rx_dma.active;
#endif /* defined(UART_USE_STREAM) */
    if (armed) {
        if (priv->events & MR_UART_WAIT_RX_READY) {
            ctlr1 |= USART_CTLR1_RXNEIE;
        }
        if (priv->events & MR_UART_WAIT_RX_IDLE) {
            ctlr1 |= USART_CTLR1_IDLEIE;
        }
        if (priv->events & MR_UART_WAIT_RX_ERROR) {
            ctlr1 |= USART_CTLR1_PEIE | USART_CTLR1_RXNEIE;
            ctlr3 |= USART_CTLR3_EIE;
        }
    }
    instance->CTLR1 = ctlr1;
    instance->CTLR3 = ctlr3;
}

static mr_err_t uart_configure(const mr_driver_t *drv,
                               const mr_uart_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    uart_frame_t frame;
    mr_err_t ret;
    int mask;

    /* Check UART resources */
    if (priv->instance == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Build UART frame */
    ret = uart_make_frame(priv->instance, config, &frame);
    if (ret != 0) {
        return ret;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check UART state */
#if defined(UART_USE_STREAM)
    if (priv->rx_dma.active || priv->tx_dma.active) {
        ret = -MR_EBUSY;
        goto _exit;
    }
#endif /* defined(UART_USE_STREAM) */
    if (priv->rx_pending
        || (priv->initialized && (priv->config.mode != MR_UART_MODE_RX)
            && ((priv->instance->STATR & USART_STATR_TC) == 0U))) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Configure the pins and apply the board remap before enabling USART */
    ret = uart_configure_pins(priv, &frame);
    if (ret != 0) {
        goto _exit;
    }

    /* Init UART */
    if (!priv->initialized) {
        priv->clk_enable();
    }
    priv->instance->CTLR1 = 0U;
    priv->instance->CTLR2 = frame.ctlr2;
    priv->instance->CTLR3 = frame.ctlr3;
    priv->instance->BRR = frame.brr;
    priv->config = *config;
    priv->initialized = MR_TRUE;
    priv->instance->CTLR1 = frame.ctlr1;

    /* Update UART interrupt */
    uart_rx_irq_update(priv);

_exit:
    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t uart_configuration(const mr_driver_t *drv,
                                   mr_uart_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check UART state */
    if (!priv->initialized) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Get UART configuration */
    *config = priv->config;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_ssize_t uart_read(const mr_driver_t *drv, void *buf, mr_size_t size) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint16_t bits;
    mr_size_t count;
    mr_uint8_t *dst;
    int mask;

    /* Read received data */
    dst = (mr_uint8_t *)buf;
    count = 0U;
    while (count < size) {
        mask = mr_port_irq_disable();
#if defined(UART_USE_STREAM)
        if (priv->rx_dma.active) {
            mr_port_irq_enable(mask);
            return (count != 0U) ? (mr_ssize_t)count : -MR_EBUSY;
        }
#endif /* defined(UART_USE_STREAM) */
        if (!priv->initialized || (priv->config.mode == MR_UART_MODE_TX)) {
            mr_port_irq_enable(mask);
            return (count != 0U) ? (mr_ssize_t)count : -MR_ENODEV;
        }
        if (priv->rx_pending) {
            dst[count++] = priv->rx_byte;
            priv->rx_pending = MR_FALSE;
            mr_port_irq_enable(mask);
            continue;
        }
        if ((priv->instance->STATR & USART_STATR_RXNE) == 0U) {
            mr_port_irq_enable(mask);
            break;
        }
        bits = (priv->config.data_bits == MR_UART_DATA_BITS_7) ? 0x7FU : 0xFFU;
        dst[count++] = (mr_uint8_t)(priv->instance->DATAR & bits);
        mr_port_irq_enable(mask);
    }

    /* Rearm receive events after giving the consumer access to the data */
    mask = mr_port_irq_disable();
    uart_rx_irq_update(priv);
    mr_port_irq_enable(mask);
    return (mr_ssize_t)count;
}

static mr_ssize_t uart_write(const mr_driver_t *drv, const void *buf,
                             mr_size_t size) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uint8_t *src;
    mr_uint16_t bits;
    mr_size_t count;
    int mask;

    /* Write transmit data */
    src = (const mr_uint8_t *)buf;
    count = 0U;
    while (count < size) {
        mask = mr_port_irq_disable();
#if defined(UART_USE_STREAM)
        if (priv->tx_dma.active) {
            mr_port_irq_enable(mask);
            return (count != 0U) ? (mr_ssize_t)count : -MR_EBUSY;
        }
#endif /* defined(UART_USE_STREAM) */
        if (!priv->initialized || (priv->config.mode == MR_UART_MODE_RX)) {
            mr_port_irq_enable(mask);
            return (count != 0U) ? (mr_ssize_t)count : -MR_ENODEV;
        }
        if ((priv->instance->STATR & USART_STATR_TXE) == 0U) {
            mr_port_irq_enable(mask);
            break;
        }
        bits = (priv->config.data_bits == MR_UART_DATA_BITS_7) ? 0x7FU : 0xFFU;
        priv->instance->DATAR = src[count++] & bits;
        if (priv->events & MR_UART_WAIT_TX_DONE) {
            priv->instance->CTLR1 |= USART_CTLR1_TCIE;
        }
        mr_port_irq_enable(mask);
    }
    return (mr_ssize_t)count;
}

#if defined(UART_USE_STREAM)
static void uart_dma_signal(const uart_priv_t *priv,
                            const uart_dma_stream_t *stream,
                            mr_uint32_t events) {
    const mr_uart_data_t *data;

    /* Raise subscribed UART events */
    events &= priv->events;
    data = MR_DRIVER_DATA(stream->driver);
    if ((events != 0U) && mr_driver_is_ready(stream->driver)
        && MR_OBJECT_IS_INITED(&data->event)) {
        mr_uart_event_raise(stream->driver, events);
    }
}

static void uart_dma_event(uart_priv_t *priv, uart_dma_stream_t *stream,
                            mr_wait_event_t event, mr_bool_t rx) {
    mr_uint16_t request;
    mr_uint32_t events;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
    if (!stream->active || stream->complete || stream->draining) {
        mr_port_irq_enable(mask);
        return;
    }

    /* Translate DMA event */
    request = rx ? USART_CTLR3_DMAR : USART_CTLR3_DMAT;
    events = 0U;
    if (event & MR_DMA_WAIT_ERROR) {
        /* Stop UART DMA requests */
        priv->instance->CTLR3 &= ~request;
        stream->complete = MR_TRUE;
        events = rx ? MR_UART_WAIT_RX_ERROR : MR_UART_WAIT_TX_ERROR;
    } else {
        if (event & MR_DMA_WAIT_HALF) {
            events |= rx ? MR_UART_WAIT_RX_STREAM_HALF
                         : MR_UART_WAIT_TX_STREAM_HALF;
        }
        if (event & MR_DMA_WAIT_DONE) {
            if (stream->transfer.next != MR_NULL) {
                events |= rx ? MR_UART_WAIT_RX_STREAM_FULL
                             : MR_UART_WAIT_TX_STREAM_DONE;
            } else {
                priv->instance->CTLR3 &= ~request;
                if (rx) {
                    stream->complete = MR_TRUE;
                    events |= MR_UART_WAIT_RX_STREAM_FULL;
                } else {
                    /* TC is final only after DMA has supplied the last byte */
                    stream->draining = MR_TRUE;
                    priv->instance->CTLR1 |= USART_CTLR1_TCIE;
                }
            }
        }
    }
    uart_dma_signal(priv, stream, events);

    /* Unlock */
    mr_port_irq_enable(mask);
}

static void uart_dma_rx_event(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    uart_priv_t *priv;

    priv = (uart_priv_t *)param;
    MR_IGNORE(waiter);
    MR_IGNORE(data);
    uart_dma_event(priv, &priv->rx_dma, event, MR_TRUE);
}

static void uart_dma_tx_event(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    uart_priv_t *priv;

    priv = (uart_priv_t *)param;
    MR_IGNORE(waiter);
    MR_IGNORE(data);
    uart_dma_event(priv, &priv->tx_dma, event, MR_FALSE);
}

static void uart_dma_irq_update(const uart_priv_t *priv) {
    /* Update UART IRQ */
    if (!mr_ch32_irq_valid(priv->irqn)) {
        return;
    }
    if ((priv->events & UART_EVENT_MASK) || priv->tx_dma.active) {
        NVIC_SetPriority((IRQn_Type)priv->irqn, priv->irq_priority);
        NVIC_EnableIRQ((IRQn_Type)priv->irqn);
    } else {
        NVIC_DisableIRQ((IRQn_Type)priv->irqn);
        NVIC_ClearPendingIRQ((IRQn_Type)priv->irqn);
    }
}

static mr_err_t uart_dma_stop(uart_priv_t *priv, uart_dma_stream_t *stream,
                              mr_bool_t rx) {
    mr_err_t ret;

    /* Check stream state */
    if (!stream->active) {
        return 0;
    }

    /* Stop UART DMA request */
    priv->instance->CTLR3 &= ~(rx ? USART_CTLR3_DMAR : USART_CTLR3_DMAT);

    /* Stop DMA */
    ret = mr_ht_dma_stop(&stream->dma);
    if (ret != 0) {
        return ret;
    }

    /* Disable DMA event */
    mr_ht_dma_event_disable(&stream->dma, UART_DMA_EVENTS);

    /* Release waiter */
    mr_waiter_del(&stream->waiter);

    /* Reset stream state */
    stream->active = MR_FALSE;
    stream->complete = MR_FALSE;
    stream->draining = MR_FALSE;
    stream->driver = MR_NULL;

    /* Update UART interrupt */
    if (rx) {
        uart_rx_irq_update(priv);
    } else {
        priv->instance->CTLR1 &= ~USART_CTLR1_TCIE;
    }
    uart_dma_irq_update(priv);
    return 0;
}

static mr_err_t uart_dma_start(const mr_driver_t *drv, mr_bool_t rx,
                               void *buffer, mr_size_t size,
                               mr_bool_t circular) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    uart_dma_stream_t *stream;
    mr_dma_config_t config;
    mr_err_t ret;
    int mask;

    /* Check DMA */
    stream = rx ? &priv->rx_dma : &priv->tx_dma;
    if (stream->dma.drv == MR_NULL) {
        return -MR_ENOSYS;
    }
    if ((priv->rx_dma.dma.drv == priv->tx_dma.dma.drv)
        && (priv->rx_dma.dma.channel == priv->tx_dma.dma.channel)) {
        return -MR_EINVAL;
    }
    if (!mr_driver_is_ready(stream->dma.drv)) {
        return -MR_ENODEV;
    }
    if (!rx && !circular && !mr_ch32_irq_valid(priv->irqn)) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check UART state */
    if (!priv->initialized) {
        ret = -MR_ENODEV;
        goto _exit;
    }

    /* Check UART mode */
    if ((rx && (priv->config.mode == MR_UART_MODE_TX))
        || (!rx && (priv->config.mode == MR_UART_MODE_RX))) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* DMA cannot strip the parity bit from a seven-bit payload */
    if (rx && (priv->config.data_bits == MR_UART_DATA_BITS_7)) {
        ret = -MR_ENOSYS;
        goto _exit;
    }

    /* Check RX state */
    if (rx && (priv->rx_pending || (priv->events & MR_UART_WAIT_RX_IDLE))) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check stream state */
    if (stream->active && !stream->complete) {
        ret = -MR_EBUSY;
        goto _exit;
    }
    ret = uart_dma_stop(priv, stream, rx);
    if (ret != 0) {
        goto _exit;
    }

    /* Configure DMA */
    config = (mr_dma_config_t){
        .src_width = MR_DMA_WIDTH_8,
        .dst_width = MR_DMA_WIDTH_8,
        .src_adj = rx ? MR_DMA_ADJ_CONST : MR_DMA_ADJ_INC,
        .dst_adj = rx ? MR_DMA_ADJ_INC : MR_DMA_ADJ_CONST,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = rx ? MR_DMA_DIR_P2M : MR_DMA_DIR_M2P,
    };
    ret = mr_ht_dma_configure(&stream->dma, &config);
    if (ret != 0) {
        goto _exit;
    }

    /* Init waiter */
    ret = mr_waiter_init(&stream->waiter,
                         rx ? uart_dma_rx_event : uart_dma_tx_event, priv,
                         UART_DMA_EVENTS);
    if (ret != 0) {
        goto _exit;
    }

    /* Attach DMA event */
    ret = mr_ht_dma_event_attach(&stream->dma, &stream->waiter);
    if (ret != 0) {
        goto _release;
    }
    ret = mr_ht_dma_event_enable(&stream->dma, UART_DMA_EVENTS);
    if (ret != 0) {
        goto _release;
    }

    /* Start DMA */
    stream->transfer.next = circular ? &stream->transfer : MR_NULL;
    stream->transfer.count = size;
    stream->transfer.src = rx ? (const void *)&priv->instance->DATAR : buffer;
    stream->transfer.dst = rx ? buffer : (void *)&priv->instance->DATAR;
    ret = mr_ht_dma_start(&stream->dma, &stream->transfer);
    if (ret != 0) {
        mr_ht_dma_event_disable(&stream->dma, UART_DMA_EVENTS);
        goto _release;
    }

    /* The channel and waiter must be live before opening UART requests */
    stream->driver = (mr_driver_t *)drv;
    stream->active = MR_TRUE;
    if (rx) {
        uart_rx_irq_update(priv);
        priv->instance->CTLR3 |= USART_CTLR3_DMAR;
    } else {
        priv->instance->CTLR1 &= ~USART_CTLR1_TCIE;
        priv->instance->STATR = (mr_uint16_t)~USART_STATR_TC;
        priv->instance->CTLR3 |= USART_CTLR3_DMAT;
    }
    uart_dma_irq_update(priv);
    goto _exit;

_release:
    /* Release waiter */
    mr_waiter_del(&stream->waiter);
_exit:
    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t uart_rx_stream_start(const mr_driver_t *drv, void *buf,
                                    mr_size_t size,
                                    const mr_uart_rx_stream_config_t *config) {
    return uart_dma_start(drv, MR_TRUE, buf, size, config->circular);
}

static mr_err_t uart_tx_stream_start(const mr_driver_t *drv, const void *buf,
                                    mr_size_t size,
                                    const mr_uart_tx_stream_config_t *config) {
    return uart_dma_start(drv, MR_FALSE, (void *)buf, size, config->circular);
}

static mr_err_t uart_rx_stream_stop(const mr_driver_t *drv) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();
    ret = uart_dma_stop(priv, &priv->rx_dma, MR_TRUE);

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t uart_tx_stream_stop(const mr_driver_t *drv) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();
    ret = uart_dma_stop(priv, &priv->tx_dma, MR_FALSE);

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t uart_rx_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get RX stream state */
    mask = mr_port_irq_disable();
    *busy = priv->rx_dma.active && !priv->rx_dma.complete;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t uart_tx_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get TX stream state */
    mask = mr_port_irq_disable();
    *busy = priv->tx_dma.active && !priv->tx_dma.complete;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t uart_rx_stream_offset(const mr_driver_t *drv,
                                     mr_size_t *offset) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get RX stream offset */
    if (!mr_driver_is_ready(priv->rx_dma.dma.drv)) {
        return priv->rx_dma.dma.drv ? -MR_ENODEV : -MR_ENOSYS;
    }
    return mr_ht_dma_offset(&priv->rx_dma.dma, offset);
}

static mr_err_t uart_tx_stream_offset(const mr_driver_t *drv,
                                     mr_size_t *offset) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get TX stream offset */
    if (!mr_driver_is_ready(priv->tx_dma.dma.drv)) {
        return priv->tx_dma.dma.drv ? -MR_ENODEV : -MR_ENOSYS;
    }
    return mr_ht_dma_offset(&priv->tx_dma.dma, offset);
}
#endif /* defined(UART_USE_STREAM) */

static mr_err_t uart_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t supported;
    mr_bool_t tx_draining;
    mr_uint16_t ctlr1;
    int mask;

    /* Check parameter */
    if ((events == 0U) || ((events & ~UART_KNOWN_EVENTS) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check event support */
    supported = !mr_ch32_irq_valid(priv->irqn) ? 0U : UART_EVENT_MASK;
#if defined(UART_USE_STREAM)
    if (priv->rx_dma.dma.drv != MR_NULL) {
        supported |= UART_DMA_RX_EVENTS;
    }
    if (priv->tx_dma.dma.drv != MR_NULL) {
        supported |= UART_DMA_TX_EVENTS;
    }
#endif /* defined(UART_USE_STREAM) */
    if ((events & ~supported) != 0U) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check UART state */
    if (!priv->initialized) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }
#if defined(UART_USE_STREAM)
    /* Check RX stream */
    if (enable && priv->rx_dma.active && (events & MR_UART_WAIT_RX_IDLE)) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
#endif /* defined(UART_USE_STREAM) */

    /* Update event mask */
    ctlr1 = priv->instance->CTLR1;
#if defined(UART_USE_STREAM)
    tx_draining = priv->tx_dma.draining;
#else
    tx_draining = MR_FALSE;
#endif /* defined(UART_USE_STREAM) */
    if (enable) {
#if !defined(UART_USE_STREAM)
        if (priv->events == 0U) {
            NVIC_SetPriority((IRQn_Type)priv->irqn, priv->irq_priority);
            NVIC_ClearPendingIRQ((IRQn_Type)priv->irqn);
            NVIC_EnableIRQ((IRQn_Type)priv->irqn);
        }
#endif /* !defined(UART_USE_STREAM) */
        priv->events |= events;
        if ((events & MR_UART_WAIT_TX_DONE) && (ctlr1 & USART_CTLR1_TE)
#if defined(UART_USE_STREAM)
            && !priv->tx_dma.active
#endif /* defined(UART_USE_STREAM) */
            && ((priv->instance->STATR & USART_STATR_TC) == 0U)) {
            ctlr1 |= USART_CTLR1_TCIE;
        }
    } else {
        priv->events &= ~events;
        if ((events & MR_UART_WAIT_TX_DONE) && !tx_draining) {
            ctlr1 &= ~USART_CTLR1_TCIE;
        }
    }
    priv->instance->CTLR1 = ctlr1;

    /* Update UART interrupt */
    uart_rx_irq_update(priv);
#if defined(UART_USE_STREAM)
    uart_dma_irq_update(priv);
#else
    if (priv->events == 0U) {
        NVIC_DisableIRQ((IRQn_Type)priv->irqn);
    }
#endif /* defined(UART_USE_STREAM) */

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_uint32_t uart_rx_irq_handle(uart_priv_t *priv, mr_uint16_t status,
                                     mr_uint16_t *ctlr1) {
    mr_uint16_t ctlr3, data, bits;
    mr_bool_t rxne, overrun;
    mr_uint32_t events;

    /* Handle RX events */
    ctlr3 = priv->instance->CTLR3;
    rxne = (status & USART_STATR_RXNE) != 0U;
    overrun = ((*ctlr1 & USART_CTLR1_RXNEIE) != 0U)
           && ((status & USART_STATR_ORE) != 0U);
    events = 0U;
    if ((*ctlr1 & USART_CTLR1_IDLEIE) && (status & USART_STATR_IDLE)) {
        events |= MR_UART_WAIT_RX_IDLE;
    }
    if ((priv->events & MR_UART_WAIT_RX_ERROR)
        && ((status & UART_RX_ERROR_MASK) != 0U)) {
        events |= MR_UART_WAIT_RX_ERROR;
    }
    if ((events != 0U) || (overrun && !rxne)
        || (rxne && (priv->events & MR_UART_WAIT_RX_ERROR))) {
        /* STATR followed by DATAR clears IDLE/errors and consumes RXNE */
        data = priv->instance->DATAR;
        if (rxne) {
            bits = (priv->config.data_bits == MR_UART_DATA_BITS_7)
                ? 0x7FU : 0xFFU;
            priv->rx_byte = (mr_uint8_t)(data & bits);
            priv->rx_pending = MR_TRUE;
            /* Wait for the consumer to drain the staged byte */
            *ctlr1 &= ~UART_RX_IRQ_MASK;
            priv->instance->CTLR3 = ctlr3 & ~USART_CTLR3_EIE;
        }
    }
    if (rxne && (priv->events & MR_UART_WAIT_RX_READY)
        && (priv->rx_pending || (*ctlr1 & USART_CTLR1_RXNEIE))) {
        events |= MR_UART_WAIT_RX_READY;
    }
    if (rxne && (*ctlr1 & USART_CTLR1_RXNEIE)) {
        *ctlr1 &= ~USART_CTLR1_RXNEIE;
    }
    return events;
}

MR_UNUSED static void uart_irq_handle(mr_driver_t *drv) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uart_data_t *data;
    mr_uint16_t status, ctlr1;
    mr_uint32_t events;
    int mask;

    /* Keep teardown excluded through the event notification */
    mask = mr_port_irq_disable();

    /* Check UART state */
    if (!priv->initialized || !mr_ch32_irq_valid(priv->irqn)) {
        mr_port_irq_enable(mask);
        return;
    }
    data = MR_DRIVER_DATA(drv);
    ctlr1 = priv->instance->CTLR1;

    /* Disable events for an unusable driver */
    if (!mr_driver_is_ready(drv) || !MR_OBJECT_IS_INITED(&data->event)) {
        priv->instance->CTLR1 = ctlr1 & ~(UART_RX_IRQ_MASK | USART_CTLR1_TCIE);
        priv->instance->CTLR3 &= ~USART_CTLR3_EIE;
        priv->events = 0U;
        priv->rx_pending = MR_FALSE;
        NVIC_DisableIRQ((IRQn_Type)priv->irqn);
        mr_port_irq_enable(mask);
        return;
    }

    /* Handle RX events */
    status = priv->instance->STATR;
    events = 0U;
    if ((ctlr1 & UART_RX_IRQ_MASK)
        || (priv->instance->CTLR3 & USART_CTLR3_EIE)) {
        events = uart_rx_irq_handle(priv, status, &ctlr1);
    }

    /* Handle TX complete */
    if ((ctlr1 & USART_CTLR1_TCIE) && (status & USART_STATR_TC)) {
        /* Keep TC set for the next reconfiguration and arm it on write */
        ctlr1 &= ~USART_CTLR1_TCIE;
#if defined(UART_USE_STREAM)
        if (priv->tx_dma.draining) {
            priv->tx_dma.draining = MR_FALSE;
            priv->tx_dma.complete = MR_TRUE;
            events |= (MR_UART_WAIT_TX_STREAM_DONE | MR_UART_WAIT_TX_DONE)
                      & priv->events;
        } else {
            events |= MR_UART_WAIT_TX_DONE;
        }
#else
        events |= MR_UART_WAIT_TX_DONE;
#endif /* defined(UART_USE_STREAM) */
    }
    priv->instance->CTLR1 = ctlr1;

    /* Raise UART event */
    if (events != 0U) {
        mr_uart_event_raise(drv, events);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

/* Instance install */
#define CH32_HT_UART_INSTALL(_id, _ops)                                        \
    extern void MR_HT_DRIVER_PROP(_id, wch_clken)(void);                       \
    static uart_priv_t MR_CAT(__uart_priv_, _id) = {                           \
        .instance = MR_HT_DRIVER_PROP(_id, wch_uartx),                         \
        .clk_enable = MR_HT_DRIVER_PROP(_id, wch_clken),                       \
        .pins = {                                                              \
            [UART_PIN_RX] = __CH32_HT_UART_PIN(_id, rx),                       \
            [UART_PIN_TX] = __CH32_HT_UART_PIN(_id, tx),                       \
            [UART_PIN_RTS] = __CH32_HT_UART_PIN(_id, rts),                     \
            [UART_PIN_CTS] = __CH32_HT_UART_PIN(_id, cts),                     \
        },                                                                     \
        .irqn = __CH32_HT_UART_IRQ_NUMBER(_id),                                \
        .irq_priority = __CH32_HT_UART_IRQ_PRIORITY(_id),                      \
        __CH32_HT_UART_STREAM_INIT(_id)                                        \
    };                                                                         \
    __CH32_HT_UART_IRQ(_id)                                                    \
    MR_HT_UART_INSTALL(_id, &MR_CAT(__uart_priv_, _id), _ops)
#define __CH32_HT_UART_PIN(_id, _prop)                                         \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, _prop),                             \
                (MR_HT_GPIO_SPEC(_id, _prop)), ({0}))
#define __CH32_HT_UART_IRQ(_id)                                                \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_irq_handler),                   \
                (MR_CH32_INTERRUPT void                                        \
                     MR_HT_DRIVER_PROP(_id, wch_irq_handler)(void) {           \
                     uart_irq_handle(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));    \
                 }),                                                           \
                ())
#define __CH32_HT_UART_IRQ_NUMBER(_id)                                         \
    MR_HT_DRIVER_PROP_OR(_id, wch_irqn, UART_NO_IRQ)
#define __CH32_HT_UART_IRQ_PRIORITY(_id)                                       \
    MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U)
#if defined(UART_USE_STREAM)
#define __CH32_HT_UART_DMA_SPEC(_id, _prop)                                    \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, _prop),                             \
                (MR_HT_DMA_SPEC(_id, _prop)), ({0}))
#define __CH32_HT_UART_STREAM_INIT(_id)                                        \
    .rx_dma = {                                                                \
        .dma = __CH32_HT_UART_DMA_SPEC(_id, rx_dma),                           \
    },                                                                         \
    .tx_dma = {                                                                \
        .dma = __CH32_HT_UART_DMA_SPEC(_id, tx_dma),                           \
    },
#else
#define __CH32_HT_UART_STREAM_INIT(_id)
#endif /* defined(UART_USE_STREAM) */
MR_HT_DRIVER_FOREACH(CH32_HT_UART_INSTALL, &__uart_ops);

#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_UART) */

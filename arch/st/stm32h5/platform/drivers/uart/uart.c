/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-17        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_UART)
#include <mrX/drivers/uart/mr_uart.h>
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#include <mrX/libc/mr_atomic.h>
#endif /* UART stream + DMA */
#include "stm32h5xx_hal_uart.h"

/* HardTree compatible */
#define MR_HT_COMPAT st_stm32h5_uart

#define UART_EVENT_BASE_MASK                                                   \
    (MR_UART_WAIT_RX_IDLE | MR_UART_WAIT_RX_READY | MR_UART_WAIT_TX_DONE       \
     | MR_UART_WAIT_RX_ERROR | MR_UART_WAIT_TX_ERROR)
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
#define UART_EVENT_MASK                                                        \
    (UART_EVENT_BASE_MASK | MR_UART_WAIT_RX_STREAM_HALF                        \
     | MR_UART_WAIT_RX_STREAM_FULL | MR_UART_WAIT_TX_STREAM_HALF               \
     | MR_UART_WAIT_TX_STREAM_DONE)
#else
#define UART_EVENT_MASK UART_EVENT_BASE_MASK
#endif /* UART stream + DMA */

/* UART pin */
typedef struct uart_pin {
    GPIO_TypeDef *port;
    uint32_t pin;
    uint32_t alternate;
} uart_pin_t;

#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
/* UART stream data */
typedef struct uart_stream {
    mr_ht_dma_spec_t dma;
    mr_dma_transfer_t transfer;
    mr_waiter_t waiter;
    mr_driver_t *driver;
    volatile mr_atomic_t active;
    volatile mr_atomic_t complete;
    volatile mr_atomic_t dma_complete;
    mr_bool_t circular;
    mr_bool_t failed;
    mr_bool_t waiter_initialized;
} uart_stream_t;
#endif /* UART stream + DMA */

/* UART private data */
typedef struct uart_priv {
    UART_HandleTypeDef handle;
    void (*clk_enable)(void);
    void (*gpio_clk_enable)(void);
    uart_pin_t tx;
    uart_pin_t rx;
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    mr_driver_t *driver;
    mr_uint32_t event_mask;
    mr_uart_config_t config;
    mr_bool_t initialized;
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
    uart_stream_t rx_stream;
    uart_stream_t tx_stream;
#endif /* UART stream + DMA */
} uart_priv_t;

/* UART operations */
static mr_err_t uart_configure(const mr_driver_t *drv,
                               const mr_uart_config_t *config);
static mr_err_t uart_configuration(const mr_driver_t *drv,
                                   mr_uart_config_t *config);
static mr_ssize_t uart_read(const mr_driver_t *drv, void *buf, mr_size_t size);
static mr_ssize_t uart_write(const mr_driver_t *drv, const void *buf,
                             mr_size_t size);
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
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
#endif /* UART stream + DMA */
static mr_err_t uart_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable);

MR_UNUSED static const mr_uart_ops_t __uart_ops = {
    .configure = uart_configure,
    .configuration = uart_configuration,
    .read = uart_read,
    .write = uart_write,
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
    .rx_stream_start = uart_rx_stream_start,
    .rx_stream_stop = uart_rx_stream_stop,
    .rx_stream_is_busy = uart_rx_stream_is_busy,
    .rx_stream_offset = uart_rx_stream_offset,
    .tx_stream_start = uart_tx_stream_start,
    .tx_stream_stop = uart_tx_stream_stop,
    .tx_stream_is_busy = uart_tx_stream_is_busy,
    .tx_stream_offset = uart_tx_stream_offset,
#endif /* UART stream + DMA */
    .event_control = uart_event_control,
};

/* HAL word length includes the optional parity bit */
static const uint32_t __uart_word_length[][2] = {
    {UART_WORDLENGTH_7B, UART_WORDLENGTH_8B},
    {UART_WORDLENGTH_8B, UART_WORDLENGTH_9B},
};

static const uint32_t __uart_stop_bits[] = {
    UART_STOPBITS_1,
    UART_STOPBITS_1_5,
    UART_STOPBITS_2,
};

static const uint32_t __uart_parity[] = {
    UART_PARITY_NONE,
    UART_PARITY_ODD,
    UART_PARITY_EVEN,
};

static const uint32_t __uart_mode[] = {
    UART_MODE_RX,
    UART_MODE_TX,
    UART_MODE_TX_RX,
};

static mr_err_t uart_make_init(const mr_uart_config_t *config,
                               UART_InitTypeDef *init) {
    mr_uint32_t data_index;
    mr_uint32_t parity_index;

    /* Reject invalid fields and unsupported but legal frames */
    if (config->baud_rate == 0U) {
        return -MR_EINVAL;
    }
    if (config->stop_bits > MR_UART_STOP_BITS_2) {
        return -MR_EINVAL;
    }
    if (config->flow_ctrl != MR_UART_FLOW_CTRL_NONE) {
        return -MR_ENOSYS;
    }
    if ((config->data_bits < MR_UART_DATA_BITS_7)
        || (config->data_bits > MR_UART_DATA_BITS_8)) {
        return -MR_ENOSYS;
    }
    if (config->parity > MR_UART_PARITY_EVEN) {
        return -MR_EINVAL;
    }
    if (config->mode > MR_UART_MODE_TX_RX) {
        return -MR_EINVAL;
    }

    data_index = (mr_uint32_t)(config->data_bits - MR_UART_DATA_BITS_7);
    parity_index = (config->parity != MR_UART_PARITY_NONE) ? 1U : 0U;
    init->BaudRate = config->baud_rate;
    init->WordLength = __uart_word_length[data_index][parity_index];
    init->StopBits = __uart_stop_bits[config->stop_bits];
    init->Parity = __uart_parity[config->parity];
    init->Mode = __uart_mode[config->mode];
    init->HwFlowCtl = UART_HWCONTROL_NONE;
    init->OverSampling = UART_OVERSAMPLING_16;
    init->OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    init->ClockPrescaler = UART_PRESCALER_DIV1;
    return 0;
}

static void uart_interrupts_update(uart_priv_t *priv) {
    mr_bool_t rx_ready_enable;
    mr_bool_t tc_enable;

    /* Keep UART IRQ sources aligned with the enabled event mask */
    rx_ready_enable = ((priv->event_mask & MR_UART_WAIT_RX_READY) != 0U);
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
    if (mr_atomic_load(&priv->rx_stream.active) != 0) {
        rx_ready_enable = MR_FALSE;
    }
#endif /* UART stream + DMA */
    if (rx_ready_enable) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_RXNE);
    } else {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_RXNE);
    }
    if (priv->event_mask & MR_UART_WAIT_RX_IDLE) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_IDLE);
    } else {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_IDLE);
    }

    /* Keep TC armed for TX_DONE and active TX streams */
    tc_enable = ((priv->event_mask & MR_UART_WAIT_TX_DONE) != 0U)
             && (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_TC) == 0U);
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
    if (mr_atomic_load(&priv->tx_stream.active) != 0) {
        tc_enable = !priv->tx_stream.circular
                 && (mr_atomic_load(&priv->tx_stream.dma_complete) != 0)
                 && (mr_atomic_load(&priv->tx_stream.complete) == 0);
    }
#endif /* UART stream + DMA */
    if (tc_enable) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_TC);
    } else {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_TC);
    }
    if (priv->event_mask & MR_UART_WAIT_RX_ERROR) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_PE);
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_ERR);
    } else {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_PE);
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_ERR);
    }
}

static mr_err_t uart_pin_check(const uart_pin_t *pin) {
    if ((pin->port == MR_NULL) || (pin->pin == 0U)
        || ((pin->pin & (pin->pin - 1U)) != 0U) || (pin->alternate > 15U)) {
        return -MR_EINVAL;
    }
    return 0;
}

static void uart_pin_init(const uart_pin_t *pin) {
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Pin = pin->pin;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = pin->alternate;
    HAL_GPIO_Init(pin->port, &gpio_init);
}

static void uart_gpio_configure(uart_priv_t *priv, mr_uint8_t mode,
                                mr_bool_t reconfigure) {
    /* Enable GPIO clocks; HAL clock macros are idempotent */
    priv->gpio_clk_enable();
    if (reconfigure && (priv->config.mode != MR_UART_MODE_RX)
        && (mode == MR_UART_MODE_RX)) {
        HAL_GPIO_DeInit(priv->tx.port, priv->tx.pin);
    }
    if (reconfigure && (priv->config.mode != MR_UART_MODE_TX)
        && (mode == MR_UART_MODE_TX)) {
        HAL_GPIO_DeInit(priv->rx.port, priv->rx.pin);
    }
    if (!reconfigure || (priv->config.mode != mode)) {
        if (mode != MR_UART_MODE_RX) {
            uart_pin_init(&priv->tx);
        }
        if (mode != MR_UART_MODE_TX) {
            uart_pin_init(&priv->rx);
        }
    }
}

static mr_err_t uart_configure(const mr_driver_t *drv,
                               const mr_uart_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    UART_InitTypeDef init = {0};
    mr_bool_t reconfigure;
    mr_err_t ret;

    /* Check board-provided resources */
    if ((priv->handle.Instance == MR_NULL) || (priv->clk_enable == MR_NULL)
        || (priv->gpio_clk_enable == MR_NULL)) {
        return -MR_EINVAL;
    }

    /* Reject while a transfer is still using the UART */
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
    if ((mr_atomic_load(&priv->rx_stream.active) != 0)
        || (mr_atomic_load(&priv->tx_stream.active) != 0)) {
        return -MR_EBUSY;
    }
#endif /* UART stream + DMA */
    if (priv->initialized && (priv->config.mode != MR_UART_MODE_RX)
        && (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_TC) == 0U)) {
        return -MR_EBUSY;
    }

    /* Reuse the current hardware setup when the frame is unchanged */
    if (priv->initialized && (priv->config.baud_rate == config->baud_rate)
        && (priv->config.data_bits == config->data_bits)
        && (priv->config.stop_bits == config->stop_bits)
        && (priv->config.parity == config->parity)
        && (priv->config.flow_ctrl == config->flow_ctrl)
        && (priv->config.mode == config->mode)) {
        priv->clk_enable();
        priv->gpio_clk_enable();
        return 0;
    }

    /* Translate the UART frame */
    ret = uart_make_init(config, &init);
    if (ret != 0) {
        return ret;
    }

    /* Check pins used by the requested direction */
    if (config->mode != MR_UART_MODE_RX) {
        ret = uart_pin_check(&priv->tx);
        if (ret != 0) {
            return ret;
        }
    }
    if (config->mode != MR_UART_MODE_TX) {
        ret = uart_pin_check(&priv->rx);
        if (ret != 0) {
            return ret;
        }
    }

    /* Enable clocks and apply a new UART frame */
    reconfigure = priv->initialized;
    priv->clk_enable();
    if (priv->initialized) {
        ret = mr_stm32h5_hal_status(HAL_UART_DeInit(&priv->handle));
        if (ret != 0) {
            return ret;
        }
        priv->initialized = MR_FALSE;
    }
    uart_gpio_configure(priv, config->mode, reconfigure);
    priv->handle.Init = init;
    priv->handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    priv->handle.FifoMode = UART_FIFOMODE_DISABLE;
    ret = mr_stm32h5_hal_status(HAL_UART_Init(&priv->handle));
    if (ret != 0) {
        priv->initialized = MR_FALSE;
        return ret;
    }
    priv->config = *config;
    priv->driver = (mr_driver_t *)drv;
    priv->initialized = MR_TRUE;
    if (!reconfigure) {
        HAL_NVIC_SetPriority(priv->irqn, priv->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(priv->irqn);
    }
    uart_interrupts_update(priv);
    return 0;
}

static mr_err_t uart_configuration(const mr_driver_t *drv,
                                   mr_uart_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    if (!priv->initialized) {
        return -MR_ENODEV;
    }
    *config = priv->config;
    return 0;
}

static mr_ssize_t uart_read(const mr_driver_t *drv, void *buf, mr_size_t size) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t *dst = (mr_uint8_t *)buf;
    mr_size_t completed = 0U;

    if (!priv->initialized) {
        return -MR_ENODEV;
    }
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
    if (mr_atomic_load(&priv->rx_stream.active) != 0) {
        return -MR_EBUSY;
    }
#endif /* UART stream + DMA */

    /* Drain bytes already in RDR without waiting */
    while ((completed < size)
           && (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_RXNE) != 0U)) {
        *dst++ = (mr_uint8_t)(priv->handle.Instance->RDR
                              & ((priv->config.data_bits == MR_UART_DATA_BITS_7)
                                     ? 0x7FU
                                     : 0xFFU));
        completed++;
    }

    /* Rearm RX_READY after the consumer has drained the receive register */
    uart_interrupts_update(priv);
    return (mr_ssize_t)completed;
}

static mr_ssize_t uart_write(const mr_driver_t *drv, const void *buf,
                             mr_size_t size) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uint8_t *src = (const mr_uint8_t *)buf;
    mr_size_t completed = 0U;

    if (!priv->initialized) {
        return -MR_ENODEV;
    }
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
    if (mr_atomic_load(&priv->tx_stream.active) != 0) {
        return -MR_EBUSY;
    }
#endif /* UART stream + DMA */

    /* Push bytes into TDR while the holding register is empty */
    while ((completed < size)
           && (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_TXE) != 0U)) {
        priv->handle.Instance->TDR = *src++;
        completed++;
    }
    if ((completed != 0U) && (priv->event_mask & MR_UART_WAIT_TX_DONE)) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_TC);
    }
    return (mr_ssize_t)completed;
}

#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
static void uart_stream_waiter_release(uart_stream_t *stream) {
    if (stream->waiter_initialized) {
        (void)mr_waiter_del(&stream->waiter);
        stream->waiter_initialized = MR_FALSE;
    }
}

static void uart_stream_dma_event(uart_priv_t *priv, uart_stream_t *stream,
                                  mr_wait_event_t event, uint32_t dma_mask,
                                  mr_bool_t receive) {
    mr_wait_event_t uart_event = 0U;

    /* Translate DMA events while this direction is active */
    if ((mr_atomic_load(&stream->active) == 0) || (stream->driver == MR_NULL)) {
        return;
    }
    if (event & MR_DMA_WAIT_ERROR) {
        /* Stop UART DMA requests; defer waiter teardown out of IRQ */
        CLEAR_BIT(priv->handle.Instance->CR3, dma_mask);
        if (!receive) {
            stream->failed = MR_TRUE;
        }
        mr_atomic_store(&stream->complete, 1);
        if (priv->event_mask
            & (receive ? MR_UART_WAIT_RX_ERROR : MR_UART_WAIT_TX_ERROR)) {
            uart_event
                |= receive ? MR_UART_WAIT_RX_ERROR : MR_UART_WAIT_TX_ERROR;
        }
    }
    if ((event & MR_DMA_WAIT_HALF)
        && (priv->event_mask
            & (receive ? MR_UART_WAIT_RX_STREAM_HALF
                       : MR_UART_WAIT_TX_STREAM_HALF))) {
        uart_event |= receive ? MR_UART_WAIT_RX_STREAM_HALF
                              : MR_UART_WAIT_TX_STREAM_HALF;
    }
    if (event & MR_DMA_WAIT_DONE) {
        if (priv->event_mask
            & (receive ? MR_UART_WAIT_RX_STREAM_FULL
                       : MR_UART_WAIT_TX_STREAM_DONE)) {
            uart_event |= receive ? MR_UART_WAIT_RX_STREAM_FULL
                                  : MR_UART_WAIT_TX_STREAM_DONE;
        }
        if (!stream->circular) {
            CLEAR_BIT(priv->handle.Instance->CR3, dma_mask);
            if (receive) {
                mr_atomic_store(&stream->complete, 1);
            } else {
                /* TC is final only after DMA has supplied the last byte */
                mr_atomic_store(&stream->dma_complete, 1);
                __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_TC);
            }
        }
    }
    if (uart_event != 0U) {
        (void)mr_uart_event_raise(stream->driver, uart_event);
    }
}

static void uart_rx_dma_event(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    uart_priv_t *priv = (uart_priv_t *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(data);
    uart_stream_dma_event(priv, &priv->rx_stream, event, USART_CR3_DMAR,
                          MR_TRUE);
}

static void uart_tx_dma_event(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    uart_priv_t *priv = (uart_priv_t *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(data);
    uart_stream_dma_event(priv, &priv->tx_stream, event, USART_CR3_DMAT,
                          MR_FALSE);
}

static mr_err_t uart_stream_stop(uart_priv_t *priv, uart_stream_t *stream,
                                 uint32_t dma_mask) {
    mr_err_t first_error = 0;
    mr_err_t ret;

    /* Drop UART DMA requests only when the peripheral clock is on */
    if (priv->initialized && (priv->handle.Instance != MR_NULL)) {
        CLEAR_BIT(priv->handle.Instance->CR3, dma_mask);
    }
    if (mr_atomic_load(&stream->active) != 0) {
        /* Stop/recover DMA even when HAL has put the channel in ERROR */
        if (stream->dma.drv == MR_NULL) {
            first_error = -MR_ENODEV;
        } else {
            ret = mr_ht_dma_stop(&stream->dma);
            if (ret != 0) {
                first_error = ret;
            }
        }

        /* Keep ownership and the waiter so a failed stop can be retried */
        if (first_error != 0) {
            return first_error;
        }
        mr_atomic_store(&stream->active, 0);
    }
    if ((dma_mask == USART_CR3_DMAT) && priv->initialized
        && ((priv->event_mask & MR_UART_WAIT_TX_DONE) == 0U)) {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_TC);
    }
    mr_atomic_store(&stream->complete, 0);
    uart_stream_waiter_release(stream);
    stream->driver = MR_NULL;
    stream->failed = MR_FALSE;
    if ((dma_mask == USART_CR3_DMAR) && priv->initialized) {
        uart_interrupts_update(priv);
    }
    return first_error;
}

static mr_err_t uart_stream_is_busy(uart_stream_t *stream, mr_bool_t *busy) {
    if (mr_atomic_load(&stream->active) == 0) {
        *busy = MR_FALSE;
        return 0;
    }
    if (mr_atomic_load(&stream->complete) != 0) {
        *busy = MR_FALSE;
        return 0;
    }
    if (stream->dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    return mr_ht_dma_is_busy(&stream->dma, busy);
}

static mr_err_t uart_stream_start(const mr_driver_t *drv, uart_stream_t *stream,
                                  const void *src, void *dst, mr_size_t size,
                                  mr_bool_t circular, mr_bool_t receive) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_dma_config_t dma_config = {
        .src_width = MR_DMA_WIDTH_8,
        .dst_width = MR_DMA_WIDTH_8,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
    };
    uint32_t dma_mask;
    mr_err_t ret;

    dma_mask = receive ? USART_CR3_DMAR : USART_CR3_DMAT;

    /* Check the UART is configured and the DMA stream is bound */
    if (!priv->initialized) {
        return -MR_ENODEV;
    }
    if (stream->dma.drv == MR_NULL) {
        return -MR_EINVAL;
    }
    if ((receive && (priv->config.mode == MR_UART_MODE_TX))
        || (!receive && (priv->config.mode == MR_UART_MODE_RX))) {
        return -MR_EINVAL;
    }

    /* DMA cannot strip the parity bit from a seven-bit payload */
    if (receive && (priv->config.data_bits == MR_UART_DATA_BITS_7)
        && (priv->config.parity != MR_UART_PARITY_NONE)) {
        return -MR_ENOSYS;
    }

    /* Reject a running transfer and clean up a completed oneshot */
    if (mr_atomic_load(&stream->active) != 0) {
        if (mr_atomic_load(&stream->complete) == 0) {
            return -MR_EBUSY;
        }
        ret = uart_stream_stop(priv, stream, dma_mask);
        if (ret != 0) {
            return ret;
        }
    }

    /* Configure DMA for UART RDR/TDR transfers */
    dma_config.src_adj = receive ? MR_DMA_ADJ_CONST : MR_DMA_ADJ_INC;
    dma_config.dst_adj = receive ? MR_DMA_ADJ_INC : MR_DMA_ADJ_CONST;
    dma_config.direction = receive ? MR_DMA_DIR_P2M : MR_DMA_DIR_M2P;
    ret = mr_ht_dma_configure(&stream->dma, &dma_config);
    if (ret != 0) {
        return ret;
    }

    /* Attach a fresh waiter so it can be released on stream stop */
    stream->driver = (mr_driver_t *)drv;
    stream->circular = circular;
    stream->failed = MR_FALSE;
    mr_atomic_store(&stream->active, 1);
    mr_atomic_store(&stream->complete, 0);
    mr_atomic_store(&stream->dma_complete, 0);
    uart_interrupts_update(priv);
    ret = mr_waiter_init(
        &stream->waiter, receive ? uart_rx_dma_event : uart_tx_dma_event, priv,
        MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR);
    if (ret != 0) {
        goto _fail;
    }
    stream->waiter_initialized = MR_TRUE;
    ret = mr_ht_dma_event_attach(&stream->dma, &stream->waiter);
    if (ret != 0) {
        goto _fail;
    }
    ret = mr_ht_dma_event_enable(
        &stream->dma, MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR);
    if (ret != 0) {
        goto _fail;
    }

    /* Start DMA before enabling UART DMA requests */
    stream->transfer.next = circular ? &stream->transfer : MR_NULL;
    stream->transfer.src = src;
    stream->transfer.dst = dst;
    stream->transfer.count = size;
    ret = mr_ht_dma_start(&stream->dma, &stream->transfer);
    if (ret != 0) {
        goto _fail;
    }
    if (!receive) {
        __HAL_UART_CLEAR_FLAG(&priv->handle, UART_CLEAR_TCF);
    }
    SET_BIT(priv->handle.Instance->CR3, dma_mask);
    return 0;

_fail:
    (void)uart_stream_stop(priv, stream, dma_mask);
    return ret;
}

static mr_err_t uart_rx_stream_start(const mr_driver_t *drv, void *buf,
                                     mr_size_t size,
                                     const mr_uart_rx_stream_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    return uart_stream_start(drv, &priv->rx_stream,
                             (const void *)&priv->handle.Instance->RDR, buf,
                             size, config->circular, MR_TRUE);
}

static mr_err_t uart_rx_stream_stop(const mr_driver_t *drv) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    return uart_stream_stop(priv, &priv->rx_stream, USART_CR3_DMAR);
}

static mr_err_t uart_rx_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    return uart_stream_is_busy(&priv->rx_stream, busy);
}

static mr_err_t uart_rx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    if (priv->rx_stream.dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    return mr_ht_dma_offset(&priv->rx_stream.dma, offset);
}

static mr_err_t uart_tx_stream_start(const mr_driver_t *drv, const void *buf,
                                     mr_size_t size,
                                     const mr_uart_tx_stream_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    return uart_stream_start(drv, &priv->tx_stream, buf,
                             (void *)&priv->handle.Instance->TDR, size,
                             config->circular, MR_FALSE);
}

static mr_err_t uart_tx_stream_stop(const mr_driver_t *drv) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    return uart_stream_stop(priv, &priv->tx_stream, USART_CR3_DMAT);
}

static mr_err_t uart_tx_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Oneshot TX stays busy until the last stop bit, then teardown */
    if (mr_atomic_load(&priv->tx_stream.active) == 0) {
        *busy = MR_FALSE;
        return 0;
    }
    if (mr_atomic_load(&priv->tx_stream.complete) == 0) {
        *busy = MR_TRUE;
        return 0;
    }
    *busy = MR_FALSE;
    return uart_tx_stream_stop(drv);
}

static mr_err_t uart_tx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    if (priv->tx_stream.dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    return mr_ht_dma_offset(&priv->tx_stream.dma, offset);
}
#endif /* UART stream + DMA */

static mr_err_t uart_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    if ((events == 0U) || ((events & ~UART_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }
    if (!priv->initialized) {
        return -MR_ENODEV;
    }
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
    if (!enable && (events & MR_UART_WAIT_TX_DONE)
        && (mr_atomic_load(&priv->tx_stream.active) != 0)) {
        return -MR_EBUSY;
    }
#endif /* UART stream + DMA */
    if (enable) {
        priv->event_mask |= events;
    } else {
        priv->event_mask &= ~events;
    }
    uart_interrupts_update(priv);
    return 0;
}

static void uart_irq_handle(uart_priv_t *priv) {
    mr_wait_event_t event = 0U;

    if ((__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_RXNE) != 0U)
        && (__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_RXNE) != 0U)) {
        /* One-shot notification; mr_uart_read() rearms RX_READY afterwards */
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_RXNE);
        event |= MR_UART_WAIT_RX_READY;
    }
    if ((__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_IDLE) != 0U)
        && (__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_IDLE) != 0U)) {
        __HAL_UART_CLEAR_IDLEFLAG(&priv->handle);
        event |= MR_UART_WAIT_RX_IDLE;
    }
    if ((__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_TC) != 0U)
        && (__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_TC) != 0U)) {
        /* Preserve the idle flag for configure; rearm on the next write */
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_TC);
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
        if (mr_atomic_load(&priv->tx_stream.active) != 0) {
            if (!priv->tx_stream.circular
                && (mr_atomic_load(&priv->tx_stream.dma_complete) != 0)) {
                mr_atomic_store(&priv->tx_stream.complete, 1);
                if (!priv->tx_stream.failed
                    && (priv->event_mask & MR_UART_WAIT_TX_DONE)) {
                    event |= MR_UART_WAIT_TX_DONE;
                }
            }
        } else
#endif /* UART stream + DMA */
        {
            if (priv->event_mask & MR_UART_WAIT_TX_DONE) {
                event |= MR_UART_WAIT_TX_DONE;
            }
        }
    }
    if (((__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_PE) != 0U)
         || (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_FE) != 0U)
         || (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_NE) != 0U)
         || (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_ORE) != 0U))
        && ((__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_PE) != 0U)
            || (__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_ERR) != 0U))) {
        __HAL_UART_CLEAR_FLAG(&priv->handle, UART_CLEAR_PEF | UART_CLEAR_FEF
                                                 | UART_CLEAR_NEF
                                                 | UART_CLEAR_OREF);
        event |= MR_UART_WAIT_RX_ERROR;
    }
    if ((event != 0U) && (priv->driver != MR_NULL)) {
        (void)mr_uart_event_raise(priv->driver, event);
    }
}

/* Instance install */
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
#define UART_STREAM_INIT(_id)                                                  \
    .rx_stream.dma = MR_HT_DMA_SPEC(_id, st_rx_dma),                           \
    .tx_stream.dma = MR_HT_DMA_SPEC(_id, st_tx_dma),
#else
#define UART_STREAM_INIT(_id)
#endif /* UART stream + DMA */

/* clang-format off */
#define ST_HT_UART_INSTALL(_id, _ops)                                          \
    static void MR_CAT(__uart_clk_enable_, _id)(void) {                        \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static void MR_CAT(__uart_gpio_clk_enable_, _id)(void) {                   \
        MR_HT_DRIVER_PROP(_id, st_gpio_clken)();                               \
    }                                                                          \
    static uart_priv_t MR_CAT(__uart_priv_, _id) = {                           \
        UART_STREAM_INIT(_id)                                                  \
        .handle.Instance = MR_HT_DRIVER_PROP(_id, st_uartx),                   \
        .clk_enable = MR_CAT(__uart_clk_enable_, _id),                         \
        .gpio_clk_enable = MR_CAT(__uart_gpio_clk_enable_, _id),               \
        .tx = {.port = MR_HT_DRIVER_PROP(_id, st_tx_port),                     \
               .pin = MR_HT_DRIVER_PROP(_id, st_tx_pin),                       \
               .alternate = MR_HT_DRIVER_PROP(_id, st_tx_af)},                 \
        .rx = {.port = MR_HT_DRIVER_PROP(_id, st_rx_port),                     \
               .pin = MR_HT_DRIVER_PROP(_id, st_rx_pin),                       \
               .alternate = MR_HT_DRIVER_PROP(_id, st_rx_af)},                 \
        .irqn = MR_HT_DRIVER_PROP(_id, st_irqn),                               \
        .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority),               \
    };                                                                         \
    void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {                        \
        uart_irq_handle(&MR_CAT(__uart_priv_, _id));                           \
    }                                                                          \
    MR_HT_UART_INSTALL(_id, &MR_CAT(__uart_priv_, _id), _ops)
/* clang-format on */
MR_HT_DRIVER_FOREACH(ST_HT_UART_INSTALL, &__uart_ops);
#endif /* defined(MR_USE_DRIVER_UART) */

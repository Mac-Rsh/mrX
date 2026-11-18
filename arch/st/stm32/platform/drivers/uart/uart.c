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
#if defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#define UART_USE_STREAM
#endif /* defined(MR_USE_DRIVER_UART_STREAM) && defined(MR_USE_DRIVER_DMA) */

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_uart
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "STM32 UART requires the GPIO driver"
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

/* UART capability definition */
#if defined(USART_RDR_RDR) || defined(USART_RDR_RDR_Msk)
#define STM32_UART_HAS_RDR_TDR
#endif /* defined(USART_RDR_RDR) || defined(USART_RDR_RDR_Msk) */
#if defined(UART_CLEAR_PEF) && defined(UART_CLEAR_IDLEF)
#define STM32_UART_HAS_ICR
#endif /* defined(UART_CLEAR_PEF) && defined(UART_CLEAR_IDLEF) */

/* UART pin type */
typedef struct uart_pin {
    mr_ht_gpio_spec_t spec;
    uint32_t alternate;
} uart_pin_t;

#if defined(UART_USE_STREAM)
/* UART stream type */
typedef struct uart_stream {
    mr_ht_dma_spec_t dma;
    mr_dma_transfer_t transfer;
    mr_waiter_t waiter;
    volatile mr_bool_t dma_complete;
    volatile mr_bool_t complete;
    volatile mr_bool_t active;
    mr_uint8_t circular: 1;
    mr_uint8_t failed: 1;
} uart_stream_t;
#endif /* defined(UART_USE_STREAM) */

/* UART private data type */
typedef struct uart_priv {
    UART_HandleTypeDef handle;
    void (*clk_enable)(void);
    uart_pin_t tx;
    uart_pin_t rx;
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    mr_driver_t *driver;
    mr_uint32_t event_mask;
    mr_uart_config_t config;
#if !defined(STM32_UART_HAS_ICR)
    mr_uint8_t rx_byte;
    mr_uint8_t rx_pending: 1;
#endif /* !defined(STM32_UART_HAS_ICR) */
    mr_uint8_t initialized: 1;
#if defined(UART_USE_STREAM)
    uart_stream_t rx_stream;
    uart_stream_t tx_stream;
#endif /* defined(UART_USE_STREAM) */
} uart_priv_t;

/* UART event mask definition */
#define UART_EVENT_BASE_MASK                                                   \
    (MR_UART_WAIT_RX_IDLE | MR_UART_WAIT_RX_READY | MR_UART_WAIT_TX_DONE       \
     | MR_UART_WAIT_RX_ERROR | MR_UART_WAIT_TX_ERROR)
#if defined(UART_USE_STREAM)
#define UART_EVENT_MASK                                                        \
    (UART_EVENT_BASE_MASK | MR_UART_WAIT_RX_STREAM_HALF                        \
     | MR_UART_WAIT_RX_STREAM_FULL | MR_UART_WAIT_TX_STREAM_HALF               \
     | MR_UART_WAIT_TX_STREAM_DONE)
#define UART_DMA_EVENT_MASK                                                    \
    (MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR)
#else
#define UART_EVENT_MASK UART_EVENT_BASE_MASK
#endif /* defined(UART_USE_STREAM) */
/* UART data register definition */
#if defined(STM32_UART_HAS_RDR_TDR)
#define UART_RX_DATA(_priv)             ((_priv)->handle.Instance->RDR)
#define UART_TX_DATA(_priv)             ((_priv)->handle.Instance->TDR)
#else
#define UART_RX_DATA(_priv)             ((_priv)->handle.Instance->DR)
#define UART_TX_DATA(_priv)             ((_priv)->handle.Instance->DR)
#endif /* defined(STM32_UART_HAS_RDR_TDR) */

/* UART parity map */
static const uint32_t __uart_parity[] = {
    UART_PARITY_NONE,
    UART_PARITY_ODD,
    UART_PARITY_EVEN,
};

/* UART mode map */
static const uint32_t __uart_mode[] = {
    UART_MODE_RX,
    UART_MODE_TX,
    UART_MODE_TX_RX,
};

static mr_uint8_t uart_data_read(const uart_priv_t *priv) {
    mr_uint8_t data;

    /* Read UART data */
    data = (mr_uint8_t)UART_RX_DATA(priv);

    /* Mask 7-bit data */
    if (priv->config.data_bits == MR_UART_DATA_BITS_7) {
        data &= 0x7FU;
    }
    return data;
}

#if !defined(STM32_UART_HAS_ICR)
static void uart_rx_preserve(uart_priv_t *priv) {
    /* Check RX state */
    if (priv->rx_pending) {
        return;
    }

#if defined(UART_USE_STREAM)
    /* Check DMA ownership */
    if (priv->rx_stream.active) {
        return;
    }
#endif /* defined(UART_USE_STREAM) */

    /* Check RX data */
    if (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_RXNE) == 0U) {
        return;
    }

    /* Preserve RX data */
    priv->rx_byte = uart_data_read(priv);
    priv->rx_pending = MR_TRUE;
}
#endif /* !defined(STM32_UART_HAS_ICR) */

static void uart_clear_tc(uart_priv_t *priv) {
    /* Clear TX complete */
#if defined(UART_CLEAR_TCF)
    __HAL_UART_CLEAR_FLAG(&priv->handle, UART_CLEAR_TCF);
#else
    __HAL_UART_CLEAR_FLAG(&priv->handle, UART_FLAG_TC);
#endif /* defined(UART_CLEAR_TCF) */
}

static void uart_clear_error(uart_priv_t *priv) {
    /* Clear RX errors */
#if defined(STM32_UART_HAS_ICR)
    __HAL_UART_CLEAR_PEFLAG(&priv->handle);
    __HAL_UART_CLEAR_FEFLAG(&priv->handle);
    __HAL_UART_CLEAR_NEFLAG(&priv->handle);
    __HAL_UART_CLEAR_OREFLAG(&priv->handle);
#else
    __HAL_UART_CLEAR_PEFLAG(&priv->handle);
#endif /* defined(STM32_UART_HAS_ICR) */
}

static mr_err_t uart_make_init(const mr_uart_config_t *config,
                               UART_InitTypeDef *init) {
    /* Check parameter */
    if ((config->baud_rate == 0U) || (config->stop_bits > MR_UART_STOP_BITS_2)
        || (config->parity > MR_UART_PARITY_EVEN)
        || (config->mode > MR_UART_MODE_TX_RX)) {
        return -MR_EINVAL;
    }
    if (config->flow_ctrl != MR_UART_FLOW_CTRL_NONE) {
        return -MR_ENOSYS;
    }
    if ((config->data_bits < MR_UART_DATA_BITS_7)
        || (config->data_bits > MR_UART_DATA_BITS_8)) {
        return -MR_ENOSYS;
    }

    /* Translate word length */
    if (config->data_bits == MR_UART_DATA_BITS_7) {
        if (config->parity == MR_UART_PARITY_NONE) {
#if defined(UART_WORDLENGTH_7B)
            init->WordLength = UART_WORDLENGTH_7B;
#else
            return -MR_ENOSYS;
#endif /* defined(UART_WORDLENGTH_7B) */
        } else {
            init->WordLength = UART_WORDLENGTH_8B;
        }
    } else {
        init->WordLength = (config->parity == MR_UART_PARITY_NONE)
                             ? UART_WORDLENGTH_8B
                             : UART_WORDLENGTH_9B;
    }

    /* Translate stop bits */
    switch (config->stop_bits) {
        case MR_UART_STOP_BITS_1: {
            init->StopBits = UART_STOPBITS_1;
            break;
        }
        case MR_UART_STOP_BITS_1_5: {
#if defined(UART_STOPBITS_1_5)
            init->StopBits = UART_STOPBITS_1_5;
            break;
#else
            return -MR_ENOSYS;
#endif /* defined(UART_STOPBITS_1_5) */
        }
        case MR_UART_STOP_BITS_2: {
            init->StopBits = UART_STOPBITS_2;
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Init UART configuration */
    init->BaudRate = config->baud_rate;
    init->Parity = __uart_parity[config->parity];
    init->Mode = __uart_mode[config->mode];
    init->HwFlowCtl = UART_HWCONTROL_NONE;
#if defined(UART_OVERSAMPLING_16)
    init->OverSampling = UART_OVERSAMPLING_16;
#endif /* defined(UART_OVERSAMPLING_16) */
#if defined(UART_ONE_BIT_SAMPLE_DISABLE)
    init->OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
#endif /* defined(UART_ONE_BIT_SAMPLE_DISABLE) */
#if defined(UART_PRESCALER_DIV1)
    init->ClockPrescaler = UART_PRESCALER_DIV1;
#endif /* defined(UART_PRESCALER_DIV1) */
    return 0;
}

static void uart_interrupts_update(uart_priv_t *priv) {
    mr_bool_t rx_ready_enable, tc_enable;

    /* Update RX ready interrupt */
    rx_ready_enable = ((priv->event_mask & MR_UART_WAIT_RX_READY) != 0U);
#if defined(UART_USE_STREAM)
    if (priv->rx_stream.active) {
        rx_ready_enable = MR_FALSE;
    }
#endif /* defined(UART_USE_STREAM) */
    if (rx_ready_enable) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_RXNE);
    } else {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_RXNE);
    }

    /* Update RX idle interrupt */
    if (priv->event_mask & MR_UART_WAIT_RX_IDLE) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_IDLE);
    } else {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_IDLE);
    }

    /* Update TX complete interrupt */
    tc_enable = ((priv->event_mask & MR_UART_WAIT_TX_DONE) != 0U)
             && (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_TC) == 0U);
#if defined(UART_USE_STREAM)
    if (priv->tx_stream.active) {
        tc_enable = !priv->tx_stream.circular && priv->tx_stream.dma_complete
                 && !priv->tx_stream.complete;
    }
#endif /* defined(UART_USE_STREAM) */
    if (tc_enable) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_TC);
    } else {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_TC);
    }

    /* Update RX error interrupt */
    if (priv->event_mask & MR_UART_WAIT_RX_ERROR) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_PE);
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_ERR);
    } else {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_PE);
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_ERR);
    }
}
static mr_err_t uart_pin_check(const uart_pin_t *pin) {
    /* Check pin */
    if ((pin->spec.drv == MR_NULL) || (pin->spec.pin >= 16U)
        || (pin->alternate > 15U)) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_err_t uart_gpio_configure(uart_priv_t *priv, mr_uint8_t mode,
                                    mr_bool_t reconfigure) {
    mr_err_t ret;

    /* Release unused pins */
    if (reconfigure && (priv->config.mode != MR_UART_MODE_RX)
        && (mode == MR_UART_MODE_RX)) {
        ret = mr_stm32_gpio_pin_release(&priv->tx.spec);
        if (ret != 0) {
            return ret;
        }
    }
    if (reconfigure && (priv->config.mode != MR_UART_MODE_TX)
        && (mode == MR_UART_MODE_TX)) {
        ret = mr_stm32_gpio_pin_release(&priv->rx.spec);
        if (ret != 0) {
            return ret;
        }
    }

    /* Configure UART pins */
    if (!reconfigure || (priv->config.mode != mode)) {
        if (mode != MR_UART_MODE_RX) {
            ret = mr_stm32_gpio_pin_alternate(
                &priv->tx.spec, priv->tx.alternate, MR_GPIO_PULL_NONE,
                MR_GPIO_OTYPE_PP);
            if (ret != 0) {
                return ret;
            }
        }
        if (mode != MR_UART_MODE_TX) {
            ret = mr_stm32_gpio_pin_alternate(
                &priv->rx.spec, priv->rx.alternate, MR_GPIO_PULL_NONE,
                MR_GPIO_OTYPE_PP);
            if (ret != 0) {
                return ret;
            }
        }
    }
    return 0;
}

static mr_err_t uart_configure(const mr_driver_t *drv,
                               const mr_uart_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    UART_InitTypeDef init;
    mr_bool_t reconfigure;
    mr_err_t ret;

    /* Check UART resources */
    if ((priv->handle.Instance == MR_NULL) || (priv->clk_enable == MR_NULL)) {
        return -MR_EINVAL;
    }

    /* Check UART state */
#if defined(UART_USE_STREAM)
    if (priv->rx_stream.active || priv->tx_stream.active) {
        return -MR_EBUSY;
    }
#endif /* defined(UART_USE_STREAM) */
    if (priv->initialized && (priv->config.mode != MR_UART_MODE_RX)
        && (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_TC) == 0U)) {
        return -MR_EBUSY;
    }

    /* Build UART configuration */
    init = (UART_InitTypeDef){0};
    ret = uart_make_init(config, &init);
    if (ret != 0) {
        return ret;
    }

    /* Check UART pins */
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

    /* Get UART state */
    reconfigure = priv->initialized;

    /* Enable UART clock */
    priv->clk_enable();

    /* Stop UART */
    if (priv->initialized) {
        ret = mr_stm32_hal_status(HAL_UART_DeInit(&priv->handle));
        if (ret != 0) {
            return ret;
        }
        priv->initialized = MR_FALSE;
    }

    /* Configure GPIO */
    ret = uart_gpio_configure(priv, config->mode, reconfigure);
    if (ret != 0) {
        return ret;
    }

    /* Init UART */
    priv->handle.Init = init;
#if defined(UART_ADVFEATURE_NO_INIT)
    priv->handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
#endif /* defined(UART_ADVFEATURE_NO_INIT) */
#if defined(UART_FIFOMODE_DISABLE)
    priv->handle.FifoMode = UART_FIFOMODE_DISABLE;
#endif /* defined(UART_FIFOMODE_DISABLE) */
    ret = mr_stm32_hal_status(HAL_UART_Init(&priv->handle));
    if (ret != 0) {
        priv->initialized = MR_FALSE;
        return ret;
    }

    /* Update UART state */
    priv->config = *config;
    priv->driver = (mr_driver_t *)drv;
#if !defined(STM32_UART_HAS_ICR)
    priv->rx_pending = MR_FALSE;
#endif /* !defined(STM32_UART_HAS_ICR) */
    priv->initialized = MR_TRUE;

    /* Init UART IRQ */
    if (!reconfigure) {
        HAL_NVIC_SetPriority(priv->irqn, priv->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(priv->irqn);
    }

    /* Update UART interrupt */
    uart_interrupts_update(priv);
    return 0;
}

static mr_err_t uart_configuration(const mr_driver_t *drv,
                                   mr_uart_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check UART state */
    if (!priv->initialized) {
        return -MR_ENODEV;
    }

    /* Get UART configuration */
    *config = priv->config;
    return 0;
}

static mr_ssize_t uart_read(const mr_driver_t *drv, void *buf, mr_size_t size) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_size_t completed;
    mr_uint8_t *dst;

    /* Check RX state */
    if (!priv->initialized) {
        return -MR_ENODEV;
    }
#if defined(UART_USE_STREAM)
    if (priv->rx_stream.active) {
        return -MR_EBUSY;
    }
#endif /* defined(UART_USE_STREAM) */

    /* Read received data */
    dst = (mr_uint8_t *)buf;
    completed = 0U;
#if !defined(STM32_UART_HAS_ICR)
    if (priv->rx_pending && (size != 0U)) {
        *dst++ = priv->rx_byte;
        priv->rx_pending = MR_FALSE;
        completed++;
    }
#endif /* !defined(STM32_UART_HAS_ICR) */
    while ((completed < size)
           && (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_RXNE) != 0U)) {
        *dst++ = uart_data_read(priv);
        completed++;
    }

    /* Update UART interrupt */
    uart_interrupts_update(priv);
    return (mr_ssize_t)completed;
}

static mr_ssize_t uart_write(const mr_driver_t *drv, const void *buf,
                             mr_size_t size) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uint8_t *src;
    mr_size_t completed;

    /* Check TX state */
    if (!priv->initialized) {
        return -MR_ENODEV;
    }
#if defined(UART_USE_STREAM)
    if (priv->tx_stream.active) {
        return -MR_EBUSY;
    }
#endif /* defined(UART_USE_STREAM) */

    /* Write transmit data */
    src = (const mr_uint8_t *)buf;
    completed = 0U;
    while ((completed < size)
           && (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_TXE) != 0U)) {
        UART_TX_DATA(priv) = *src++;
        completed++;
    }

    /* Update TX complete interrupt */
    if ((completed != 0U) && (priv->event_mask & MR_UART_WAIT_TX_DONE)) {
        __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_TC);
    }
    return (mr_ssize_t)completed;
}

#if defined(UART_USE_STREAM)
static void uart_stream_dma_event(uart_priv_t *priv, uart_stream_t *stream,
                                  mr_wait_event_t event, uint32_t dma_mask,
                                  mr_bool_t receive) {
    mr_wait_event_t uart_event;

    uart_event = 0U;

    /* Check stream state */
    if (!stream->active) {
        return;
    }
    if (event & MR_DMA_WAIT_ERROR) {
        /* Stop UART DMA requests; defer waiter teardown out of IRQ */
        CLEAR_BIT(priv->handle.Instance->CR3, dma_mask);
        if (!receive) {
            stream->failed = MR_TRUE;
        }
        stream->complete = MR_TRUE;
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
                stream->complete = MR_TRUE;
            } else {
                /* TC is final only after DMA has supplied the last byte */
                stream->dma_complete = MR_TRUE;
                __HAL_UART_ENABLE_IT(&priv->handle, UART_IT_TC);
            }
        }
    }
    if (uart_event != 0U) {
        mr_uart_event_raise(priv->driver, uart_event);
    }
}

static void uart_rx_dma_event(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    uart_priv_t *priv;

    priv = (uart_priv_t *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(data);
    uart_stream_dma_event(priv, &priv->rx_stream, event, USART_CR3_DMAR,
                          MR_TRUE);
}

static void uart_tx_dma_event(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    uart_priv_t *priv;

    priv = (uart_priv_t *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(data);
    uart_stream_dma_event(priv, &priv->tx_stream, event, USART_CR3_DMAT,
                          MR_FALSE);
}

static mr_err_t uart_stream_stop(uart_priv_t *priv, uart_stream_t *stream,
                                 uint32_t dma_mask) {
    mr_err_t ret;

    /* Check stream state */
    if (!stream->active) {
        return 0;
    }

    /* Stop UART DMA request */
    CLEAR_BIT(priv->handle.Instance->CR3, dma_mask);

    /* Stop DMA */
    ret = mr_ht_dma_stop(&stream->dma);
    if (ret != 0) {
        return ret;
    }

    /* Disable DMA event */
    ret = mr_ht_dma_event_disable(&stream->dma, UART_DMA_EVENT_MASK);
    if (ret != 0) {
        return ret;
    }

    /* Release waiter */
    mr_waiter_del(&stream->waiter);

    /* Reset stream state */
    stream->active = MR_FALSE;
    stream->complete = MR_FALSE;
    stream->dma_complete = MR_FALSE;
    stream->failed = MR_FALSE;

    /* Update UART interrupt */
    if ((dma_mask == USART_CR3_DMAT)
        && ((priv->event_mask & MR_UART_WAIT_TX_DONE) == 0U)) {
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_TC);
    }
    if (dma_mask == USART_CR3_DMAR) {
        uart_interrupts_update(priv);
    }
    return 0;
}

static mr_err_t uart_stream_is_busy(uart_stream_t *stream, mr_bool_t *busy) {
    /* Check stream state */
    if (!stream->active || stream->complete) {
        *busy = MR_FALSE;
        return 0;
    }

    /* Get DMA state */
    return mr_ht_dma_is_busy(&stream->dma, busy);
}

static mr_err_t uart_stream_start(const mr_driver_t *drv, uart_stream_t *stream,
                                  const void *src, void *dst, mr_size_t size,
                                  mr_bool_t circular, mr_bool_t receive) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_dma_config_t dma_config;
    uint32_t dma_mask;
    mr_err_t ret;

    /* Supported 7/8-bit payloads use byte DMA access to RDR/TDR/DR. */
    dma_config = (mr_dma_config_t){
        .src_width = MR_DMA_WIDTH_8,
        .dst_width = MR_DMA_WIDTH_8,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
    };
    dma_mask = receive ? USART_CR3_DMAR : USART_CR3_DMAT;

    /* Check UART state */
    if (!priv->initialized) {
        return -MR_ENODEV;
    }

    /* Check DMA */
    if (stream->dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Check UART mode */
    if ((receive && (priv->config.mode == MR_UART_MODE_TX))
        || (!receive && (priv->config.mode == MR_UART_MODE_RX))) {
        return -MR_EINVAL;
    }

    /* DMA cannot strip the parity bit from a seven-bit payload */
    if (receive && (priv->config.data_bits == MR_UART_DATA_BITS_7)
        && (priv->config.parity != MR_UART_PARITY_NONE)) {
        return -MR_ENOSYS;
    }

    /* Check stream state */
    if (stream->active) {
        if (!stream->complete) {
            return -MR_EBUSY;
        }
        ret = uart_stream_stop(priv, stream, dma_mask);
        if (ret != 0) {
            return ret;
        }
    }

    /* Configure DMA */
    dma_config.src_adj = receive ? MR_DMA_ADJ_CONST : MR_DMA_ADJ_INC;
    dma_config.dst_adj = receive ? MR_DMA_ADJ_INC : MR_DMA_ADJ_CONST;
    dma_config.direction = receive ? MR_DMA_DIR_P2M : MR_DMA_DIR_M2P;
    ret = mr_ht_dma_configure(&stream->dma, &dma_config);
    if (ret != 0) {
        return ret;
    }

    /* Init stream state */
    stream->circular = (circular != MR_FALSE);
    stream->failed = MR_FALSE;
    stream->complete = MR_FALSE;
    stream->dma_complete = MR_FALSE;

    /* Init waiter */
    ret = mr_waiter_init(&stream->waiter,
                         receive ? uart_rx_dma_event : uart_tx_dma_event, priv,
                         UART_DMA_EVENT_MASK);
    if (ret != 0) {
        return ret;
    }

    /* Attach DMA event */
    ret = mr_ht_dma_event_attach(&stream->dma, &stream->waiter);
    if (ret != 0) {
        goto _release_waiter;
    }
    ret = mr_ht_dma_event_enable(&stream->dma, UART_DMA_EVENT_MASK);
    if (ret != 0) {
        goto _release_waiter;
    }

    /* Start DMA */
    stream->transfer.next = circular ? &stream->transfer : MR_NULL;
    stream->transfer.src = src;
    stream->transfer.dst = dst;
    stream->transfer.count = size;
    ret = mr_ht_dma_start(&stream->dma, &stream->transfer);
    if (ret != 0) {
        mr_ht_dma_event_disable(&stream->dma, UART_DMA_EVENT_MASK);
        goto _release_waiter;
    }

    /* Activate stream */
    stream->active = MR_TRUE;
    uart_interrupts_update(priv);

    /* Start UART DMA request */
    if (!receive) {
        uart_clear_tc(priv);
    }
    SET_BIT(priv->handle.Instance->CR3, dma_mask);
    return 0;

_release_waiter:
    /* Release waiter */
    mr_waiter_del(&stream->waiter);
    return ret;
}

static mr_err_t uart_rx_stream_start(const mr_driver_t *drv, void *buf,
                                     mr_size_t size,
                                     const mr_uart_rx_stream_config_t *config) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    return uart_stream_start(drv, &priv->rx_stream,
                             (const void *)&UART_RX_DATA(priv), buf, size,
                             config->circular, MR_TRUE);
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

    /* Get RX stream offset */
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
                             (void *)&UART_TX_DATA(priv), size,
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
    if (!priv->tx_stream.active) {
        *busy = MR_FALSE;
        return 0;
    }
    if (!priv->tx_stream.complete) {
        *busy = MR_TRUE;
        return 0;
    }
    *busy = MR_FALSE;
    return uart_tx_stream_stop(drv);
}

static mr_err_t uart_tx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get TX stream offset */
    if (priv->tx_stream.dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    return mr_ht_dma_offset(&priv->tx_stream.dma, offset);
}
#endif /* defined(UART_USE_STREAM) */

static mr_err_t uart_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable) {
    uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if ((events == 0U) || ((events & ~UART_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check UART state */
    if (!priv->initialized) {
        return -MR_ENODEV;
    }
#if defined(UART_USE_STREAM)
    /* Check TX stream */
    if (!enable && (events & MR_UART_WAIT_TX_DONE) && priv->tx_stream.active) {
        return -MR_EBUSY;
    }
#endif /* defined(UART_USE_STREAM) */

    /* Update event mask */
    if (enable) {
        priv->event_mask |= events;
    } else {
        priv->event_mask &= ~events;
    }

    /* Update UART interrupt */
    uart_interrupts_update(priv);
    return 0;
}

static void uart_irq_handle(uart_priv_t *priv) {
    mr_wait_event_t event;

    event = 0U;

    /* Handle RX ready */
    if ((__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_RXNE) != 0U)
        && (__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_RXNE) != 0U)) {
        /* Mask RX ready */
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_RXNE);
        event |= MR_UART_WAIT_RX_READY;
    }

    /* Handle RX idle */
    if ((__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_IDLE) != 0U)
        && (__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_IDLE) != 0U)) {
#if !defined(STM32_UART_HAS_ICR)
        uart_rx_preserve(priv);
#endif /* !defined(STM32_UART_HAS_ICR) */
        __HAL_UART_CLEAR_IDLEFLAG(&priv->handle);
        event |= MR_UART_WAIT_RX_IDLE;
    }

    /* Handle TX complete */
    if ((__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_TC) != 0U)
        && (__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_TC) != 0U)) {
        /* Keep TC for reconfiguration */
        __HAL_UART_DISABLE_IT(&priv->handle, UART_IT_TC);
#if defined(UART_USE_STREAM)
        if (priv->tx_stream.active) {
            if (!priv->tx_stream.circular && priv->tx_stream.dma_complete) {
                priv->tx_stream.complete = MR_TRUE;
                if (!priv->tx_stream.failed
                    && (priv->event_mask & MR_UART_WAIT_TX_DONE)) {
                    event |= MR_UART_WAIT_TX_DONE;
                }
            }
        } else
#endif /* defined(UART_USE_STREAM) */
        {
            if (priv->event_mask & MR_UART_WAIT_TX_DONE) {
                event |= MR_UART_WAIT_TX_DONE;
            }
        }
    }

    /* Handle RX error */
    if (((__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_PE) != 0U)
         || (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_FE) != 0U)
         || (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_NE) != 0U)
         || (__HAL_UART_GET_FLAG(&priv->handle, UART_FLAG_ORE) != 0U))
        && ((__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_PE) != 0U)
            || (__HAL_UART_GET_IT_SOURCE(&priv->handle, UART_IT_ERR) != 0U))) {
#if !defined(STM32_UART_HAS_ICR)
        uart_rx_preserve(priv);
#endif /* !defined(STM32_UART_HAS_ICR) */
        uart_clear_error(priv);
        event |= MR_UART_WAIT_RX_ERROR;
    }

    /* Raise UART event */
    if ((event != 0U) && (priv->driver != MR_NULL)) {
        mr_uart_event_raise(priv->driver, event);
    }
}

/* Instance install */
#define ST_HT_UART_INSTALL(_id, _ops)                                          \
    static void MR_CAT(__uart_clk_enable_, _id)(void) {                        \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static uart_priv_t MR_CAT(__uart_priv_, _id) = {                           \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_uartx),                      \
        },                                                                     \
        .clk_enable = MR_CAT(__uart_clk_enable_, _id),                         \
        .tx = __ST_HT_UART_PIN(_id, st_tx, st_tx_af),                          \
        .rx = __ST_HT_UART_PIN(_id, st_rx, st_rx_af),                          \
        .irqn = MR_HT_DRIVER_PROP(_id, st_irqn),                               \
        .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority),               \
        __ST_HT_UART_STREAM_INIT(_id)                                          \
    };                                                                         \
    void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {                        \
        uart_irq_handle(&MR_CAT(__uart_priv_, _id));                           \
    }                                                                          \
    MR_HT_UART_INSTALL(_id, &MR_CAT(__uart_priv_, _id), _ops)
#define __ST_HT_UART_PIN(_id, _prop, _af_prop)                                 \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, _prop),                             \
                ({                                                             \
                    .spec = MR_HT_GPIO_SPEC(_id, _prop),                       \
                    .alternate = MR_HT_DRIVER_PROP_OR(_id, _af_prop, 0),       \
                }),                                                            \
                ({                                                             \
                    .spec = {0},                                               \
                    .alternate = 0,                                            \
                }))
#if defined(UART_USE_STREAM)
#define __ST_HT_UART_STREAM_INIT(_id)                                          \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_rx_dma),                         \
                (.rx_stream = {                                                \
                    .dma = MR_HT_DMA_SPEC(_id, st_rx_dma),                     \
                }),                                                            \
                (.rx_stream = {0})),                                           \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_tx_dma),                         \
                (.tx_stream = {                                                \
                    .dma = MR_HT_DMA_SPEC(_id, st_tx_dma),                     \
                }),                                                            \
                (.tx_stream = {0})),
#else
#define __ST_HT_UART_STREAM_INIT(_id)
#endif /* defined(UART_USE_STREAM) */
MR_HT_DRIVER_FOREACH(ST_HT_UART_INSTALL, &__uart_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_UART) */

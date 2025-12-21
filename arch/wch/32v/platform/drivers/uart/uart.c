/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-12-20        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_UART)
#include <mrX/drivers/uart/mr_uart.h>

/* Hard tree compatible */
#define MR_HT_COMPAT                    wch_ch32v_uart

/* Driver UART operations */
static mr_err_t drv_uart_configure(const mr_driver_t *drv,
                                   const mr_uart_config_t *config);
static mr_err_t drv_uart_write(const mr_driver_t *drv, mr_uint8_t data);
static mr_err_t drv_uart_read(const mr_driver_t *drv, mr_uint8_t *data);
static mr_err_t drv_uart_tx_int_control(const mr_driver_t *drv,
                                        mr_bool_t enable);
static mr_err_t drv_uart_tx_int_is_busy(const mr_driver_t *drv,
                                        mr_bool_t *busy);
static mr_err_t drv_uart_rx_int_control(const mr_driver_t *drv,
                                        mr_bool_t enable);
static mr_err_t drv_uart_rx_int_is_busy(const mr_driver_t *drv,
                                        mr_bool_t *busy);
static const mr_uart_ops_t __drv_uart_ops
    = {.configure = drv_uart_configure,
       .write = drv_uart_write,
       .read = drv_uart_read,
       .tx_int_control = drv_uart_tx_int_control,
       .tx_int_is_busy = drv_uart_tx_int_is_busy,
       .rx_int_control = drv_uart_rx_int_control,
       .rx_int_is_busy = drv_uart_rx_int_is_busy};

/* Driver UART private data type */
typedef struct drv_uart_priv {
    USART_TypeDef *UARTx;
    uint32_t RCCx;
    GPIO_TypeDef *TX_GPIOx;
    uint32_t TX_GPIOx_RCCx;
    uint32_t TX_GPIOx_PINx;
    GPIO_TypeDef *RX_GPIOx;
    uint32_t RX_GPIOx_RCCx;
    uint32_t RX_GPIOx_PINx;
    uint32_t TX_RX_GPIOx_remap;
    IRQn_Type irqn;
    uint8_t irq_prio_preempt;
    uint8_t irq_prio_sub;
    uint8_t tx_irq_enable;
    uint8_t rx_irq_enable;
} drv_uart_priv_t;

/* UART irq map */
static const mr_driver_t *__drv_uart_irq_map[8];

static uint8_t UARTx2IRQmap(USART_TypeDef *UARTx) {
#if defined(USART1)
    if (UARTx == USART1) {
        return 0;
    }
#endif /* defined(USART1) */
#if defined(USART2)
    if (UARTx == USART2) {
        return 1;
    }
#endif /* defined(USART2) */
#if defined(USART3)
    if (UARTx == USART3) {
        return 2;
    }
#endif /* defined(USART3) */
#if defined(UART4)
    if (UARTx == UART4) {
        return 3;
    }
#endif /* defined(UART4) */
#if defined(UART5)
    if (UARTx == UART5) {
        return 4;
    }
#endif /* defined(UART5) */
#if defined(UART6)
    if (UARTx == UART6) {
        return 5;
    }
#endif /* defined(UART6) */
#if defined(UART7)
    if (UARTx == UART7) {
        return 6;
    }
#endif /* defined(UART7) */
#if defined(UART8)
    if (UARTx == UART8) {
        return 7;
    }
#endif /* defined(UART8) */
    return 0;
}

static mr_err_t drv_uart_configure(const mr_driver_t *drv,
                                   const mr_uart_config_t *config) {
    drv_uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    USART_InitTypeDef USART_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* Check param*/
    MR_ASSERT((priv->TX_GPIOx != NULL) || (priv->RX_GPIOx != NULL));
    MR_ASSERT(priv->UARTx != NULL);

    if (config->baud_rate > 0) {
        switch (config->data_bits) {
            case MR_UART_DATA_BITS_8: {
                USART_InitStructure.USART_WordLength = USART_WordLength_8b;
                break;
            }
            default: {
                return -MR_EPERM;
            }
        }

        switch (config->stop_bits) {
            case MR_UART_STOP_BITS_0_5: {
                USART_InitStructure.USART_StopBits = USART_StopBits_0_5;
                break;
            }
            case MR_UART_STOP_BITS_1: {
                USART_InitStructure.USART_StopBits = USART_StopBits_1;
                break;
            }
            case MR_UART_STOP_BITS_1_5: {
                USART_InitStructure.USART_StopBits = USART_StopBits_1_5;
                break;
            }
            case MR_UART_STOP_BITS_2: {
                USART_InitStructure.USART_StopBits = USART_StopBits_2;
                break;
            }
            default: {
                return -MR_EPERM;
            }
        }

        switch (config->parity) {
            case MR_UART_PARITY_NONE: {
                USART_InitStructure.USART_Parity = USART_Parity_No;
                break;
            }
            case MR_UART_PARITY_ODD: {
                USART_InitStructure.USART_Parity = USART_Parity_Odd;
                break;
            }
            case MR_UART_PARITY_EVEN: {
                USART_InitStructure.USART_Parity = USART_Parity_Even;
                break;
            }
            default: {
                return -MR_EPERM;
            }
        }

        switch (config->bits_order) {
            case MR_UART_BITS_ORDER_LSB: {
                break;
            }
            default: {
                return -MR_EPERM;
            }
        }

        /* Configure TX&RX GPIO */
        if (priv->TX_RX_GPIOx_remap > 0) {
            RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
            GPIO_PinRemapConfig(priv->TX_RX_GPIOx_remap, ENABLE);
        }
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        if (priv->TX_GPIOx != 0) {
            RCC_APB2PeriphClockCmd(priv->TX_GPIOx_RCCx, ENABLE);
            GPIO_InitStructure.GPIO_Pin = priv->TX_GPIOx_PINx;
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
            GPIO_Init(priv->TX_GPIOx, &GPIO_InitStructure);
            USART_InitStructure.USART_Mode = USART_Mode_Tx;
        }
        if (priv->RX_GPIOx != 0) {
            RCC_APB2PeriphClockCmd(priv->RX_GPIOx_RCCx, ENABLE);
            GPIO_InitStructure.GPIO_Pin = priv->RX_GPIOx_PINx;
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
            GPIO_Init(priv->RX_GPIOx, &GPIO_InitStructure);
            USART_InitStructure.USART_Mode |= USART_Mode_Rx;
        }
    } else {
        /* Reset TX&RX GPIO */
        if (priv->TX_RX_GPIOx_remap > 0) {
            GPIO_PinRemapConfig(priv->TX_RX_GPIOx_remap, DISABLE);
        }
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        if (priv->TX_GPIOx != 0) {
            GPIO_InitStructure.GPIO_Pin = priv->TX_GPIOx_PINx;
            GPIO_Init(priv->TX_GPIOx, &GPIO_InitStructure);
        }
        if (priv->RX_GPIOx != 0) {
            GPIO_InitStructure.GPIO_Pin = priv->RX_GPIOx_PINx;
            GPIO_Init(priv->RX_GPIOx, &GPIO_InitStructure);
        }
    }

    /* Configure NVIC */
    if (priv->irqn > 0) {
        __drv_uart_irq_map[UARTx2IRQmap(priv->UARTx)] = drv;
        if (config->baud_rate > 0) {
            NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        } else {
            NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
        }
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = priv->irq_prio_preempt;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority
            = priv->irq_prio_sub;
        NVIC_InitStructure.NVIC_IRQChannel = priv->irqn;
        USART_ClearITPendingBit(priv->UARTx, USART_IT_RXNE);
        NVIC_Init(&NVIC_InitStructure);
    }

    /* Configure UART */
    if (config->baud_rate > 0) {
        if ((uint32_t)priv->UARTx < APB2PERIPH_BASE) {
            RCC_APB1PeriphClockCmd(priv->RCCx, ENABLE);
        } else {
            RCC_APB2PeriphClockCmd(priv->RCCx, ENABLE);
        }
        USART_InitStructure.USART_BaudRate = config->baud_rate;
        USART_InitStructure.USART_HardwareFlowControl = 0;
        USART_Init(priv->UARTx, &USART_InitStructure);
        USART_ClearFlag(priv->UARTx, USART_FLAG_RXNE);
        USART_ClearFlag(priv->UARTx, USART_FLAG_TXE);
        USART_Cmd(priv->UARTx, ENABLE);
    } else {
        if ((uint32_t)priv->UARTx < APB2PERIPH_BASE) {
            RCC_APB1PeriphClockCmd(priv->RCCx, DISABLE);
        } else {
            RCC_APB2PeriphClockCmd(priv->RCCx, DISABLE);
        }
        USART_ITConfig(priv->UARTx, USART_IT_RXNE, DISABLE);
        USART_ITConfig(priv->UARTx, USART_IT_TXE, DISABLE);
        priv->tx_irq_enable = MR_FALSE;
        priv->rx_irq_enable = MR_FALSE;
        USART_Cmd(priv->UARTx, DISABLE);
    }
    return 0;
}

static mr_err_t drv_uart_write(const mr_driver_t *drv, mr_uint8_t data) {
    drv_uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Write UART data */
    while (USART_GetFlagStatus(priv->UARTx, USART_FLAG_TXE) == RESET) {
    }
    USART_SendData(priv->UARTx, data);
    return 0;
}

static mr_err_t drv_uart_read(const mr_driver_t *drv, mr_uint8_t *data) {
    drv_uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read UART data */
    while (USART_GetFlagStatus(priv->UARTx, USART_FLAG_RXNE) == RESET) {
    }
    *data = (uint8_t)USART_ReceiveData(priv->UARTx);
    return 0;
}

static mr_err_t drv_uart_tx_int_control(const mr_driver_t *drv,
                                        mr_bool_t enable) {
    drv_uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Enable UART TX INT */
    priv->tx_irq_enable = enable;
    USART_ClearITPendingBit(priv->UARTx, USART_IT_TXE);
    USART_ITConfig(priv->UARTx, USART_IT_TXE, enable);
    return 0;
}

static mr_err_t drv_uart_tx_int_is_busy(const mr_driver_t *drv,
                                        mr_bool_t *busy) {
    drv_uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get UART TX status */
    *busy = priv->tx_irq_enable;
    return 0;
}

static mr_err_t drv_uart_rx_int_control(const mr_driver_t *drv,
                                        mr_bool_t enable) {
    drv_uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Enable UART RX INT */
    priv->rx_irq_enable = enable;
    USART_ClearITPendingBit(priv->UARTx, USART_IT_RXNE);
    USART_ITConfig(priv->UARTx, USART_IT_RXNE, enable);
    return 0;
}

static mr_err_t drv_uart_rx_int_is_busy(const mr_driver_t *drv,
                                        mr_bool_t *busy) {
    drv_uart_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get UART RX status */
    *busy = priv->rx_irq_enable;
    return 0;
}

static void drv_uart_irq_handle(mr_uint32_t index) {
    mr_driver_t *drv = (mr_driver_t *)__drv_uart_irq_map[index];
    drv_uart_priv_t *priv;

    /* Call IRQ handler */
    if (drv != MR_NULL) {
        priv = MR_DRIVER_PRIV(drv);
        if (USART_GetITStatus(priv->UARTx, USART_IT_TXE) != RESET) {
            mr_uart_irq_handle(drv, MR_UART_IRQ_TYPE_TX_INT, 1U);
            USART_ClearITPendingBit(priv->UARTx, USART_IT_TXE);
        }
        if (USART_GetITStatus(priv->UARTx, USART_IT_RXNE) != RESET) {
            mr_uart_irq_handle(drv, MR_UART_IRQ_TYPE_RX_INT, 1U);
            USART_ClearITPendingBit(priv->UARTx, USART_IT_RXNE);
        }
    }
}

#if defined(USART1)
void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART1_IRQHandler(void) {
    drv_uart_irq_handle(0);
}
#endif /* defined(USART1) */
#if defined(USART2)
void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void) {
    drv_uart_irq_handle(1);
}
#endif /* defined(USART2) */
#if defined(USART3)
void USART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART3_IRQHandler(void) {
    drv_uart_irq_handle(2);
}
#endif /* defined(USART3) */
#if defined(UART4)
void UART4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART4_IRQHandler(void) {
    drv_uart_irq_handle(3);
}
#endif /* defined(UART4) */
#if defined(UART5)
void UART5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART5_IRQHandler(void) {
    drv_uart_irq_handle(4);
}
#endif /* defined(UART5) */
#if defined(UART6)
void UART6_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART6_IRQHandler(void) {
    drv_uart_irq_handle(5);
}
#endif /* defined(UART6) */
#if defined(UART7)
void UART7_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART7_IRQHandler(void) {
    drv_uart_irq_handle(6);
}
#endif /* defined(UART7) */
#if defined(UART8)
void UART8_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART8_IRQHandler(void) {
    drv_uart_irq_handle(7);
}
#endif /* defined(UART8) */

/* Instance install */
#define WCH_UART_HT_INST_UART_INSTALL(_id, _ops)                               \
    static drv_uart_priv_t __drv_uart_priv_##_id                               \
        = {.UARTx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), base),             \
           .RCCx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), clk),               \
           .TX_GPIOx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), tx_gpio),       \
           .TX_GPIOx_RCCx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), tx_clk),   \
           .TX_GPIOx_PINx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), tx_pin),   \
           .RX_GPIOx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), rx_gpio),       \
           .RX_GPIOx_RCCx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), rx_clk),   \
           .RX_GPIOx_PINx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), rx_pin),   \
           .TX_RX_GPIOx_remap                                                  \
           = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), tx_rx_remap),             \
           .irqn = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), irqn),              \
           .irq_prio_preempt                                                   \
           = MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), prio_preempt, 1),      \
           .irq_prio_sub                                                       \
           = MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), prio_sub, 1)};         \
    MR_HT_INST_UART_INSTALL(_id, &__drv_uart_priv_##_id, _ops)
MR_HT_INST_DRIVER_FOREACH(WCH_UART_HT_INST_UART_INSTALL, &__drv_uart_ops);
#endif /* defined (MR_USE_DRIVER_UART) */

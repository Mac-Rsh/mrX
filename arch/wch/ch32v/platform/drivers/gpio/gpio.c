/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-17        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32v_gpio

/* Driver GPIO operations */
static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config);
static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_config_t *config);
static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value);
static mr_err_t drv_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint8_t *value);
static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask);
static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask);
static mr_err_t drv_irq_attach(const mr_driver_t *drv, mr_uint32_t pin,
                               mr_waiter_t *entry);
MR_UNUSED static const mr_gpio_ops_t __drv_gpio_ops = {
    .pin_configure = drv_pin_configure,
    .pin_configuration = drv_pin_configuration,
    .pin_write = drv_pin_write,
    .pin_read = drv_pin_read,
    .port_write = drv_port_write,
    .port_read = drv_port_read,
    .attach = drv_irq_attach,
};

/* Driver GPIO private data type */
typedef struct drv_gpio_priv {
    GPIO_TypeDef *GPIOx;
    uint32_t RCCx;
} drv_gpio_priv_t;

/* GPIO irq map type */
typedef struct drv_gpio_irq_map {
    const mr_driver_t *drv;
    mr_uint8_t mode;
} drv_gpio_irq_map_t;

/* GPIO irq map */
static drv_gpio_irq_map_t __drv_gpio_irq_map[16];
/* GPIO exti waitable */
static mr_waitable_t __drv_gpio_exti[16];

/* GPIO irqn */
#if defined(MR_USE_WCH_SERIES_V00X)
static IRQn_Type __drv_gpio_irqn[]
    = {EXTI7_0_IRQn, EXTI7_0_IRQn, EXTI7_0_IRQn, EXTI7_0_IRQn,
       EXTI7_0_IRQn, EXTI7_0_IRQn, EXTI7_0_IRQn, EXTI7_0_IRQn};
#else
static IRQn_Type __drv_gpio_irqn[]
    = {EXTI0_IRQn,     EXTI1_IRQn,     EXTI2_IRQn,     EXTI3_IRQn,
       EXTI4_IRQn,     EXTI9_5_IRQn,   EXTI9_5_IRQn,   EXTI9_5_IRQn,
       EXTI9_5_IRQn,   EXTI9_5_IRQn,   EXTI15_10_IRQn, EXTI15_10_IRQn,
       EXTI15_10_IRQn, EXTI15_10_IRQn, EXTI15_10_IRQn, EXTI15_10_IRQn};
#endif /* defined(MR_USE_WCH_SERIES_V00X) */

static uint8_t GPIOx2GPIOSource(GPIO_TypeDef *GPIOx) {
#if defined(GPIOA)
    if (GPIOx == GPIOA) {
        return GPIO_PortSourceGPIOA;
    }
#endif /* defined(GPIOA) */
#if defined(GPIOB)
    if (GPIOx == GPIOB) {
        return GPIO_PortSourceGPIOB;
    }
#endif /* defined(GPIOB) */
#if defined(GPIOC)
    if (GPIOx == GPIOC) {
        return GPIO_PortSourceGPIOC;
    }
#endif /* defined(GPIOC) */
#if defined(GPIOD)
    if (GPIOx == GPIOD) {
        return GPIO_PortSourceGPIOD;
    }
#endif /* defined(GPIOD) */
#if defined(GPIOE)
    if (GPIOx == GPIOE) {
        return GPIO_PortSourceGPIOE;
    }
#endif /* defined(GPIOE) */
#if defined(GPIOF)
    if (GPIOx == GPIOF) {
        return GPIO_PortSourceGPIOF;
    }
#endif /* defined(GPIOF) */
#if defined(GPIOG)
    if (GPIOx == GPIOG) {
        return GPIO_PortSourceGPIOG;
    }
#endif /* defined(GPIOG) */
    return 0;
}

static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config) {
    drv_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    uint16_t PINx = (1U << pin);

    switch (config->mode) {
        case MR_GPIO_MODE_ANALOG: {
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
            break;
        }
        case MR_GPIO_MODE_OUTPUT: {
            if (config->otype == MR_GPIO_OTYPE_PP) {
                if (!config->alt) {
                    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
                } else {
                    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
                }
            } else {
                if (!config->alt) {
                    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
                } else {
                    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
                }
            }
            break;
        }
        case MR_GPIO_MODE_INPUT: {
            if (config->pull == MR_GPIO_PULL_UP) {
                GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
            } else if (config->pull == MR_GPIO_PULL_DOWN) {
                GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
            } else {
                GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
            }
            break;
        }
        case MR_GPIO_MODE_IRQ_RISING: {
            EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
            break;
        }
        case MR_GPIO_MODE_IRQ_FALLING: {
            EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
            break;
        }
        case MR_GPIO_MODE_IRQ_EDGE: {
            EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
            GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Configure EXTI */
    if (config->mode >= MR_GPIO_MODE_IRQ_RISING) {
        /* Check if the pin is already used */
        if ((__drv_gpio_irq_map[pin].drv != MR_NULL)
            && (drv != __drv_gpio_irq_map[pin].drv)) {
            return -MR_EBUSY;
        } else {
            /* Initialize the waitable */
            if (!MR_OBJECT_IS_INITED((mr_object_t *)&__drv_gpio_exti[pin])) {
                mr_waitable_init(&__drv_gpio_exti[pin], MR_NULL, MR_NULL);
            }

            /* Mark the irq as used */
            __drv_gpio_irq_map[pin].mode = config->mode;
            __drv_gpio_irq_map[pin].drv = drv;
        }

        GPIO_EXTILineConfig(GPIOx2GPIOSource(priv->GPIOx), pin);
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
        EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    } else if (drv == __drv_gpio_irq_map[pin].drv) {
        /* Mark the irq as unused */
        __drv_gpio_irq_map[pin].drv = MR_NULL;
        __drv_gpio_irq_map[pin].mode = 0;

#if defined(MR_USE_WCH_SERIES_V00X)
        if ((__drv_gpio_irq_map[0].drv == MR_NULL)
            && (__drv_gpio_irq_map[1].drv == MR_NULL)
            && (__drv_gpio_irq_map[2].drv == MR_NULL)
            && (__drv_gpio_irq_map[3].drv == MR_NULL)
            && (__drv_gpio_irq_map[4].drv == MR_NULL)
            && (__drv_gpio_irq_map[5].drv == MR_NULL)
            && (__drv_gpio_irq_map[6].drv == MR_NULL)
            && (__drv_gpio_irq_map[7].drv == MR_NULL)) {
            EXTI_InitStructure.EXTI_LineCmd = DISABLE;
        } else {
            EXTI_InitStructure.EXTI_LineCmd = ENABLE;
        }
#else
        if ((pin >= 5) && (pin <= 9)) {
            if ((__drv_gpio_irq_map[5].drv == MR_NULL)
                && (__drv_gpio_irq_map[6].drv == MR_NULL)
                && (__drv_gpio_irq_map[7].drv == MR_NULL)
                && (__drv_gpio_irq_map[8].drv == MR_NULL)
                && (__drv_gpio_irq_map[9].drv == MR_NULL)) {
                EXTI_InitStructure.EXTI_LineCmd = DISABLE;
            } else {
                EXTI_InitStructure.EXTI_LineCmd = ENABLE;
            }
        } else if ((pin >= 10) && (pin <= 15)) {
            if ((__drv_gpio_irq_map[10].drv == MR_NULL)
                && (__drv_gpio_irq_map[11].drv == MR_NULL)
                && (__drv_gpio_irq_map[12].drv == MR_NULL)
                && (__drv_gpio_irq_map[13].drv == MR_NULL)
                && (__drv_gpio_irq_map[14].drv == MR_NULL)
                && (__drv_gpio_irq_map[15].drv == MR_NULL)) {
                EXTI_InitStructure.EXTI_LineCmd = DISABLE;
            } else {
                EXTI_InitStructure.EXTI_LineCmd = ENABLE;
            }
        } else {
            EXTI_InitStructure.EXTI_LineCmd = DISABLE;
        }
#endif /* MR_USE_WCH_SERIES_V00X */
    }
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Line = PINx;
    EXTI_Init(&EXTI_InitStructure);

    /* Configure NVIC */
    NVIC_InitStructure.NVIC_IRQChannelCmd = EXTI_InitStructure.EXTI_LineCmd;
    NVIC_InitStructure.NVIC_IRQChannel = __drv_gpio_irqn[pin];
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_Init(&NVIC_InitStructure);

    /* Configure GPIO */
    RCC_APB2PeriphClockCmd(priv->RCCx, ENABLE);
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = PINx;
    GPIO_Init(priv->GPIOx, &GPIO_InitStructure);
    return 0;
}

static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_config_t *config) {
    drv_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t PINx = (1U << pin);
    uint32_t CFGr, CNFr, MODEr;

    if (PINx <= GPIO_Pin_7) {
        CFGr = priv->GPIOx->CFGLR;
    }
#if defined(GPIO_Pin_8)
    else {
        CFGr = priv->GPIOx->CFGHR;
    }
#else
    else {
        return -MR_EINVAL;
    }
#endif /* defined(GPIO_Pin_8) */
    CNFr = ((CFGr >> ((pin & 0x07) << 2)) >> 2) & 0x03;
    MODEr = (CFGr >> ((pin & 0x07) << 2)) & 0x03;

    if (MODEr == 0x0) {
        config->alt = MR_GPIO_ALT_DISABLE;
        if (CNFr == 0x0) {
            config->mode = MR_GPIO_MODE_ANALOG;
        } else {
            config->mode = MR_GPIO_MODE_INPUT;
            config->pull = MR_GPIO_PULL_NONE;
            if (CNFr == 0x2) {
                if (priv->GPIOx->INDR & (1U << pin)) {
                    config->pull = MR_GPIO_PULL_UP;
                } else {
                    config->pull = MR_GPIO_PULL_DOWN;
                }
            }
        }
        if (drv == __drv_gpio_irq_map[pin].drv) {
            config->mode = __drv_gpio_irq_map[pin].mode;
        }
    } else {
        config->mode = MR_GPIO_MODE_OUTPUT;
        config->pull = MR_GPIO_PULL_NONE;
        if (CNFr == 0x0) {
            config->alt = MR_GPIO_ALT_DISABLE;
            config->otype = MR_GPIO_OTYPE_PP;
        } else if (CNFr == 0x1) {
            config->alt = MR_GPIO_ALT_DISABLE;
            config->otype = MR_GPIO_OTYPE_OD;
        } else if (CNFr == 0x2) {
            config->alt = MR_GPIO_ALT_ENABLE;
            config->otype = MR_GPIO_OTYPE_PP;
        } else {
            config->alt = MR_GPIO_ALT_ENABLE;
            config->otype = MR_GPIO_OTYPE_OD;
        }
    }
    return 0;
}

static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value) {
    drv_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint16_t PINx = (1U << pin);

    /* Write pin value */
    GPIO_WriteBit(priv->GPIOx, PINx, value);
    return 0;
}

static mr_err_t drv_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint8_t *value) {
    drv_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint16_t PINx = (1U << pin);

    /* Read pin value */
    *value = GPIO_ReadInputDataBit(priv->GPIOx, PINx);
    return 0;
}

static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    drv_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Write port value */
    GPIO_Write(priv->GPIOx, mask);
    return 0;
}

static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    drv_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read port value */
    *mask = (mr_uint32_t)GPIO_ReadInputData(priv->GPIOx);
    return 0;
}

static mr_err_t drv_irq_attach(const mr_driver_t *drv, mr_uint32_t pin,
                               mr_waiter_t *entry) {
    MR_IGNORE(drv);

    /* Initialize the waitable */
    if (!MR_OBJECT_IS_INITED((mr_object_t *)&__drv_gpio_exti[pin])) {
        mr_waitable_init(&__drv_gpio_exti[pin], MR_NULL, MR_NULL);
    }

    /* Attach GPIO EXTI irq */
    return mr_waiter_wait(entry, &__drv_gpio_exti[pin]);
}

static void drv_gpio_irq_handle(mr_uint32_t pin) {
    mr_driver_t *drv = (mr_driver_t *)__drv_gpio_irq_map[pin].drv;

    /* Call IRQ handler */
    if (drv != MR_NULL) {
        /* Raise GPIO EXTI irq event */
        mr_waitable_raise(&__drv_gpio_exti[pin], MR_GPIO_WAIT_EXTI);
    }
}

#if defined(MR_USE_WCH_SERIES_V00X)
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line0);
        drv_gpio_irq_handle(0);
    }
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line1);
        drv_gpio_irq_handle(1);
    }
    if (EXTI_GetITStatus(EXTI_Line2) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line2);
        drv_gpio_irq_handle(2);
    }
    if (EXTI_GetITStatus(EXTI_Line3) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line3);
        drv_gpio_irq_handle(3);
    }
    if (EXTI_GetITStatus(EXTI_Line4) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line4);
        drv_gpio_irq_handle(4);
    }
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line5);
        drv_gpio_irq_handle(5);
    }
    if (EXTI_GetITStatus(EXTI_Line6) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line6);
        drv_gpio_irq_handle(6);
    }
    if (EXTI_GetITStatus(EXTI_Line7) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line7);
        drv_gpio_irq_handle(7);
    }
}
#else
void EXTI0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI0_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line0);
        drv_gpio_irq_handle(0);
    }
}
void EXTI1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI1_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line1);
        drv_gpio_irq_handle(1);
    }
}
void EXTI2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI2_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line2) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line2);
        drv_gpio_irq_handle(2);
    }
}
void EXTI3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI3_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line3) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line3);
        drv_gpio_irq_handle(3);
    }
}
void EXTI4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI4_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line4) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line4);
        drv_gpio_irq_handle(4);
    }
}
void EXTI9_5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI9_5_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line5);
        drv_gpio_irq_handle(5);
    }
    if (EXTI_GetITStatus(EXTI_Line6) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line6);
        drv_gpio_irq_handle(6);
    }
    if (EXTI_GetITStatus(EXTI_Line7) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line7);
        drv_gpio_irq_handle(7);
    }
    if (EXTI_GetITStatus(EXTI_Line8) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line8);
        drv_gpio_irq_handle(8);
    }
    if (EXTI_GetITStatus(EXTI_Line9) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line9);
        drv_gpio_irq_handle(9);
    }
}
void EXTI15_10_IRQHandler(void)
    __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI15_10_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line11) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line11);
        drv_gpio_irq_handle(11);
    }
    if (EXTI_GetITStatus(EXTI_Line10) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line10);
        drv_gpio_irq_handle(10);
    }
    if (EXTI_GetITStatus(EXTI_Line12) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line12);
        drv_gpio_irq_handle(12);
    }
    if (EXTI_GetITStatus(EXTI_Line13) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line13);
        drv_gpio_irq_handle(13);
    }
    if (EXTI_GetITStatus(EXTI_Line14) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line14);
        drv_gpio_irq_handle(14);
    }
    if (EXTI_GetITStatus(EXTI_Line15) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line15);
        drv_gpio_irq_handle(15);
    }
}
#endif /* defined(MR_USE_WCH_SERIES_V00X) */

/* Instance install */
#define WCH_HT_GPIO_INST_GPIO_INSTALL(_id, _ops)                               \
    static drv_gpio_priv_t MR_CAT(__drv_gpio_priv_, _id)                       \
        = {.GPIOx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), wch_gpiox),        \
           .RCCx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), wch_rccx)};         \
    MR_HT_INST_GPIO_INSTALL(_id, &MR_CAT(__drv_gpio_priv_, _id), _ops)
MR_HT_INST_DRIVER_FOREACH(WCH_HT_GPIO_INST_GPIO_INSTALL, &__drv_gpio_ops);
#endif /* defined (MR_USE_DRIVER_GPIO) */

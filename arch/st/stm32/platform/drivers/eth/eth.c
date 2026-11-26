/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_ETH)
#include <mrX/drivers/eth/mr_eth.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_eth
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "STM32 ETH requires the GPIO driver"
#endif /* !defined(MR_USE_DRIVER_GPIO) */
#if (MR_HT_INST_NUM(MR_HT_COMPAT) > 1)
#error "STM32 ETH backend supports one MAC instance"
#endif /* (MR_HT_INST_NUM(MR_HT_COMPAT) > 1) */

/* Driver ETH operations */
static mr_err_t eth_configure(const mr_driver_t *drv,
                              const mr_eth_config_t *config);
static mr_err_t eth_link(const mr_driver_t *drv, mr_eth_link_t *link);
static mr_ssize_t eth_read(const mr_driver_t *drv, void *buf, mr_size_t size);
static mr_ssize_t eth_write(const mr_driver_t *drv, const void *buf,
                            mr_size_t size);
#if defined(MR_USE_DRIVER_ETH_STREAM)
static mr_err_t eth_stream_start(const mr_driver_t *drv);
static mr_err_t eth_stream_stop(const mr_driver_t *drv);
static mr_err_t eth_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t eth_rx_stream_submit(const mr_driver_t *drv,
                                     mr_eth_frame_t *frame);
static mr_err_t eth_tx_stream_submit(const mr_driver_t *drv,
                                     mr_eth_frame_t *frame);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
MR_UNUSED static const mr_eth_ops_t __eth_ops = {
    .configure = eth_configure,
    .link = eth_link,
    .read = eth_read,
    .write = eth_write,
#if defined(MR_USE_DRIVER_ETH_STREAM)
    .rx_stream_start = eth_stream_start,
    .rx_stream_stop = eth_stream_stop,
    .rx_stream_is_busy = eth_stream_is_busy,
    .rx_stream_submit = eth_rx_stream_submit,
    .tx_stream_start = eth_stream_start,
    .tx_stream_stop = eth_stream_stop,
    .tx_stream_is_busy = eth_stream_is_busy,
    .tx_stream_submit = eth_tx_stream_submit,
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
};

/* ETH HAL capability definition */
#if !defined(HAL_ETH_MODULE_ENABLED)
#error "STM32 ETH requires HAL_ETH_MODULE_ENABLED"
#endif /* !defined(HAL_ETH_MODULE_ENABLED) */
#if !defined(USE_HAL_ETH_REGISTER_CALLBACKS)                                   \
    || (USE_HAL_ETH_REGISTER_CALLBACKS != 1)
#error "STM32 ETH requires USE_HAL_ETH_REGISTER_CALLBACKS=1"
#endif /* STM32 ETH callback registration capability */
#if !defined(ETH_RX_DESC_CNT) || !defined(ETH_TX_DESC_CNT)                     \
    || !defined(ETH_DMATXNDESCWBF_OWN) || !defined(ETH_DMATXNDESCWBF_ES)       \
    || !defined(ETH_DMACSR_FBE) || !defined(ETH_DMACSR_RI)                     \
    || !defined(ETH_DMACSR_TI) || !defined(ETH_DMACSR_NIS)                     \
    || !defined(ETH_DMACSR_RBU) || !defined(ETH_DMACSR_AIS)                    \
    || !defined(ETH_TX_PACKETS_FEATURES_CRCPAD)                                \
    || !defined(ETH_TX_PACKETS_FEATURES_CSUM)                                  \
    || !defined(ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT)                             \
    || !defined(ETH_CRC_PAD_INSERT)
#error "STM32 ETH requires the descriptor-based ETH HAL"
#endif /* STM32 descriptor-based ETH HAL capability */

/* ETH reset control definition */
#if defined(__HAL_RCC_ETH_FORCE_RESET) && defined(__HAL_RCC_ETH_RELEASE_RESET)
#define STM32_ETH_FORCE_RESET()         __HAL_RCC_ETH_FORCE_RESET()
#define STM32_ETH_RELEASE_RESET()       __HAL_RCC_ETH_RELEASE_RESET()
#elif defined(__HAL_RCC_ETH1MAC_FORCE_RESET)                                   \
    && defined(__HAL_RCC_ETH1MAC_RELEASE_RESET)
#define STM32_ETH_FORCE_RESET()         __HAL_RCC_ETH1MAC_FORCE_RESET()
#define STM32_ETH_RELEASE_RESET()       __HAL_RCC_ETH1MAC_RELEASE_RESET()
#elif defined(__HAL_RCC_ETHMAC_FORCE_RESET)                                    \
    && defined(__HAL_RCC_ETHMAC_RELEASE_RESET)
#define STM32_ETH_FORCE_RESET()         __HAL_RCC_ETHMAC_FORCE_RESET()
#define STM32_ETH_RELEASE_RESET()       __HAL_RCC_ETHMAC_RELEASE_RESET()
#else
#error "STM32 ETH requires supported RCC reset control"
#endif /* defined(__HAL_RCC_ETH_FORCE_RESET) &&                                \
          defined(__HAL_RCC_ETH_RELEASE_RESET) */

/* Clause 22 PHY register definition */
#define PHY_BMCR                        (0U)
#define PHY_BMSR                        (1U)
#define PHY_ANAR                        (4U)
#define PHY_ANLPAR                      (5U)
#define PHY_BMCR_LOOPBACK               (0x4000U)
#define PHY_BMCR_SPEED_100              (0x2000U)
#define PHY_BMCR_AUTO_ENABLE            (0x1000U)
#define PHY_BMCR_AUTO_RESTART           (0x0200U)
#define PHY_BMCR_FULL_DUPLEX            (0x0100U)
#define PHY_BMSR_AUTO_COMPLETE          (0x0020U)
#define PHY_BMSR_AUTO_SUPPORTED         (0x0008U)
#define PHY_BMSR_LINK_UP                (0x0004U)
#define PHY_ADV_ASYM_PAUSE              (0x0800U)
#define PHY_ADV_PAUSE                   (0x0400U)
#define PHY_ADV_100_FULL                (0x0100U)
#define PHY_ADV_100_HALF                (0x0080U)
#define PHY_ADV_10_FULL                 (0x0040U)
#define PHY_ADV_10_HALF                 (0x0020U)
#define PHY_ADV_MODES                                                          \
    (PHY_ADV_100_FULL | PHY_ADV_100_HALF | PHY_ADV_10_FULL | PHY_ADV_10_HALF)
#define PHY_ADV_ACK                     (0x4000U)
#define PHY_ADV_SELECTOR                (0x001FU)
#define PHY_ADV_802_3                   (0x0001U)

/* DMA buffer definition */
#define ETH_BUFFER_SIZE                 (1536U)
#define ETH_DMA_ALIGN                   (32U)
#define ETH_RX_SLOTS                    (ETH_RX_DESC_CNT + 1U)

/* Frame trailer definition */
#define ETH_FCS_SIZE                    (4U)

/* PHY poll interval definition */
#define ETH_LINK_POLL_MS                (100U)

/* TX completion timeout definition */
#define ETH_TX_TIMEOUT_MS               (20U)

#if defined(MR_USE_DRIVER_ETH_STREAM)
/* Stream DMA error definition */
#if defined(ETH_DMACSR_CDE)
#define ETH_STREAM_DMA_ERROR            (ETH_DMACSR_FBE | ETH_DMACSR_CDE)
#else
#define ETH_STREAM_DMA_ERROR            ETH_DMACSR_FBE
#endif /* defined(ETH_DMACSR_CDE) */

/* Stream DMA acknowledge mask definition */
#define ETH_STREAM_DMA_ACK_MASK                                                \
    (ETH_DMACSR_RI | ETH_DMACSR_TI | ETH_DMACSR_NIS | ETH_DMACSR_RBU           \
     | ETH_DMACSR_AIS)
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

/* ETH pin type */
typedef struct eth_pin {
    mr_ht_gpio_spec_t spec;
    mr_uint32_t alternate;
} eth_pin_t;

/* ETH DMA type */
typedef struct eth_dma {
    MR_ALIGN(ETH_DMA_ALIGN) ETH_DMADescTypeDef rx_desc[ETH_RX_DESC_CNT];
    MR_ALIGN(ETH_DMA_ALIGN) ETH_DMADescTypeDef tx_desc[ETH_TX_DESC_CNT];
    MR_ALIGN(ETH_DMA_ALIGN) mr_uint8_t rx_buf[ETH_RX_SLOTS][ETH_BUFFER_SIZE];
    MR_ALIGN(ETH_DMA_ALIGN) mr_uint8_t tx_buf[ETH_BUFFER_SIZE];
} eth_dma_t;

/* ETH RX buffer type */
typedef struct eth_rx_buffer {
    struct eth_rx_buffer *next;
    mr_uint8_t *buf;
    mr_uint16_t size;
    mr_uint8_t used: 1;
} eth_rx_buffer_t;

/* ETH MAC state definition */
typedef enum eth_mac_state {
    ETH_MAC_OFF,
    ETH_MAC_RESET,
    ETH_MAC_READY,
    ETH_MAC_STARTED,
} eth_mac_state_t;

#if defined(MR_USE_DRIVER_ETH_STREAM)
/* ETH RX request type */
typedef struct eth_rx_request {
    eth_rx_buffer_t buffer;
    mr_eth_frame_t *frame;
} eth_rx_request_t;

/* ETH stream type */
typedef struct eth_stream {
    mr_uintptr_t dma_base;
    mr_size_t dma_size;
    IRQn_Type irq;
    mr_uint32_t irq_priority;
    eth_rx_request_t rx_requests[ETH_RX_SLOTS];
    mr_eth_frame_t *tx_frames[ETH_TX_DESC_CNT];
    mr_uint32_t tx_head;
    mr_bool_t active;
} eth_stream_t;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

/* ETH PHY link type */
typedef struct eth_phy_link {
    mr_eth_speed_t speed;
    mr_eth_flow_t flow;
    mr_bool_t full_duplex;
    mr_bool_t up;
} eth_phy_link_t;

/* ETH poll type */
typedef struct eth_poll {
    eth_rx_buffer_t rx_slots[ETH_RX_SLOTS];
    mr_uint32_t tx_index;
    mr_bool_t tx_pending;
} eth_poll_t;

/* ETH private data type */
typedef struct eth_priv {
    void (*rx_link)(void **start, void **end, uint8_t *buf, uint16_t size);
    void (*rx_allocate)(uint8_t **buf);
    void (*clk_enable)(void);
    const eth_pin_t *pins;
    mr_size_t pin_count;
    ETH_TypeDef *instance;
    eth_dma_t *dma;
    mr_uint32_t phy_addr;
    ETH_MediaInterfaceTypeDef media;
    mr_bool_t tx_prefetch;
    ETH_HandleTypeDef handle;
    mr_eth_config_t config;
    eth_mac_state_t mac_state;
    mr_size_t pins_claimed;
    mr_uint32_t phy_advertisement;
    eth_phy_link_t link_mode;
    mr_uint32_t link_poll_tick;
    mr_bool_t link_polled;
    eth_poll_t poll;
    mr_spinlock_t lock;
    mr_bool_t changing;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    eth_stream_t stream;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
} eth_priv_t;

static mr_err_t eth_control_begin(eth_priv_t *priv, mr_bool_t allow_stream) {
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&priv->lock);

    /* Reserve control outside interrupt context */
    ret = 0;
    if (priv->changing) {
        ret = -MR_EBUSY;
    }
#if defined(MR_USE_DRIVER_ETH_STREAM)
    else if (!allow_stream && priv->stream.active) {
        ret = -MR_EBUSY;
    }
#else
    MR_IGNORE(allow_stream);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    if ((ret == 0) && (__get_IPSR() != 0U)) {
        ret = -MR_EPERM;
    }
    if (ret == 0) {
        priv->changing = MR_TRUE;
#if defined(MR_USE_DRIVER_ETH_STREAM)
        HAL_NVIC_DisableIRQ(priv->stream.irq);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&priv->lock, mask);
    return ret;
}

static void eth_control_end(eth_priv_t *priv) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&priv->lock);

    /* Commit control state before submissions and IRQ work */
    priv->changing = MR_FALSE;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    if (priv->stream.active && (priv->mac_state == ETH_MAC_STARTED)) {
        HAL_NVIC_EnableIRQ(priv->stream.irq);
    }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&priv->lock, mask);
}

static void eth_msp_noop(ETH_HandleTypeDef *handle) {
    /* Backend owns clocks and pins, not HAL MSP hooks */
    MR_IGNORE(handle);
}

static eth_rx_buffer_t *eth_rx_slot(eth_priv_t *priv, mr_size_t index) {
    /* Select the buffer for the active I/O mode */
#if defined(MR_USE_DRIVER_ETH_STREAM)
    if (priv->stream.active) {
        return &priv->stream.rx_requests[index].buffer;
    }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    return &priv->poll.rx_slots[index];
}

static void eth_rx_allocate(eth_priv_t *priv, uint8_t **buf) {
    eth_rx_buffer_t *slot;
    mr_size_t index;

    /* Stream slots require explicit submissions */
    *buf = MR_NULL;
    for (index = 0; index < MR_ARRAY_SIZE(priv->poll.rx_slots); index++) {
        slot = eth_rx_slot(priv, index);
        if ((slot->buf != MR_NULL) && !slot->used) {
            slot->used = MR_TRUE;
            *buf = slot->buf;
            return;
        }
    }
}

static void eth_rx_link(eth_priv_t *priv, void **start, void **end,
                        uint8_t *buf, uint16_t size) {
    eth_rx_buffer_t *slot;
    mr_size_t index;

    /* Chain buffers belonging to the same frame */
    for (index = 0; index < MR_ARRAY_SIZE(priv->poll.rx_slots); index++) {
        slot = eth_rx_slot(priv, index);
        if (slot->used && (slot->buf == buf)) {
            slot->size = size;
            slot->next = MR_NULL;
            if (*start == MR_NULL) {
                *start = slot;
            } else {
                ((eth_rx_buffer_t *)*end)->next = slot;
            }
            *end = slot;
            return;
        }
    }
}

static void eth_tx_free(uint32_t *buf) {
    /* Keep TX storage until completion is reaped */
    MR_IGNORE(buf);
}

static mr_err_t eth_mdio_read(eth_priv_t *priv, mr_uint32_t reg,
                              mr_uint32_t *value) {
    uint32_t data;
    mr_err_t ret;

    /* Read PHY register before publishing its value */
    ret = mr_stm32_hal_status(
        HAL_ETH_ReadPHYRegister(&priv->handle, priv->phy_addr, reg, &data));
    if (ret == 0) {
        *value = data;
    }
    return ret;
}

static mr_err_t eth_mdio_write(eth_priv_t *priv, mr_uint32_t reg,
                               mr_uint32_t value) {
    /* Write PHY register */
    return mr_stm32_hal_status(
        HAL_ETH_WritePHYRegister(&priv->handle, priv->phy_addr, reg, value));
}

static mr_err_t eth_phy_read_status(eth_priv_t *priv, mr_uint32_t *status) {
    mr_err_t ret;

    /* Read BMSR */
    ret = eth_mdio_read(priv, PHY_BMSR, status);
    if (ret != 0) {
        return ret;
    }

    /* Reject values returned by an absent PHY */
    return ((*status == 0U) || (*status == 0xFFFFU)) ? -MR_ENODEV : 0;
}

static mr_uint32_t eth_phy_advertisement(const mr_eth_config_t *config,
                                         mr_uint32_t status) {
    mr_uint32_t modes;

    /* BMSR capabilities are six bits above ANAR */
    modes = (status >> 6U) & PHY_ADV_MODES;
    if (config->duplex == MR_ETH_DUPLEX_HALF) {
        modes &= PHY_ADV_100_HALF | PHY_ADV_10_HALF;
    } else if (config->duplex == MR_ETH_DUPLEX_FULL) {
        modes &= PHY_ADV_100_FULL | PHY_ADV_10_FULL;
    }
    if (config->flow.rx || config->flow.tx) {
        modes &= PHY_ADV_100_FULL | PHY_ADV_10_FULL;
        modes |= PHY_ADV_ASYM_PAUSE;
        if (config->flow.rx) {
            modes |= PHY_ADV_PAUSE;
        }
    }
    return modes;
}

static mr_err_t eth_phy_configure(eth_priv_t *priv,
                                  const mr_eth_config_t *config) {
    mr_uint32_t status, advertisement, control, mode;
    mr_err_t ret;

    /* MAC loopback uses the configured mode without accessing a PHY. */
    if (config->loopback == MR_ETH_LOOPBACK_MAC) {
        priv->phy_advertisement = 0U;
        return 0;
    }

    /* Select capabilities before writing PHY controls */
    ret = eth_phy_read_status(priv, &status);
    if (ret != 0) {
        return ret;
    }
    advertisement = eth_phy_advertisement(config, status);
    if ((advertisement & PHY_ADV_MODES) == 0U) {
        return -MR_ENOSYS;
    }

    /* Select automatic negotiation or a fixed mode */
    if (config->speed == MR_ETH_SPEED_AUTO) {
        if ((status & PHY_BMSR_AUTO_SUPPORTED) == 0U) {
            return -MR_ENOSYS;
        }
        ret = eth_mdio_write(priv, PHY_ANAR, advertisement | PHY_ADV_802_3);
        if (ret != 0) {
            return ret;
        }
        control = PHY_BMCR_AUTO_ENABLE | PHY_BMCR_AUTO_RESTART;
    } else {
        /* Fixed AUTO duplex defaults to full duplex */
        if (config->speed == MR_ETH_SPEED_100M) {
            mode = (config->duplex == MR_ETH_DUPLEX_HALF) ? PHY_ADV_100_HALF
                                                          : PHY_ADV_100_FULL;
            control = PHY_BMCR_SPEED_100;
        } else {
            mode = (config->duplex == MR_ETH_DUPLEX_HALF) ? PHY_ADV_10_HALF
                                                          : PHY_ADV_10_FULL;
            control = 0U;
        }
        if ((advertisement & mode) == 0U) {
            return -MR_ENOSYS;
        }
        if (config->duplex != MR_ETH_DUPLEX_HALF) {
            control |= PHY_BMCR_FULL_DUPLEX;
        }
    }
    if (config->loopback == MR_ETH_LOOPBACK_PHY) {
        control |= PHY_BMCR_LOOPBACK;
    }

    /* Publish negotiation data after PHY acceptance */
    ret = eth_mdio_write(priv, PHY_BMCR, control);
    if (ret == 0) {
        priv->phy_advertisement = advertisement;
    }
    return ret;
}

static mr_eth_flow_t eth_phy_flow(const eth_priv_t *priv, mr_uint32_t partner) {
    mr_eth_flow_t flow;
    mr_uint32_t local;

    /* Resolve IEEE 802.3 pause mode */
    local = priv->phy_advertisement;
    mr_memset(&flow, 0, sizeof(flow));
    if ((local & partner & PHY_ADV_PAUSE) != 0U) {
        flow.rx = 1U;
        flow.tx = 1U;
    } else if ((local & partner & PHY_ADV_ASYM_PAUSE) != 0U) {
        if ((local & PHY_ADV_PAUSE) != 0U) {
            flow.rx = 1U;
        } else if ((partner & PHY_ADV_PAUSE) != 0U) {
            flow.tx = 1U;
        }
    }

    /* Apply requested flow control */
    flow.rx = flow.rx && priv->config.flow.rx;
    flow.tx = flow.tx && priv->config.flow.tx;

    return flow;
}

static mr_err_t eth_phy_link(eth_priv_t *priv, eth_phy_link_t *link) {
    mr_uint32_t status, partner, common;
    eth_phy_link_t mode;
    mr_err_t ret;

    /* Loopback does not require a link partner */
    if (priv->config.loopback != MR_ETH_LOOPBACK_NONE) {
        mr_memset(&mode, 0, sizeof(mode));
        mode.speed = priv->config.speed;
        mode.full_duplex = priv->config.duplex != MR_ETH_DUPLEX_HALF;
        mode.up = MR_TRUE;
        if (mode.full_duplex) {
            mode.flow = priv->config.flow;
        }
        *link = mode;
        return 0;
    }

    /* Read twice to clear the BMSR latched-low link */
    ret = eth_mdio_read(priv, PHY_BMSR, &status);
    if (ret == 0) {
        ret = eth_phy_read_status(priv, &status);
    }
    if (ret != 0) {
        return ret;
    }

    /* Resolve link state and fixed-speed settings */
    mr_memset(&mode, 0, sizeof(mode));
    mode.up = ((status & PHY_BMSR_LINK_UP) != 0U);
    if (!mode.up) {
        *link = mode;
        return 0;
    }
    if (priv->config.speed != MR_ETH_SPEED_AUTO) {
        mode.speed = priv->config.speed;
        mode.full_duplex = priv->config.duplex != MR_ETH_DUPLEX_HALF;
        if (mode.full_duplex) {
            mode.flow = priv->config.flow;
        }
        *link = mode;
        return 0;
    }
    if ((status & PHY_BMSR_AUTO_COMPLETE) == 0U) {
        *link = mode;
        return 0;
    }

    /* Resolve the highest common advertised mode */
    ret = eth_mdio_read(priv, PHY_ANLPAR, &partner);
    if (ret != 0) {
        return ret;
    }
    if (((partner & PHY_ADV_ACK) != 0U)
        && ((partner & PHY_ADV_SELECTOR) == PHY_ADV_802_3)) {
        common = partner & priv->phy_advertisement;
        if ((common & (PHY_ADV_100_FULL | PHY_ADV_100_HALF)) != 0U) {
            mode.speed = MR_ETH_SPEED_100M;
            mode.full_duplex = ((common & PHY_ADV_100_FULL) != 0U);
        } else if ((common & (PHY_ADV_10_FULL | PHY_ADV_10_HALF)) != 0U) {
            mode.speed = MR_ETH_SPEED_10M;
            mode.full_duplex = ((common & PHY_ADV_10_FULL) != 0U);
        }
        if (mode.full_duplex) {
            mode.flow = eth_phy_flow(priv, partner);
        }
    }

    /* Publish the resolved link */
    *link = mode;
    return 0;
}

static void eth_mac_force_reset(void) {
    /* Reset MAC and DMA logic */
    STM32_ETH_FORCE_RESET();
    __DSB();
    STM32_ETH_RELEASE_RESET();
    __DSB();
}

static void eth_mac_reset(eth_priv_t *priv) {
    /* Revoke MAC and DMA ownership */
    eth_mac_force_reset();

#if defined(MR_USE_DRIVER_ETH_STREAM)
    /* Clear pending stream interrupt */
    HAL_NVIC_ClearPendingIRQ(priv->stream.irq);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Release HAL state */
    if (priv->handle.Instance != MR_NULL) {
        HAL_ETH_DeInit(&priv->handle);
    }

    /* Reset software and descriptor state */
    mr_memset(&priv->handle, 0, sizeof(priv->handle));
    mr_memset(priv->dma->rx_desc, 0, sizeof(priv->dma->rx_desc));
    mr_memset(priv->dma->tx_desc, 0, sizeof(priv->dma->tx_desc));
    mr_memset(&priv->poll, 0, sizeof(priv->poll));
    mr_memset(&priv->link_mode, 0, sizeof(priv->link_mode));
    priv->mac_state = ETH_MAC_RESET;
    priv->link_polled = MR_FALSE;
}

static mr_err_t eth_mac_prepare(eth_priv_t *priv) {
    ETH_DMAConfigTypeDef dma_config;
    HAL_StatusTypeDef status;
    mr_err_t ret;

    /* Prepare only after a MAC reset */
    if (priv->mac_state != ETH_MAC_RESET) {
        return 0;
    }

    /* Initialize the HAL handle and descriptor rings */
    mr_memset(&priv->handle, 0, sizeof(priv->handle));
    priv->handle.Instance = priv->instance;
    priv->handle.Init.MACAddr = priv->config.mac;
    priv->handle.Init.MediaInterface = priv->media;
    priv->handle.Init.TxDesc = priv->dma->tx_desc;
    priv->handle.Init.RxDesc = priv->dma->rx_desc;
    priv->handle.Init.RxBuffLen = ETH_BUFFER_SIZE;
    priv->handle.MspInitCallback = eth_msp_noop;
    priv->handle.MspDeInitCallback = eth_msp_noop;
    status = HAL_ETH_Init(&priv->handle);
    if (status != HAL_OK) {
        ret = ((priv->handle.ErrorCode & HAL_ETH_ERROR_TIMEOUT) != 0U)
                ? -MR_ETIMEDOUT
                : mr_stm32_hal_status(status);
        goto _reset;
    }

    /* HAL init resets DMA defaults; restore TX prefetch */
    if (priv->tx_prefetch) {
        status = HAL_ETH_GetDMAConfig(&priv->handle, &dma_config);
        if (status == HAL_OK) {
            dma_config.SecondPacketOperate = ENABLE;
            status = HAL_ETH_SetDMAConfig(&priv->handle, &dma_config);
        }
        if (status != HAL_OK) {
            ret = mr_stm32_hal_status(status);
            goto _reset;
        }
    }

    /* Register buffer callbacks */
    status
        = HAL_ETH_RegisterRxAllocateCallback(&priv->handle, priv->rx_allocate);
    if (status == HAL_OK) {
        status = HAL_ETH_RegisterRxLinkCallback(&priv->handle, priv->rx_link);
    }
    if (status == HAL_OK) {
        status = HAL_ETH_RegisterTxFreeCallback(&priv->handle, eth_tx_free);
    }
    if (status != HAL_OK) {
        ret = mr_stm32_hal_status(status);
        goto _reset;
    }
    priv->mac_state = ETH_MAC_READY;
    return 0;
_reset:
    /* Reset the MAC after initialization failure */
    eth_mac_reset(priv);
    return ret;
}

static mr_err_t eth_mac_configure(eth_priv_t *priv,
                                  const eth_phy_link_t *mode) {
    ETH_MACConfigTypeDef config;
    mr_err_t ret;

    /* Apply the resolved link mode */
    ret = mr_stm32_hal_status(HAL_ETH_GetMACConfig(&priv->handle, &config));
    if (ret != 0) {
        return ret;
    }
    config.Speed
        = (mode->speed == MR_ETH_SPEED_100M) ? ETH_SPEED_100M : ETH_SPEED_10M;
    config.DuplexMode
        = mode->full_duplex ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
    config.LoopbackMode
        = (priv->config.loopback == MR_ETH_LOOPBACK_MAC) ? ENABLE : DISABLE;
    config.SourceAddrControl = ETH_SOURCEADDRESS_DISABLE;
    config.ChecksumOffload = priv->config.csum.rx ? ENABLE : DISABLE;
    config.DropTCPIPChecksumErrorPacket
        = priv->config.csum.rx ? ENABLE : DISABLE;
    config.ReceiveFlowControl = mode->flow.rx ? ENABLE : DISABLE;
    config.TransmitFlowControl = mode->flow.tx ? ENABLE : DISABLE;
    config.PauseTime = (config.TransmitFlowControl == ENABLE) ? 0xFFFFU : 0U;

    /* Keep padding and remove the FCS when delivering the frame */
    config.AutomaticPadCRCStrip = DISABLE;
    config.CRCStripTypePacket = DISABLE;

    /* Commit MAC configuration */
    return mr_stm32_hal_status(HAL_ETH_SetMACConfig(&priv->handle, &config));
}

static mr_err_t eth_dma_start(eth_priv_t *priv) {
    mr_size_t index;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    mr_err_t ret;

    /* Control operations own the mode until eth_control_end() */
    if (priv->stream.active) {
        HAL_NVIC_ClearPendingIRQ(priv->stream.irq);
        ret = mr_stm32_hal_status(HAL_ETH_Start_IT(&priv->handle));
        if (ret == 0) {
#if defined(ETH_DMACIER_CDEE)
            __HAL_ETH_DMA_ENABLE_IT(&priv->handle, ETH_DMACIER_CDEE);
#endif /* defined(ETH_DMACIER_CDEE) */

            /* An empty RX ring cannot trigger a refill IRQ */
            HAL_NVIC_SetPendingIRQ(priv->stream.irq);
        }
        return ret;
    }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Recycle private polling buffers after copying */
    for (index = 0; index < MR_ARRAY_SIZE(priv->poll.rx_slots); index++) {
        mr_memset(&priv->poll.rx_slots[index], 0,
                  sizeof(priv->poll.rx_slots[index]));
        priv->poll.rx_slots[index].buf = priv->dma->rx_buf[index];
    }

    /* Start DMA in polling mode */
    return mr_stm32_hal_status(HAL_ETH_Start(&priv->handle));
}

static mr_err_t eth_mac_start(eth_priv_t *priv, const eth_phy_link_t *mode) {
    mr_err_t ret;

    /* Rebuild the MAC and rings as one control operation */
    ret = eth_mac_prepare(priv);
    if (ret == 0) {
        ret = eth_mac_configure(priv, mode);
    }
    if (ret == 0) {
        ret = eth_dma_start(priv);
    }
    if (ret != 0) {
        eth_mac_reset(priv);
        return ret;
    }
    priv->mac_state = ETH_MAC_STARTED;
    return 0;
}

#if defined(MR_USE_DRIVER_ETH_STREAM)
static mr_eth_frame_t *eth_stream_rx_detach(eth_rx_request_t *request) {
    mr_eth_frame_t *frame;

    /* Clear frame and DMA address before re-entry */
    frame = request->frame;
    mr_memset(request, 0, sizeof(*request));
    return frame;
}

static mr_eth_frame_t *eth_stream_tx_detach(eth_stream_t *stream) {
    mr_eth_frame_t *frame;

    /* Consume one submission in ring order */
    frame = stream->tx_frames[stream->tx_head];
    stream->tx_frames[stream->tx_head] = MR_NULL;
    stream->tx_head = (stream->tx_head + 1U) % ETH_TX_DESC_CNT;
    return frame;
}

static void eth_stream_return(const mr_driver_t *drv, mr_eth_frame_t *frame,
                              mr_err_t status, mr_bool_t rx) {
    mr_wait_event_t event;

    /* Publish frame status before notification */
    frame->status = status;
    if (rx) {
        if (status != 0) {
            frame->size = 0U;
        }
        event = (status == 0) ? MR_ETH_WAIT_RX_READY : MR_ETH_WAIT_RX_ERROR;
    } else {
        event = (status == 0) ? MR_ETH_WAIT_TX_DONE : MR_ETH_WAIT_TX_ERROR;
    }

    /* Notify waiters while the driver is alive */
    if (!MR_DRIVER_IS_DYING(drv)) {
        mr_eth_event_raise((mr_driver_t *)drv, event, frame);
    }
}

static void eth_stream_quiesce(const mr_driver_t *drv, mr_err_t status,
                               mr_bool_t keep_enabled) {
    mr_eth_frame_t *tx_frames[ETH_TX_DESC_CNT];
    mr_eth_frame_t *rx_frames[ETH_RX_SLOTS];
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_size_t tx_count, rx_count, index;
    mr_eth_frame_t *frame;
    mr_bool_t changing;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&priv->lock);

    /* Block new stream work */
    changing = priv->changing;
    priv->changing = MR_TRUE;
    HAL_NVIC_DisableIRQ(priv->stream.irq);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&priv->lock, mask);

    /* Revoke MAC and DMA ownership */
    if (priv->mac_state != ETH_MAC_RESET) {
        eth_mac_reset(priv);
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&priv->lock);

    /* Detach outstanding frame ownership */
    priv->stream.active = keep_enabled;
    tx_count = 0U;
    for (index = 0; index < MR_ARRAY_SIZE(priv->stream.tx_frames); index++) {
        frame = eth_stream_tx_detach(&priv->stream);
        if (frame != MR_NULL) {
            tx_frames[tx_count++] = frame;
        }
    }

    /* Reset TX ring cursor */
    priv->stream.tx_head = 0U;
    rx_count = 0U;
    for (index = 0; index < MR_ARRAY_SIZE(priv->stream.rx_requests); index++) {
        frame = eth_stream_rx_detach(&priv->stream.rx_requests[index]);
        if (frame != MR_NULL) {
            rx_frames[rx_count++] = frame;
        }
    }

    /* Restore previous control state */
    priv->changing = changing;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&priv->lock, mask);

    /* Return detached TX frames */
    for (index = 0; index < tx_count; index++) {
        eth_stream_return(drv, tx_frames[index], status, MR_FALSE);
    }

    /* Return detached RX frames */
    for (index = 0; index < rx_count; index++) {
        eth_stream_return(drv, rx_frames[index], status, MR_TRUE);
    }
}
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

static mr_err_t eth_disable(eth_priv_t *priv, mr_err_t first_error) {
    mr_err_t ret;

    /* Stop the MAC before releasing pins */
    if (priv->mac_state != ETH_MAC_OFF) {
        if (priv->mac_state != ETH_MAC_RESET) {
            eth_mac_reset(priv);
        }
        priv->mac_state = ETH_MAC_OFF;
        mr_memset(&priv->link_mode, 0, sizeof(priv->link_mode));
        priv->link_polled = MR_FALSE;
    }

    /* Release pin muxing in reverse acquisition order */
    while (priv->pins_claimed != 0U) {
        ret = mr_stm32_gpio_pin_release(
            &priv->pins[priv->pins_claimed - 1U].spec);
        if (ret != 0) {
            return (first_error != 0) ? first_error : ret;
        }
        priv->pins_claimed--;
    }
    return first_error;
}

static mr_err_t eth_enable(eth_priv_t *priv, const mr_eth_config_t *config) {
    const eth_pin_t *pin;
    mr_err_t ret;

    /* Retry pending pin releases */
    ret = eth_disable(priv, 0);
    if (ret != 0) {
        return ret;
    }

    /* Claim the Ethernet pins */
    while (priv->pins_claimed < priv->pin_count) {
        pin = &priv->pins[priv->pins_claimed];
        ret = mr_stm32_gpio_pin_alternate(
            &pin->spec, pin->alternate, MR_GPIO_PULL_NONE, MR_GPIO_OTYPE_PP);
        if (ret != 0) {
            goto _disable;
        }
        priv->pins_claimed++;
    }

    /* Enable the MAC clocks before HAL resets the Ethernet subsystem */
    priv->clk_enable();
#if defined(__HAL_RCC_ETHTX_CLK_ENABLE)
    __HAL_RCC_ETHTX_CLK_ENABLE();
#endif /* defined(__HAL_RCC_ETHTX_CLK_ENABLE) */
#if defined(__HAL_RCC_ETHRX_CLK_ENABLE)
    __HAL_RCC_ETHRX_CLK_ENABLE();
#endif /* defined(__HAL_RCC_ETHRX_CLK_ENABLE) */

    /* Prepare the MAC and configure the external PHY */
    priv->mac_state = ETH_MAC_RESET;
    priv->config = *config;
    ret = eth_mac_prepare(priv);
    if (ret != 0) {
        goto _disable;
    }
    ret = eth_phy_configure(priv, config);
    if (ret != 0) {
        goto _disable;
    }
    return 0;
_disable:
    /* Release resources acquired before the failure */
    return eth_disable(priv, ret);
}

static mr_err_t eth_config_check(const eth_priv_t *priv,
                                 const mr_eth_config_t *config) {
    /* Check ETH capabilities */
    if ((config->speed != MR_ETH_SPEED_AUTO)
        && (config->speed != MR_ETH_SPEED_10M)
        && (config->speed != MR_ETH_SPEED_100M)) {
        return -MR_ENOSYS;
    }
    if ((config->loopback != MR_ETH_LOOPBACK_NONE)
        && (config->speed == MR_ETH_SPEED_AUTO)) {
        return -MR_ENOSYS;
    }

    /* Check instance resources */
    if ((priv->instance == MR_NULL) || (priv->clk_enable == MR_NULL)
        || (priv->dma == MR_NULL) || (priv->pins == MR_NULL)) {
        return -MR_ENODEV;
    }

    /* Check board resources */
    if ((priv->phy_addr > 31U) || (priv->pin_count == 0U)
        || ((priv->media != HAL_ETH_MII_MODE)
            && (priv->media != HAL_ETH_RMII_MODE))) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_bool_t eth_config_matches(const mr_eth_config_t *left,
                                    const mr_eth_config_t *right) {
    /* Only MTU can change while the MAC runs */
    return (left->speed == right->speed) && (left->duplex == right->duplex)
        && (left->loopback == right->loopback)
        && (left->flow.rx == right->flow.rx)
        && (left->flow.tx == right->flow.tx)
        && (left->csum.rx == right->csum.rx)
        && (left->csum.tx == right->csum.tx)
        && (mr_memcmp(left->mac, right->mac, MR_ETH_ADDR_LEN) == 0);
}

static mr_err_t eth_configure(const mr_driver_t *drv,
                              const mr_eth_config_t *config) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Reserve control for the configuration change */
    ret = eth_control_begin(priv, config->speed == MR_ETH_SPEED_OFF);
    if (ret != 0) {
        return ret;
    }

    /* Revoke DMA ownership before releasing board resources */
    if (config->speed == MR_ETH_SPEED_OFF) {
#if defined(MR_USE_DRIVER_ETH_STREAM)
        if (priv->stream.active) {
            eth_stream_quiesce(drv, -MR_EPERM, MR_FALSE);
        }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
        ret = eth_disable(priv, 0);
        goto _exit;
    }

    /* Apply supported settings or update the active MTU */
    ret = eth_config_check(priv, config);
    if (ret != 0) {
        goto _exit;
    }
    if (priv->mac_state == ETH_MAC_OFF) {
        ret = eth_enable(priv, config);
    } else if (!eth_config_matches(config, &priv->config)) {
        ret = -MR_EBUSY;
    } else {
        priv->config.mtu = config->mtu;
    }
_exit:
    /* Release the configuration reservation */
    eth_control_end(priv);
    return ret;
}

static mr_bool_t eth_link_matches(const eth_phy_link_t *left,
                                  const eth_phy_link_t *right) {
    /* Compare the MAC-relevant link settings */
    return (left->speed == right->speed)
        && (left->full_duplex == right->full_duplex)
        && (left->flow.rx == right->flow.rx)
        && (left->flow.tx == right->flow.tx);
}

static mr_err_t eth_link_update(const mr_driver_t *drv, mr_bool_t force) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t now, status;
    eth_phy_link_t mode;
    mr_err_t ret;

    /* Skip link refresh while the MAC is off */
    if (priv->mac_state == ETH_MAC_OFF) {
        return 0;
    }

    /* Check DMA faults before using cached PHY state; HAL has no query */
    if (priv->mac_state != ETH_MAC_RESET) {
        status = priv->handle.Instance->DMACSR;
#if defined(MR_USE_DRIVER_ETH_STREAM)
        if (priv->stream.active && ((status & ETH_STREAM_DMA_ERROR) != 0U)) {
            eth_stream_quiesce(drv, -MR_EIO, MR_FALSE);
            return -MR_EIO;
        }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
        if ((status & ETH_DMACSR_FBE) != 0U) {
            /* Reset polling DMA so the next link refresh can restart it */
            eth_mac_reset(priv);
            return -MR_EIO;
        }
    }

    /* Throttle polling; explicit link queries always refresh */
    now = HAL_GetTick();
    if (!force && priv->link_polled
        && ((now - priv->link_poll_tick) < ETH_LINK_POLL_MS)) {
        return 0;
    }

    /* Prepare the MAC and read the PHY link */
    ret = eth_mac_prepare(priv);
    if (ret != 0) {
        return ret;
    }
    mr_memset(&mode, 0, sizeof(mode));
    ret = eth_phy_link(priv, &mode);

    /* Invalidate DMA work before changing MAC mode */
    if ((priv->mac_state == ETH_MAC_STARTED)
        && ((ret != 0) || (mode.speed == MR_ETH_SPEED_OFF)
            || !eth_link_matches(&mode, &priv->link_mode))) {
#if defined(MR_USE_DRIVER_ETH_STREAM)
        if (priv->stream.active) {
            eth_stream_quiesce(drv, -MR_EIO, MR_TRUE);
        } else
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
        {
            eth_mac_reset(priv);
        }
    }
    if (ret != 0) {
        return ret;
    }

    /* Start the MAC when a usable link is available */
    if ((mode.speed != MR_ETH_SPEED_OFF)
        && (priv->mac_state != ETH_MAC_STARTED)) {
        ret = eth_mac_start(priv, &mode);
        if (ret != 0) {
#if defined(MR_USE_DRIVER_ETH_STREAM)
            /* A failed start may have consumed RX records */
            if (priv->stream.active) {
                eth_stream_quiesce(drv, ret, MR_TRUE);
            }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
            return ret;
        }
    }

    /* Commit the PHY snapshot after the MAC mode is ready */
    priv->link_mode = mode;
    priv->link_polled = MR_TRUE;
    priv->link_poll_tick = now;
    return 0;
}

static mr_err_t eth_link(const mr_driver_t *drv, mr_eth_link_t *link) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Reserve control while refreshing the link */
    ret = eth_control_begin(priv, MR_TRUE);
    if (ret != 0) {
        return ret;
    }

    /* Publish the link snapshot after a successful refresh */
    ret = eth_link_update(drv, MR_TRUE);
    if (ret == 0) {
        link->up = priv->link_mode.up;
        link->speed = priv->link_mode.speed;
        link->flow = priv->link_mode.flow;
        link->duplex = MR_ETH_DUPLEX_AUTO;
        if (link->speed != MR_ETH_SPEED_OFF) {
            link->duplex = priv->link_mode.full_duplex ? MR_ETH_DUPLEX_FULL
                                                       : MR_ETH_DUPLEX_HALF;
        }
    }

    /* Release the link control reservation */
    eth_control_end(priv);
    return ret;
}

static mr_err_t eth_rx_next(eth_priv_t *priv, eth_rx_buffer_t **slot) {
    void *packet;

    /* Read a frame and let HAL replenish the descriptor ring */
    packet = MR_NULL;
    if (HAL_ETH_ReadData(&priv->handle, &packet) != HAL_OK) {
        return -MR_EAGAIN;
    }
    if (packet == MR_NULL) {
        return -MR_EIO;
    }

    /* Order payload reads after DMA completion */
    __DMB();
    *slot = packet;
    return 0;
}

static mr_ssize_t eth_rx_frame_size(const eth_priv_t *priv,
                                    const eth_rx_buffer_t *slot) {
    mr_size_t size;
    uint32_t error;

    /* Validate the descriptor and reject multi-buffer frames */
    if ((HAL_ETH_GetRxDataErrorCode(&priv->handle, &error) != HAL_OK)
        || (error != 0U) || (slot->next != MR_NULL)
        || (slot->size < (MR_ETH_FRAME_MIN + ETH_FCS_SIZE))) {
        return -MR_EIO;
    }

    /* Report the frame length without FCS */
    size = slot->size - ETH_FCS_SIZE;
    if ((size > MR_ETH_FRAME_MIN)
        && (size > (MR_ETH_HEADER_LEN + priv->config.mtu))) {
        return -MR_ERANGE;
    }
    return (mr_ssize_t)size;
}

static mr_err_t eth_tx_status(const eth_priv_t *priv, mr_uint32_t index) {
    mr_uint32_t status;

    /* HAL releases TX buffers without reporting descriptor errors */
    status = priv->dma->tx_desc[index].DESC3;
    if ((status & ETH_DMATXNDESCWBF_OWN) != 0U) {
        return -MR_EAGAIN;
    }
    __DMB();

    /* Report the descriptor completion status */
    return ((status & ETH_DMATXNDESCWBF_ES) != 0U) ? -MR_EIO : 0;
}

static mr_err_t eth_tx_submit(eth_priv_t *priv, mr_uint8_t *buf, mr_size_t size,
                              mr_uint32_t index) {
    ETH_TxPacketConfigTypeDef config;
    ETH_BufferTypeDef buffer;

    /* HAL may retain completed descriptors until reaped */
    if (((priv->dma->tx_desc[index].DESC3 & ETH_DMATXNDESCWBF_OWN) != 0U)
        || (priv->handle.TxDescList.PacketAddress[index] != MR_NULL)) {
        return -MR_EAGAIN;
    }

    /* Prepare TX buffer */
    mr_memset(&buffer, 0, sizeof(buffer));
    buffer.buffer = buf;
    buffer.len = size;

    /* Prepare packet configuration */
    mr_memset(&config, 0, sizeof(config));
    config.Attributes
        = ETH_TX_PACKETS_FEATURES_CRCPAD
        | (priv->config.csum.tx ? ETH_TX_PACKETS_FEATURES_CSUM : 0U);
    config.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT;
    config.Length = size;
    config.TxBuffer = &buffer;
    config.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    /* HAL stores this aligned cookie as a uint32_t pointer */
    config.pData = &priv->dma->tx_desc[index];

    /* Complete SRAM writes before DMA takes ownership */
    __DSB();

    /* Submit the packet to the TX descriptor ring */
    if (HAL_ETH_Transmit_IT(&priv->handle, &config) != HAL_OK) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t eth_poll_begin(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Hold control throughout the polling operation */
    ret = eth_control_begin(priv, MR_FALSE);
    if (ret != 0) {
        return ret;
    }

    /* Refresh the link before polling */
    ret = eth_link_update(drv, MR_FALSE);
    if (ret != 0) {
        eth_control_end(priv);
    }
    return ret;
}

static void eth_poll_rx_release(eth_rx_buffer_t *slot) {
    eth_rx_buffer_t *next;

    /* Recycle only private polling storage without resubmission */
    while (slot != MR_NULL) {
        next = slot->next;
        slot->next = MR_NULL;
        slot->size = 0U;
        slot->used = MR_FALSE;
        slot = next;
    }
}

static mr_ssize_t eth_read(const mr_driver_t *drv, void *buf, mr_size_t size) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    eth_rx_buffer_t *slot;
    mr_ssize_t result;
    mr_err_t ret;

    /* Reserve polling and fetch the next frame */
    ret = eth_poll_begin(drv);
    if (ret != 0) {
        return ret;
    }
    result = 0;
    if (priv->mac_state != ETH_MAC_STARTED) {
        goto _exit;
    }
    ret = eth_rx_next(priv, &slot);
    if (ret != 0) {
        result = (ret == -MR_EAGAIN) ? 0 : ret;
        goto _exit;
    }

    /* Deliver the complete frame before recycling buffers */
    result = eth_rx_frame_size(priv, slot);
    if (result > 0) {
        if ((mr_size_t)result > size) {
            result = -MR_ERANGE;
        } else {
            mr_memcpy(buf, slot->buf, (mr_size_t)result);
        }
    }
    eth_poll_rx_release(slot);
_exit:
    /* Release the polling reservation */
    eth_control_end(priv);
    return result;
}

static mr_err_t eth_poll_tx_reap(eth_priv_t *priv) {
    mr_err_t ret;

    /* Reclaim the polling buffer after completion */
    ret = eth_tx_status(priv, priv->poll.tx_index);
    if (ret != -MR_EAGAIN) {
        HAL_ETH_ReleaseTxPacket(&priv->handle);
        priv->poll.tx_pending = MR_FALSE;
    }
    return ret;
}

static mr_err_t eth_poll_tx_wait(eth_priv_t *priv) {
    mr_uint32_t tick;
    mr_err_t ret;

    /* Keep the buffer reserved after a timeout */
    tick = HAL_GetTick();
    do {
        /* HAL has no DMA fault query while polling a transmission */
        if ((priv->handle.Instance->DMACSR & ETH_DMACSR_FBE) != 0U) {
            eth_mac_reset(priv);
            return -MR_EIO;
        }
        ret = eth_poll_tx_reap(priv);
        if (ret != -MR_EAGAIN) {
            return ret;
        }
    } while ((HAL_GetTick() - tick) < ETH_TX_TIMEOUT_MS);
    return -MR_ETIMEDOUT;
}

static mr_ssize_t eth_write(const mr_driver_t *drv, const void *buf,
                            mr_size_t size) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Check frame size against internal polling buffer */
    if (size > ETH_BUFFER_SIZE) {
        return -MR_ERANGE;
    }

    /* Reserve polling and check TX availability */
    ret = eth_poll_begin(drv);
    if (ret != 0) {
        return ret;
    }
    if (priv->mac_state != ETH_MAC_STARTED) {
        ret = -MR_EAGAIN;
        goto _exit;
    }
    if (priv->poll.tx_pending && (eth_poll_tx_reap(priv) == -MR_EAGAIN)) {
        ret = -MR_EAGAIN;
        goto _exit;
    }

    /* Caller memory stays reusable if DMA outlives this call */
    mr_memcpy(priv->dma->tx_buf, buf, size);
    priv->poll.tx_index = priv->handle.TxDescList.CurTxDesc;
    ret = eth_tx_submit(priv, priv->dma->tx_buf, size, priv->poll.tx_index);
    if (ret == 0) {
        priv->poll.tx_pending = MR_TRUE;
        ret = eth_poll_tx_wait(priv);
    }
_exit:
    /* Release the polling reservation */
    eth_control_end(priv);
    return (ret == 0) ? (mr_ssize_t)size : ret;
}

#if defined(MR_USE_DRIVER_ETH_STREAM)
static mr_err_t eth_stream_start(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Reserve control for the stream transition */
    ret = eth_control_begin(priv, MR_FALSE);
    if (ret != 0) {
        return ret;
    }

    /* Check the MAC state and stream DMA region */
    if (priv->mac_state == ETH_MAC_OFF) {
        ret = -MR_EPERM;
        goto _exit;
    }
    if ((priv->stream.dma_size < ETH_BUFFER_SIZE)
        || (((priv->stream.dma_base | priv->stream.dma_size)
             & (ETH_DMA_ALIGN - 1U))
            != 0U)
        || (priv->stream.dma_size > (MR_SIZE_MAX - priv->stream.dma_base))) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Revoke polling descriptors */
    eth_mac_reset(priv);

    /* Configure stream IRQ */
    HAL_NVIC_SetPriority(priv->stream.irq, priv->stream.irq_priority, 0U);

    /* Enable stream submissions */
    mask = mr_spinlock_lock_irqsave(&priv->lock);
    priv->stream.active = MR_TRUE;
    mr_spinlock_unlock_irqrestore(&priv->lock, mask);

    /* Refresh the link and cancel the stream on failure */
    ret = eth_link_update(drv, MR_TRUE);
    if (ret != 0) {
        eth_stream_quiesce(drv, ret, MR_FALSE);
    }
_exit:
    /* Release the stream control reservation */
    eth_control_end(priv);
    return ret;
}

static mr_err_t eth_stream_stop(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Reserve control and return outstanding frames */
    ret = eth_control_begin(priv, MR_TRUE);
    if (ret != 0) {
        return ret;
    }
    if (priv->stream.active) {
        eth_stream_quiesce(drv, -MR_EPERM, MR_FALSE);
    }

    /* Release the stream control reservation */
    eth_control_end(priv);
    return 0;
}

static mr_err_t eth_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&priv->lock);
    *busy = priv->stream.active;
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&priv->lock, mask);
    return 0;
}

static mr_bool_t eth_stream_overlaps(const void *buf, mr_size_t size,
                                     const void *other, mr_size_t length) {
    mr_uintptr_t start, addr;

    /* Use start distances to avoid address overflow */
    start = (mr_uintptr_t)other;
    addr = (mr_uintptr_t)buf;
    return (addr <= start) ? ((start - addr) < size)
                           : ((addr - start) < length);
}

static mr_err_t eth_stream_check(const eth_priv_t *priv,
                                 const mr_eth_frame_t *frame, mr_bool_t rx) {
    const eth_rx_request_t *request;
    const mr_eth_frame_t *pending;
    mr_uintptr_t addr, offset;
    mr_size_t size, index;

    /* Caller holds the lock through validation and insertion */
    if (!priv->stream.active || priv->changing) {
        return -MR_EPERM;
    }
    size = rx ? ETH_BUFFER_SIZE : frame->size;
    addr = (mr_uintptr_t)frame->buf;
    offset = addr - priv->stream.dma_base;
    if (rx && (frame->capacity < ETH_BUFFER_SIZE)) {
        return -MR_ERANGE;
    }
    if ((rx && ((addr & (ETH_DMA_ALIGN - 1U)) != 0U))
        || (addr < priv->stream.dma_base) || (offset >= priv->stream.dma_size)
        || (size > (priv->stream.dma_size - offset))) {
        return -MR_EINVAL;
    }

    /* Reject duplicate frames and overlapping DMA buffers */
    for (index = 0; index < MR_ARRAY_SIZE(priv->stream.tx_frames); index++) {
        pending = priv->stream.tx_frames[index];
        if ((pending != MR_NULL)
            && ((pending == frame)
                || eth_stream_overlaps(frame->buf, size, pending->buf,
                                       pending->size))) {
            return -MR_EBUSY;
        }
    }
    for (index = 0; index < MR_ARRAY_SIZE(priv->stream.rx_requests); index++) {
        request = &priv->stream.rx_requests[index];
        if ((request->frame != MR_NULL)
            && ((request->frame == frame)
                || eth_stream_overlaps(frame->buf, size, request->buffer.buf,
                                       ETH_BUFFER_SIZE))) {
            return -MR_EBUSY;
        }
    }
    return 0;
}

static mr_err_t eth_stream_rx_enqueue(eth_priv_t *priv, mr_eth_frame_t *frame) {
    eth_rx_request_t *request;
    mr_size_t index;

    /* Reserve one request for later HAL DMA attachment */
    for (index = 0; index < MR_ARRAY_SIZE(priv->stream.rx_requests); index++) {
        request = &priv->stream.rx_requests[index];
        if (request->frame != MR_NULL) {
            continue;
        }
        mr_memset(request, 0, sizeof(*request));
        request->buffer.buf = frame->buf;
        request->frame = frame;
        if (priv->mac_state == ETH_MAC_STARTED) {
            /* Refill an empty ring that cannot generate an IRQ */
            HAL_NVIC_SetPendingIRQ(priv->stream.irq);
        }
        return 0;
    }
    return -MR_EAGAIN;
}

static mr_err_t eth_stream_tx_enqueue(eth_priv_t *priv, mr_eth_frame_t *frame) {
    mr_uint32_t index;
    mr_err_t ret;

    /* Check the MAC and current TX slot */
    if (priv->mac_state != ETH_MAC_STARTED) {
        return -MR_EAGAIN;
    }
    index = priv->handle.TxDescList.CurTxDesc;
    if (priv->stream.tx_frames[index] != MR_NULL) {
        return -MR_EAGAIN;
    }

    /* Reserve before DMA completion; caller owns rejected frames */
    priv->stream.tx_frames[index] = frame;
    ret = eth_tx_submit(priv, frame->buf, frame->size, index);
    if (ret != 0) {
        priv->stream.tx_frames[index] = MR_NULL;
    }
    return ret;
}

static mr_err_t eth_stream_submit(const mr_driver_t *drv, mr_eth_frame_t *frame,
                                  mr_bool_t rx) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&priv->lock);

    /* Validate and enqueue the frame under the same lock */
    ret = eth_stream_check(priv, frame, rx);
    if (ret == 0) {
        ret = rx ? eth_stream_rx_enqueue(priv, frame)
                 : eth_stream_tx_enqueue(priv, frame);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&priv->lock, mask);
    return ret;
}

static mr_err_t eth_rx_stream_submit(const mr_driver_t *drv,
                                     mr_eth_frame_t *frame) {
    /* Submit an RX frame */
    return eth_stream_submit(drv, frame, MR_TRUE);
}

static mr_err_t eth_tx_stream_submit(const mr_driver_t *drv,
                                     mr_eth_frame_t *frame) {
    /* Submit a TX frame */
    return eth_stream_submit(drv, frame, MR_FALSE);
}

static void eth_stream_rx_complete(const mr_driver_t *drv,
                                   eth_rx_buffer_t *slot) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    eth_rx_request_t *request;
    eth_rx_buffer_t *next;
    mr_eth_frame_t *frame;
    mr_ssize_t size;
    mr_err_t ret;
    int mask;

    /* Check the received frame and resolve its status */
    size = eth_rx_frame_size(priv, slot);
    ret = (size > 0) ? 0 : (mr_err_t)size;

    /* Return each chained buffer to its owner */
    do {
        /* Lock */
        mask = mr_spinlock_lock_irqsave(&priv->lock);

        next = slot->next;
        request = MR_CONTAINER_OF(slot, eth_rx_request_t, buffer);

        /* Detach the frame from its request */
        frame = eth_stream_rx_detach(request);

        /* Unlock before raising the event; waiter callbacks may re-enter */
        mr_spinlock_unlock_irqrestore(&priv->lock, mask);

        /* Publish the frame size on success */
        if (ret == 0) {
            frame->size = (mr_size_t)size;
        }
        eth_stream_return(drv, frame, ret, MR_TRUE);
        slot = next;
    } while (slot != MR_NULL);
}

static void eth_stream_tx_complete(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_eth_frame_t *frame;
    mr_uint32_t index;
    mr_size_t count;
    mr_err_t ret;
    int mask;

    /* Reap completed frames in submission order */
    for (count = 0; count < ETH_TX_DESC_CNT; count++) {
        /* Lock */
        mask = mr_spinlock_lock_irqsave(&priv->lock);

        index = priv->stream.tx_head;
        frame = priv->stream.tx_frames[index];

        /* Check the head frame completion status */
        ret = (frame != MR_NULL) ? eth_tx_status(priv, index) : -MR_EAGAIN;
        if (ret == -MR_EAGAIN) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&priv->lock, mask);
            return;
        }

        /* HAL may free later descriptors; records stay until detached */
        HAL_ETH_ReleaseTxPacket(&priv->handle);
        frame = eth_stream_tx_detach(&priv->stream);

        /* Unlock before raising the event; waiter callbacks may re-enter */
        mr_spinlock_unlock_irqrestore(&priv->lock, mask);

        eth_stream_return(drv, frame, ret, MR_FALSE);
    }
}

static void eth_irq_handle(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t status, clear;
    eth_rx_buffer_t *slot;
    mr_size_t count;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&priv->lock);

    /* Check the stream state */
    if (!priv->stream.active || priv->changing
        || (priv->mac_state != ETH_MAC_STARTED)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&priv->lock, mask);
        return;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&priv->lock, mask);

    /* Read DMA interrupt status */
    status = priv->handle.Instance->DMACSR;

    /* Stop the stream on a fatal DMA fault */
    if ((status & ETH_STREAM_DMA_ERROR) != 0U) {
        eth_stream_quiesce(drv, -MR_EIO, MR_FALSE);
        return;
    }

    /* Acknowledge handled DMA sources */
    clear = status & ETH_STREAM_DMA_ACK_MASK;
    __HAL_ETH_DMA_CLEAR_IT(&priv->handle, clear);

    /* Return completed TX frames */
    eth_stream_tx_complete(drv);

    /* Drain RX and replenish the descriptor ring */
    for (count = 0; count < ETH_RX_DESC_CNT; count++) {
        /* Serialize HAL allocation with nested submissions */
        mask = mr_spinlock_lock_irqsave(&priv->lock);

        ret = eth_rx_next(priv, &slot);

        /* Unlock before raising any completed frame */
        mr_spinlock_unlock_irqrestore(&priv->lock, mask);

        if (ret == -MR_EAGAIN) {
            /* The RX ring is drained */
            return;
        }
        if (ret != 0) {
            /* Stop the stream on a receive failure */
            eth_stream_quiesce(drv, -MR_EIO, MR_FALSE);
            return;
        }

        /* Deliver the completed frame to its waiter */
        eth_stream_rx_complete(drv, slot);
    }

    /* Reschedule RX at the IRQ drain limit */
    HAL_NVIC_SetPendingIRQ(priv->stream.irq);
}
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

/* Instance install */
#define ST_HT_ETH_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__eth_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static const eth_pin_t MR_CAT(__eth_pins_, _id)[] = {                      \
        MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_pins, __ST_HT_ETH_PIN),         \
    };                                                                         \
    MR_ALIGN(ETH_DMA_ALIGN)                                                    \
    __ST_HT_ETH_DMA_SECTION(_id)                                               \
    static eth_dma_t MR_CAT(__eth_dma_, _id);                                  \
    __ST_HT_ETH_CALLBACK_DECLARE(_id)                                          \
    static eth_priv_t MR_CAT(__eth_priv_, _id) = {                             \
        .rx_link = MR_CAT(__eth_rx_link_, _id),                                \
        .rx_allocate = MR_CAT(__eth_rx_allocate_, _id),                        \
        .clk_enable = MR_CAT(__eth_clk_enable_, _id),                          \
        .pins = MR_CAT(__eth_pins_, _id),                                      \
        .pin_count = MR_HT_DRIVER_PROP_ELEM_NUM(_id, st_pins),                 \
        .instance = MR_HT_DRIVER_PROP(_id, st_ethx),                           \
        .dma = &MR_CAT(__eth_dma_, _id),                                       \
        .phy_addr =                                                            \
            MR_HT_DRIVER_PROP_OR(_id, st_phy_address,                          \
                                 MR_HT_DRIVER_PROP(_id, st_phy_addr)),         \
        .media = MR_HT_DRIVER_PROP_OR(_id, st_media, HAL_ETH_RMII_MODE),       \
        .tx_prefetch =                                                         \
            (MR_HT_DRIVER_PROP_OR(_id, st_tx_prefetch, 0) == 1),               \
        .lock = MR_SPINLOCK_INIT(),                                            \
        __ST_HT_ETH_STREAM_INIT(_id)                                           \
    };                                                                         \
    __ST_HT_ETH_CALLBACK_DEFINE(_id)                                           \
    __ST_HT_ETH_STREAM_IRQ(_id)                                                \
    MR_HT_ETH_INSTALL(_id, &MR_CAT(__eth_priv_, _id), _ops)
#define __ST_HT_ETH_DMA_SECTION(_id)                                           \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_dma_section),                    \
                (MR_SECTION(MR_HT_DRIVER_PROP(_id, st_dma_section))), ())
#define __ST_HT_ETH_PIN(_elem, _node, _prop)                                   \
    {                                                                          \
        .spec = {                                                              \
            .drv = MR_HT_DRIVER_FIND(                                          \
                MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),                 \
            .pin = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),               \
        },                                                                     \
        .alternate = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 2),             \
    }
#define __ST_HT_ETH_CALLBACK_DECLARE(_id)                                      \
    static void MR_CAT(__eth_rx_allocate_, _id)(uint8_t **buf);                \
    static void MR_CAT(__eth_rx_link_, _id)(void **start, void **end,          \
                                            uint8_t *buf, uint16_t size);
#define __ST_HT_ETH_CALLBACK_DEFINE(_id)                                       \
    static void MR_CAT(__eth_rx_allocate_, _id)(uint8_t **buf) {               \
        eth_rx_allocate(&MR_CAT(__eth_priv_, _id), buf);                       \
    }                                                                          \
    static void MR_CAT(__eth_rx_link_, _id)(void **start, void **end,          \
                                            uint8_t *buf, uint16_t size) {     \
        eth_rx_link(&MR_CAT(__eth_priv_, _id), start, end, buf, size);         \
    }
#if defined(MR_USE_DRIVER_ETH_STREAM)
#define __ST_HT_ETH_STREAM_INIT(_id)                                           \
    .stream = {                                                                \
        .dma_base = MR_HT_DRIVER_PROP_ELEM(_id, st_dma_region, 0, 0),          \
        .dma_size = MR_HT_DRIVER_PROP_ELEM(_id, st_dma_region, 0, 1),          \
        .irq = MR_HT_DRIVER_PROP(_id, st_irqn),                                \
        .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority),               \
    },
#define __ST_HT_ETH_STREAM_IRQ(_id)                                            \
    MR_STATIC_ASSERT(eth_requires_st_irq_handler,                              \
                     MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler));              \
    void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {                        \
        eth_irq_handle(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));                  \
    }
#else
#define __ST_HT_ETH_STREAM_INIT(_id)
#define __ST_HT_ETH_STREAM_IRQ(_id)
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
MR_HT_DRIVER_FOREACH(ST_HT_ETH_INSTALL, &__eth_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ETH) */

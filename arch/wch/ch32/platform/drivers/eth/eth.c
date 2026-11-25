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
#include <mrX/port/mr_port_clock.h>
#include <mrX/port/mr_port_irq.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_eth
#if MR_HT_HAS_DRIVER()
#if defined(MR_USE_CH32V4X7)
#include "ch32v4x7_eth.h"
#define CH32_ETH_USE_DMA
#elif defined(MR_USE_CH32V30X)
#include "ch32v30x_eth.h"
#include "ch32v30x_rcc.h"
#define CH32_ETH_USE_DMA
#elif !defined(MR_USE_CH32V20X)
#error "No Ethernet register backend for this CH32 EVT SDK"
#endif /* CH32 Ethernet register backend */

/* Driver ETH operations */
static mr_err_t eth_configure(const mr_driver_t *drv,
                              const mr_eth_config_t *config);
static mr_err_t eth_link(const mr_driver_t *drv, mr_eth_link_t *link);
static mr_ssize_t eth_read(const mr_driver_t *drv, void *buf, mr_size_t size);
static mr_ssize_t eth_write(const mr_driver_t *drv, const void *buf,
                            mr_size_t size);
#if defined(MR_USE_DRIVER_ETH_STREAM)
static mr_err_t eth_rx_stream_start(const mr_driver_t *drv);
static mr_err_t eth_rx_stream_stop(const mr_driver_t *drv);
static mr_err_t eth_rx_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t eth_rx_stream_submit(const mr_driver_t *drv,
                                     mr_eth_frame_t *frame);
static mr_err_t eth_tx_stream_start(const mr_driver_t *drv);
static mr_err_t eth_tx_stream_stop(const mr_driver_t *drv);
static mr_err_t eth_tx_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t eth_tx_stream_submit(const mr_driver_t *drv,
                                     mr_eth_frame_t *frame);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
MR_UNUSED static const mr_eth_ops_t __eth_ops = {
    .configure = eth_configure,
    .link = eth_link,
    .read = eth_read,
    .write = eth_write,
#if defined(MR_USE_DRIVER_ETH_STREAM)
    .rx_stream_start = eth_rx_stream_start,
    .rx_stream_stop = eth_rx_stream_stop,
    .rx_stream_is_busy = eth_rx_stream_is_busy,
    .rx_stream_submit = eth_rx_stream_submit,
    .tx_stream_start = eth_tx_stream_start,
    .tx_stream_stop = eth_tx_stream_stop,
    .tx_stream_is_busy = eth_tx_stream_is_busy,
    .tx_stream_submit = eth_tx_stream_submit,
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
};

/* Frame buffer definition */
#define CH32_ETH_FCS_SIZE               (4U)
#define CH32_ETH_DMA_SIZE               (1520U)

/* Clause 22 control and advertisement definition */
#define CH32_PHY_RESET                  (1U << 15)
#define CH32_PHY_LOOPBACK               (1U << 14)
#define CH32_PHY_SPEED_100              (1U << 13)
#define CH32_PHY_AUTO                   (1U << 12)
#define CH32_PHY_RESTART                (1U << 9)
#define CH32_PHY_FULL                   (1U << 8)
#define CH32_PHY_ADV_100_FULL           (1U << 8)
#define CH32_PHY_ADV_100_HALF           (1U << 7)
#define CH32_PHY_ADV_10_FULL            (1U << 6)
#define CH32_PHY_ADV_10_HALF            (1U << 5)
#define CH32_PHY_ADV_PAUSE              (1U << 10)
#define CH32_PHY_ADV_ASYM_PAUSE         (1U << 11)
#define CH32_PHY_ADV_SELECTOR           (1U)

/* IEEE 802.3 pause duration in quanta of 512 bit times */
#define CH32_ETH_PAUSE_QUANTA           (32U)

#if defined(MR_USE_CH32V30X)
/* CH32V317 integrated PHY register definition */
#define CH32_PHY_PAGE_SELECT            (0x1fU)
#define CH32_PHY_REPEATER_REG           (28U)
#define CH32_PHY_REPEATER_ENABLE        (1U << 13)
#define CH32_PHY_RMII_PAGE              (7U)
#define CH32_PHY_RMII_CLOCK_REG         (16U)
#define CH32_PHY_RMII_CLOCK_MASK        (0xffU << 4)
#define CH32_PHY_RMII_CLOCK_VALUE       (0x23U << 4)
#endif /* defined(MR_USE_CH32V30X) */

#if defined(CH32_ETH_USE_DMA)
/* ETH clock gate and DMA interrupt definition */
#if defined(MR_USE_CH32V4X7)
#define CH32_ETH_CLOCKS                 RCC_HBPeriph_ETH_MAC
#define CH32_ETH_FATAL_IRQ              ETH_DMAIER_FBEIE
#define CH32_ETH_DEFAULT_MDIO           MR_UINT32_MAX

/* Internal PHY control definition */
#define CH32_ETH_PHY_POWER_DOWN         (1U << 30)
#define CH32_ETH_PHY_RESET_N            (1U << 31)
#else
#define CH32_ETH_CLOCKS                                                        \
    (RCC_AHBPeriph_ETH_MAC | RCC_AHBPeriph_ETH_MAC_Tx                          \
     | RCC_AHBPeriph_ETH_MAC_Rx)
#define CH32_ETH_FATAL_IRQ              ETH_DMA_IT_FBE
#define CH32_ETH_DEFAULT_MDIO           ETH_MACMIIAR_CR_Div42
#endif /* defined(MR_USE_CH32V4X7) */
#define CH32_ETH_DMA_IRQS                                                      \
    (ETH_DMA_IT_NIS | ETH_DMA_IT_AIS | ETH_DMA_IT_R | ETH_DMA_IT_T             \
     | CH32_ETH_FATAL_IRQ | ETH_DMA_IT_RO | ETH_DMA_IT_TU)
#define CH32_ETH_RX_COUNT               (2U)

/* ETH DMA descriptor definition */
static ETH_DMADESCTypeDef __eth_rx_desc[CH32_ETH_RX_COUNT];
static ETH_DMADESCTypeDef __eth_tx_desc;
static MR_ALIGN(4) mr_uint8_t
    __eth_rx_buf[CH32_ETH_RX_COUNT][CH32_ETH_DMA_SIZE];
#else
/* ETH10M interrupt and SRAM address definition */
#define CH32_ETH10M_IRQS                                                       \
    (RB_ETH_EIE_INTIE | RB_ETH_EIE_RXIE | RB_ETH_EIE_TXIE                      \
     | RB_ETH_EIE_RXERIE | RB_ETH_EIE_TXERIE)
#define CH32_ETH10M_SRAM_BASE           (0x20000000UL)
#define CH32_ETH10M_SRAM_SIZE           (0x10000UL)
#define CH32_ETH10M_ANALOG              (5U << 1)

/* ETH10M RX buffer definition */
static MR_ALIGN(4) mr_uint8_t __eth_rx_buf[CH32_ETH_DMA_SIZE];
#endif /* defined(CH32_ETH_USE_DMA) */

/* ETH TX buffer definition */
static MR_ALIGN(4) mr_uint8_t __eth_tx_buf[CH32_ETH_DMA_SIZE];

#if defined(MR_USE_DRIVER_ETH_STREAM)
/* ETH stream type */
typedef struct eth_stream {
    mr_eth_frame_t *frame;
    mr_bool_t enabled;
} eth_stream_t;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

/* ETH private data type */
typedef struct eth_priv {
    mr_err_t (*prepare)(void);
    void (*unprepare)(void);
    mr_eth_config_t config;
    mr_uint32_t phy_addr;
    mr_uint32_t irq_priority;
    mr_bool_t internal_phy;
#if defined(MR_USE_CH32V30X)
    mr_bool_t internal_rmii_phy;
#endif /* defined(MR_USE_CH32V30X) */
    mr_bool_t active;
    mr_bool_t fault;
    mr_bool_t tx_pending;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    eth_stream_t rx_stream, tx_stream;
    mr_bool_t tx_stream_inflight, link_up;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
#if defined(CH32_ETH_USE_DMA)
    mr_uint32_t mdio_div;
    mr_uint32_t active_mdio_div;
    mr_uint32_t rx_index;
#else
    mr_ssize_t rx_pending;
#endif /* defined(CH32_ETH_USE_DMA) */
} eth_priv_t;

static void eth_dma_barrier(void) {
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

#if defined(CH32_ETH_USE_DMA)
static mr_err_t eth_wait_reg(volatile const uint32_t *reg, uint32_t mask,
                             mr_uint32_t attempts) {
    /* Poll the register bit until it clears */
    while ((*reg & mask) != 0U) {
        if (attempts-- == 0U) {
            return -MR_ETIMEDOUT;
        }
        mr_port_clock_busy_wait(10U);
    }
    return 0;
}
#endif /* defined(CH32_ETH_USE_DMA) */

static mr_err_t eth_phy_read(const eth_priv_t *priv, mr_uint32_t reg,
                             mr_uint16_t *value) {
    /* Read the PHY register */
#if defined(CH32_ETH_USE_DMA)
    mr_err_t ret;

    ret = eth_wait_reg(&ETH->MACMIIAR, ETH_MACMIIAR_MB, 100U);
    if (ret != 0) {
        return ret;
    }
    ETH->MACMIIAR = priv->active_mdio_div | (priv->phy_addr << 11) | (reg << 6)
                    | ETH_MACMIIAR_MB;
    ret = eth_wait_reg(&ETH->MACMIIAR, ETH_MACMIIAR_MB, 100U);
    if (ret != 0) {
        return ret;
    }
    *value = (mr_uint16_t)ETH->MACMIIDR;
#else
    MR_IGNORE(priv);
    R8_ETH_MIREGADR = (mr_uint8_t)reg;
    *value = R16_ETH_MIRD;
#endif /* defined(CH32_ETH_USE_DMA) */
    return 0;
}

static mr_err_t eth_phy_write(const eth_priv_t *priv, mr_uint32_t reg,
                              mr_uint16_t value) {
    /* Write the PHY register */
#if defined(CH32_ETH_USE_DMA)
    mr_err_t ret;

    ret = eth_wait_reg(&ETH->MACMIIAR, ETH_MACMIIAR_MB, 100U);
    if (ret != 0) {
        return ret;
    }
    ETH->MACMIIDR = value;
    ETH->MACMIIAR = priv->active_mdio_div | (priv->phy_addr << 11) | (reg << 6)
                    | ETH_MACMIIAR_MB | ETH_MACMIIAR_MW;
    return eth_wait_reg(&ETH->MACMIIAR, ETH_MACMIIAR_MB, 100U);
#else
    MR_IGNORE(priv);
    R32_ETH_MIWR = reg | (1U << 8) | ((mr_uint32_t)value << 16);
    return 0;
#endif /* defined(CH32_ETH_USE_DMA) */
}

static mr_bool_t eth_phy_supports_100(const eth_priv_t *priv) {
#if defined(MR_USE_CH32V4X7)
    MR_IGNORE(priv);
    return MR_TRUE;
#elif defined(MR_USE_CH32V30X)
    return priv->internal_rmii_phy || !priv->internal_phy;
#else
    return !priv->internal_phy;
#endif /* CH32 Ethernet PHY type */
}

#if defined(MR_USE_CH32V30X)
static mr_err_t eth_phy_update(const eth_priv_t *priv, mr_uint32_t reg,
                               mr_uint16_t mask, mr_uint16_t bits) {
    mr_uint16_t value;
    mr_err_t ret;

    /* Update the PHY register */
    ret = eth_phy_read(priv, reg, &value);
    if (ret != 0) {
        return ret;
    }
    return eth_phy_write(priv, reg, (value & ~mask) | bits);
}

static mr_err_t eth_phy_prepare_rmii(const eth_priv_t *priv) {
    mr_err_t ret, restore;

    /* Apply the integrated PHY's repeater and clock settings from WCH EVT */
    ret = eth_phy_write(priv, CH32_PHY_PAGE_SELECT, 0U);
    if (ret == 0) {
        ret = eth_phy_update(priv, CH32_PHY_REPEATER_REG, 0U,
                             CH32_PHY_REPEATER_ENABLE);
    }
    if (ret == 0) {
        ret = eth_phy_write(priv, CH32_PHY_PAGE_SELECT, CH32_PHY_RMII_PAGE);
    }
    if (ret == 0) {
        ret = eth_phy_update(priv, CH32_PHY_RMII_CLOCK_REG,
                             CH32_PHY_RMII_CLOCK_MASK, CH32_PHY_RMII_CLOCK_VALUE);
    }

    /* Restore Clause 22 register access even after a failed update */
    restore = eth_phy_write(priv, CH32_PHY_PAGE_SELECT, 0U);
    return (ret != 0) ? ret : restore;
}
#endif /* defined(MR_USE_CH32V30X) */

static mr_err_t eth_phy_configure(const eth_priv_t *priv,
                                  const mr_eth_config_t *config) {
    mr_uint16_t advert, control;
    mr_uint32_t attempt;
    mr_err_t ret;

    /* Reset the PHY before reconfiguring */
    ret = eth_phy_write(priv, PHY_BMCR, CH32_PHY_RESET);
    if (ret != 0) {
        return ret;
    }
    for (attempt = 0U; attempt < 100U; attempt++) {
        ret = eth_phy_read(priv, PHY_BMCR, &control);
        if (ret != 0) {
            return ret;
        }
        if ((control & CH32_PHY_RESET) == 0U) {
            break;
        }
        mr_port_clock_busy_wait(1000U);
    }
    if (attempt == 100U) {
        return -MR_ETIMEDOUT;
    }

#if defined(MR_USE_CH32V30X)
    if (priv->internal_rmii_phy) {
        ret = eth_phy_prepare_rmii(priv);
        if (ret != 0) {
            return ret;
        }
    }
#endif /* defined(MR_USE_CH32V30X) */

    /* Select loopback or a fixed speed and duplex mode */
    if (config->loopback == MR_ETH_LOOPBACK_PHY) {
        control = CH32_PHY_LOOPBACK;
    } else {
        control = 0U;
    }
    if (config->speed == MR_ETH_SPEED_100M) {
        control |= CH32_PHY_SPEED_100;
    }
    if (config->duplex != MR_ETH_DUPLEX_HALF) {
        control |= CH32_PHY_FULL;
    }
    if ((config->speed != MR_ETH_SPEED_AUTO)
        || (config->loopback != MR_ETH_LOOPBACK_NONE)) {
        return eth_phy_write(priv, PHY_BMCR, control);
    }

    /* Advertise the supported modes for auto-negotiation */
    advert = CH32_PHY_ADV_SELECTOR;
    if (config->duplex != MR_ETH_DUPLEX_HALF) {
        advert |= CH32_PHY_ADV_10_FULL;
        if (eth_phy_supports_100(priv)) {
            advert |= CH32_PHY_ADV_100_FULL;
        }
    }
    if (config->duplex != MR_ETH_DUPLEX_FULL) {
        advert |= CH32_PHY_ADV_10_HALF;
        if (eth_phy_supports_100(priv)) {
            advert |= CH32_PHY_ADV_100_HALF;
        }
    }
    if (config->flow.rx || config->flow.tx) {
        advert |= CH32_PHY_ADV_ASYM_PAUSE;
        if (config->flow.rx) {
            advert |= CH32_PHY_ADV_PAUSE;
        }
    }
    ret = eth_phy_write(priv, PHY_ANAR, advert);
    if (ret != 0) {
        return ret;
    }
    return eth_phy_write(priv, PHY_BMCR, CH32_PHY_AUTO | CH32_PHY_RESTART);
}

static void eth_mac_link(mr_eth_speed_t speed, mr_eth_duplex_t duplex) {
    /* Apply the speed and duplex mode to the MAC */
#if defined(CH32_ETH_USE_DMA)
    mr_uint32_t value;

    value = ETH->MACCR & ~(ETH_MACCR_FES | ETH_MACCR_DM);
    if (speed == MR_ETH_SPEED_100M) {
        value |= ETH_MACCR_FES;
    }
    if (duplex == MR_ETH_DUPLEX_FULL) {
        value |= ETH_MACCR_DM;
    }
    ETH->MACCR = value;
#else
    MR_IGNORE(speed);
    if (duplex == MR_ETH_DUPLEX_FULL) {
        R8_ETH_MACON2 |= RB_ETH_MACON2_FULDPX;
    } else {
        R8_ETH_MACON2 &= ~RB_ETH_MACON2_FULDPX;
    }
#endif /* defined(CH32_ETH_USE_DMA) */
}

static void eth_mac_flow(mr_bool_t rx, mr_bool_t tx) {
    /* Apply the flow control mode to the MAC */
#if defined(CH32_ETH_USE_DMA)
    mr_uint32_t flow;

    flow = rx ? ETH_MACFCR_RFCE : 0U;
    if (tx) {
        flow |= ETH_MACFCR_TFCE | (CH32_ETH_PAUSE_QUANTA << 16U);
    }
    /* FCBBPA is a command; never replay a busy bit from a register read */
    if ((ETH->MACFCR & ~ETH_MACFCR_FCBBPA) != flow) {
        ETH->MACFCR = flow;
    }
#else
    mr_uint8_t flow;

    flow = rx ? RB_ETH_MACON1_RXPAUS : 0U;
    MR_IGNORE(tx);
    if ((R8_ETH_MACON1 & RB_ETH_MACON1_RXPAUS) != flow) {
        R8_ETH_MACON1 = RB_ETH_MACON1_MARXEN | flow;
    }
#endif /* defined(CH32_ETH_USE_DMA) */
}

static void eth_pause_resolve(mr_eth_link_t *link, const mr_eth_config_t *config,
                              mr_uint16_t advert, mr_uint16_t peer) {
    /* IEEE 802.3 pause resolution precedes applying the requested directions */
    if ((advert & peer & CH32_PHY_ADV_PAUSE) != 0U) {
        link->flow.rx = config->flow.rx;
        link->flow.tx = config->flow.tx;
    } else if ((advert & peer & CH32_PHY_ADV_ASYM_PAUSE) != 0U) {
        link->flow.rx = config->flow.rx && ((advert & CH32_PHY_ADV_PAUSE) != 0U);
        link->flow.tx = config->flow.tx && ((peer & CH32_PHY_ADV_PAUSE) != 0U);
    }
}

static mr_err_t eth_link_refresh(eth_priv_t *priv, mr_eth_link_t *link) {
    mr_uint16_t status, advert, peer, common;
    mr_err_t ret;

    /* Initialize the link snapshot */
    link->up = MR_FALSE;
    link->speed = MR_ETH_SPEED_OFF;
    link->duplex = MR_ETH_DUPLEX_AUTO;
    link->flow.rx = 0U;
    link->flow.tx = 0U;
    if (!priv->active) {
        return 0;
    }
    if (priv->fault) {
        return -MR_EIO;
    }

    if (priv->config.loopback == MR_ETH_LOOPBACK_NONE) {
        /* The first BMSR read clears the latch-low link bit */
        ret = eth_phy_read(priv, PHY_BMSR, &status);
        if (ret == 0) {
            ret = eth_phy_read(priv, PHY_BMSR, &status);
        }
        if (ret != 0) {
            return ret;
        }
        if ((status == MR_UINT16_MAX) || (status == 0U)) {
            return -MR_ENODEV;
        }
        if ((status & PHY_Linked_Status) == 0U) {
            return 0;
        }
    } else {
        status = 0U;
    }

    link->speed = (priv->config.speed == MR_ETH_SPEED_100M)
                      ? MR_ETH_SPEED_100M : MR_ETH_SPEED_10M;
    link->duplex = (priv->config.duplex == MR_ETH_DUPLEX_HALF)
                       ? MR_ETH_DUPLEX_HALF : MR_ETH_DUPLEX_FULL;

    /* Resolve the negotiated or fixed link mode */
    if ((priv->config.speed == MR_ETH_SPEED_AUTO)
        && (priv->config.loopback == MR_ETH_LOOPBACK_NONE)) {
        if ((status & PHY_AutoNego_Complete) == 0U) {
            link->speed = MR_ETH_SPEED_OFF;
            link->duplex = MR_ETH_DUPLEX_AUTO;
            return 0;
        }
        ret = eth_phy_read(priv, PHY_ANAR, &advert);
        if (ret == 0) {
            ret = eth_phy_read(priv, PHY_ANLPAR, &peer);
        }
        if (ret != 0) {
            return ret;
        }
        common = advert & peer;

        /* Select the highest common advertised mode */
        if ((common & (CH32_PHY_ADV_100_FULL | CH32_PHY_ADV_100_HALF)) != 0U) {
            link->speed = MR_ETH_SPEED_100M;
            link->duplex = (common & CH32_PHY_ADV_100_FULL)
                               ? MR_ETH_DUPLEX_FULL : MR_ETH_DUPLEX_HALF;
        } else {
            link->duplex = (common & CH32_PHY_ADV_10_FULL)
                               ? MR_ETH_DUPLEX_FULL : MR_ETH_DUPLEX_HALF;
        }
        if (link->duplex == MR_ETH_DUPLEX_FULL) {
            eth_pause_resolve(link, &priv->config, advert, peer);
        }
    } else if (link->duplex == MR_ETH_DUPLEX_FULL) {
        link->flow = priv->config.flow;
    }

    /* Publish the resolved link */
    link->up = MR_TRUE;
    return 0;
}

#if defined(CH32_ETH_USE_DMA)
#if defined(MR_USE_CH32V4X7)
static mr_bool_t eth_mdio_div_valid(mr_uint32_t div) {
    return (div == ETH_MACMIIAR_CR_Div16) || (div == ETH_MACMIIAR_CR_Div26)
        || (div == ETH_MACMIIAR_CR_Div42) || (div == ETH_MACMIIAR_CR_Div62)
        || (div == ETH_MACMIIAR_CR_Div74) || (div == ETH_MACMIIAR_CR_Div102);
}

static mr_uint32_t eth_mdio_div(mr_uint32_t hclk) {
    /* Use the MACMIIAR clock ranges to keep MDC within the PHY limit */
    if (hclk < 35000000U) {
        return ETH_MACMIIAR_CR_Div16;
    }
    if (hclk < 60000000U) {
        return ETH_MACMIIAR_CR_Div26;
    }
    if (hclk < 90000000U) {
        return ETH_MACMIIAR_CR_Div42;
    }
    if (hclk < 120000000U) {
        return ETH_MACMIIAR_CR_Div62;
    }
    if (hclk < 150000000U) {
        return ETH_MACMIIAR_CR_Div74;
    }
    return ETH_MACMIIAR_CR_Div102;
}
#endif /* defined(MR_USE_CH32V4X7) */

static void eth_hw_stop(const eth_priv_t *priv) {
    /* Disable MAC and DMA transmission */
    ETH->DMAIER = 0U;
    ETH->DMAOMR &= ~(ETH_DMAOMR_ST | ETH_DMAOMR_SR);
    ETH->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);

    /* Reset aborts any transfer before descriptors can be reused */
#if defined(MR_USE_CH32V4X7)
    MR_IGNORE(priv);
    RCC->HBRSTR |= RCC_HBPeriph_ETH_MAC;
    MR_IGNORE(RCC->HBRSTR);
    RCC->HBRSTR &= ~RCC_HBPeriph_ETH_MAC;
    RCC->HBPCENR &= ~CH32_ETH_CLOCKS;
#else
    RCC->AHBRSTR |= RCC_AHBPeriph_ETH_MAC;
    MR_IGNORE(RCC->AHBRSTR);
    RCC->AHBRSTR &= ~RCC_AHBPeriph_ETH_MAC;
    RCC->AHBPCENR &= ~CH32_ETH_CLOCKS;
    if (priv->internal_phy && !priv->internal_rmii_phy) {
        EXTEN->EXTEN_CTR &= ~EXTEN_ETH_10M_EN;
    }
#endif /* defined(MR_USE_CH32V4X7) */
}

static mr_err_t eth_hw_prepare(eth_priv_t *priv, const mr_eth_config_t *config) {
    ETH_DMADESCTypeDef *desc;
    const mr_uint8_t *mac;
    mr_uint32_t i;
    mr_err_t ret;

    /* Enable the MAC clock and reset the DMA core */
#if defined(MR_USE_CH32V4X7)
    RCC->HBPCENR |= CH32_ETH_CLOCKS;
    MR_IGNORE(RCC->HBPCENR);
    RCC->HBRSTR |= RCC_HBPeriph_ETH_MAC;
    MR_IGNORE(RCC->HBRSTR);
    RCC->HBRSTR &= ~RCC_HBPeriph_ETH_MAC;
    ETH->PHY_CR &= ~CH32_ETH_PHY_POWER_DOWN;
    ETH->PHY_CR |= CH32_ETH_PHY_RESET_N;
    priv->active_mdio_div = (priv->mdio_div == CH32_ETH_DEFAULT_MDIO)
                                ? eth_mdio_div(SystemCoreClock)
                                : priv->mdio_div;
#else
    priv->active_mdio_div = priv->mdio_div;
    RCC->AHBPCENR |= CH32_ETH_CLOCKS;
    MR_IGNORE(RCC->AHBPCENR);
    if (priv->internal_rmii_phy) {
        /* Keep the board's RMII routing to the integrated 10/100 PHY */
        EXTEN->EXTEN_CTR &= ~(EXTEN_ETH_RGMII_SEL | EXTEN_ETH_10M_EN);
        RCC->AHBRSTR |= RCC_AHBPeriph_ETH_MAC;
        MR_IGNORE(RCC->AHBRSTR);
        RCC->AHBRSTR &= ~RCC_AHBPeriph_ETH_MAC;
    } else if (priv->internal_phy) {
        EXTEN->EXTEN_CTR &= ~EXTEN_ETH_RGMII_SEL;
        EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;
    }
#endif /* defined(MR_USE_CH32V4X7) */
    ETH->DMABMR = ETH_DMABMR_SR;
    ret = eth_wait_reg(&ETH->DMABMR, ETH_DMABMR_SR, 10000U);
    if (ret != 0) {
        return ret;
    }

    /* Keep FCS stripping in software so RX padding is preserved */
    mac = config->mac;
    ETH->MACA0HR = (mr_uint32_t)mac[4] | ((mr_uint32_t)mac[5] << 8);
    ETH->MACA0LR = (mr_uint32_t)mac[0] | ((mr_uint32_t)mac[1] << 8)
                   | ((mr_uint32_t)mac[2] << 16) | ((mr_uint32_t)mac[3] << 24);
    ETH->MACFFR = ETH_MACFFR_PAM;
#if defined(MR_USE_CH32V4X7)
    ETH->MACCR = 0U;
#else
    ETH->MACCR = (priv->internal_phy && !priv->internal_rmii_phy)
                     ? ETH_Internal_Pull_Up_Res_Enable : 0U;
#endif /* defined(MR_USE_CH32V4X7) */
    if (config->loopback == MR_ETH_LOOPBACK_MAC) {
        ETH->MACCR |= ETH_MACCR_LM;
    }
    if (config->csum.rx) {
        ETH->MACCR |= ETH_MACCR_IPCO;
    }
    ETH->MMCTIMR = ETH_MMCTIMR_TGFM | ETH_MMCTIMR_TGFMSCM | ETH_MMCTIMR_TGFSCM;
    ETH->MMCRIMR = ETH_MMCRIMR_RGUFM | ETH_MMCRIMR_RFAEM | ETH_MMCRIMR_RFCEM;

    /* Reset the RX and TX descriptor rings */
    priv->rx_index = 0U;
    for (i = 0U; i < CH32_ETH_RX_COUNT; i++) {
        desc = &__eth_rx_desc[i];
        desc->Buffer1Addr = (mr_uintptr_t)__eth_rx_buf[i];
        desc->Buffer2NextDescAddr
            = (mr_uintptr_t)&__eth_rx_desc[(i + 1U) % CH32_ETH_RX_COUNT];
        desc->ControlBufferSize = ETH_DMARxDesc_RCH | CH32_ETH_DMA_SIZE;
        desc->Status = ETH_DMARxDesc_OWN;
    }
    __eth_tx_desc.Buffer1Addr = (mr_uintptr_t)__eth_tx_buf;
    __eth_tx_desc.Buffer2NextDescAddr = (mr_uintptr_t)&__eth_tx_desc;
    __eth_tx_desc.ControlBufferSize = 0U;
    __eth_tx_desc.Status = ETH_DMATxDesc_TCH;
    eth_dma_barrier();
    ETH->DMARDLAR = (mr_uintptr_t)__eth_rx_desc;
    ETH->DMATDLAR = (mr_uintptr_t)&__eth_tx_desc;
    ETH->DMABMR = ETH_DMABMR_PBL_1Beat;
    ETH->DMAOMR = ETH_DMAOMR_TSF | ETH_DMAOMR_RSF;

    /* Configure DMA operation modes */
    if (!config->csum.rx) {
        ETH->DMAOMR |= ETH_DMAOMR_DTCEFD;
    }
    return 0;
}

static void eth_hw_start(void) {
    /* Acknowledge pending events and enable DMA interrupts */
    ETH->DMASR = CH32_ETH_DMA_IRQS | ETH_DMASR_RBUS | ETH_DMASR_TBUS;
    ETH->DMAIER = CH32_ETH_DMA_IRQS;

    /* Start the MAC and DMA transmission */
    ETH->MACCR |= ETH_MACCR_TE | ETH_MACCR_RE;
    ETH->DMAOMR |= ETH_DMAOMR_ST | ETH_DMAOMR_SR;
}

static mr_ssize_t eth_hw_read(eth_priv_t *priv, void *buf, mr_size_t size) {
    ETH_DMADESCTypeDef *desc;
    mr_uint32_t status, bytes;
    mr_ssize_t ret;

    /* Check descriptor ownership before reading the frame */
    desc = &__eth_rx_desc[priv->rx_index];
    status = desc->Status;
    if ((status & ETH_DMARxDesc_OWN) != 0U) {
        return 0;
    }
    eth_dma_barrier();

    /* Validate the received frame */
    bytes = (status & ETH_DMARxDesc_FL) >> ETH_DMARXDESC_FRAME_LENGTHSHIFT;
    if ((bytes > CH32_ETH_DMA_SIZE)
        || (bytes < MR_ETH_FRAME_MIN + CH32_ETH_FCS_SIZE)
        || ((status & (ETH_DMARxDesc_ES | ETH_DMARxDesc_FS | ETH_DMARxDesc_LS))
            != (ETH_DMARxDesc_FS | ETH_DMARxDesc_LS))) {
        ret = -MR_EIO;
    } else {
        bytes -= CH32_ETH_FCS_SIZE;
        if ((bytes > size) || ((bytes > MR_ETH_HEADER_LEN + priv->config.mtu)
                               && (bytes > MR_ETH_FRAME_MIN))) {
            ret = -MR_ERANGE;
        } else {
            mr_memcpy(buf, __eth_rx_buf[priv->rx_index], bytes);
            ret = (mr_ssize_t)bytes;
        }
    }
    priv->rx_index = (priv->rx_index + 1U) % CH32_ETH_RX_COUNT;

    /* Recycle the descriptor to the DMA ring */
    eth_dma_barrier();
    desc->Status = ETH_DMARxDesc_OWN;
    eth_dma_barrier();
    ETH->DMASR = ETH_DMASR_RBUS;
    ETH->DMARPDR = 0U;
    return ret;
}

static mr_uint32_t eth_tx_checksum(mr_size_t size) {
    const mr_uint8_t *ip;
    mr_uint32_t type;

    /* Hardware checksum insertion is only used for supported Ethernet II IP */
    ip = &__eth_tx_buf[MR_ETH_HEADER_LEN];
    type = ((mr_uint32_t)__eth_tx_buf[12] << 8U) | __eth_tx_buf[13];
    if ((type == 0x0800U) && (size >= (MR_ETH_HEADER_LEN + 20U))
        && ((ip[0] >> 4U) == 4U) && ((ip[0] & 0x0fU) >= 5U)
        && (size >= MR_ETH_HEADER_LEN + (ip[0] & 0x0fU) * 4U)) {
        /* Fragmented IPv4 packets have no complete transport payload */
        if (((ip[6] & 0x3fU) == 0U) && (ip[7] == 0U)
            && ((ip[9] == 1U) || (ip[9] == 6U) || (ip[9] == 17U))) {
            return ETH_DMATxDesc_CIC_TCPUDPICMP_Full;
        }
        return ETH_DMATxDesc_CIC_IPV4Header;
    }
    if ((type == 0x86ddU) && (size >= (MR_ETH_HEADER_LEN + 40U))
        && ((ip[0] >> 4U) == 6U)
        && ((ip[6] == 6U) || (ip[6] == 17U) || (ip[6] == 58U))) {
        return ETH_DMATxDesc_CIC_TCPUDPICMP_Full;
    }
    return ETH_DMATxDesc_CIC_ByPass;
}

static void eth_hw_write(const eth_priv_t *priv, mr_size_t size) {
    mr_uint32_t checksum;

    /* Select checksum insertion and arm the descriptor */
    checksum = priv->config.csum.tx ? eth_tx_checksum(size)
                                    : ETH_DMATxDesc_CIC_ByPass;
    __eth_tx_desc.ControlBufferSize = (mr_uint32_t)size;
    eth_dma_barrier();
    __eth_tx_desc.Status = ETH_DMATxDesc_OWN | ETH_DMATxDesc_TCH
                           | ETH_DMATxDesc_FS | ETH_DMATxDesc_LS
                           | ETH_DMATxDesc_IC | checksum;
    eth_dma_barrier();

    /* Trigger the DMA transmission */
    ETH->DMASR = ETH_DMASR_TBUS;
    ETH->DMATPDR = 0U;
}

static mr_uint32_t eth_hw_events(eth_priv_t *priv) {
    mr_uint32_t status, events, flow, last, tx_errors;

    /* Read and acknowledge the DMA event status */
    status = ETH->DMASR & CH32_ETH_DMA_IRQS;
    ETH->DMASR = status;
    events = 0U;
    if ((status & ETH_DMASR_FBES) != 0U) {
        ETH->DMAIER = 0U;
        priv->fault = MR_TRUE;
        priv->tx_pending = MR_FALSE;
        return MR_ETH_WAIT_RX_ERROR | MR_ETH_WAIT_TX_ERROR;
    }
    if ((status & ETH_DMASR_RS) != 0U) {
        events |= MR_ETH_WAIT_RX_READY;
    }
    if (priv->config.flow.tx && ((events & MR_ETH_WAIT_RX_READY) != 0U)) {
        /* Ask the MAC for a finite Pause when the RX ring is full */
        flow = ETH->MACFCR;
        last = (priv->rx_index + CH32_ETH_RX_COUNT - 1U) % CH32_ETH_RX_COUNT;
        if (((flow & (ETH_MACFCR_TFCE | ETH_MACFCR_FCBBPA)) == ETH_MACFCR_TFCE)
            && ((__eth_rx_desc[last].Status & ETH_DMARxDesc_OWN) == 0U)) {
            ETH->MACFCR = flow | ETH_MACFCR_FCBBPA;
        }
    }
    if ((status & ETH_DMASR_ROS) != 0U) {
        events |= MR_ETH_WAIT_RX_ERROR;
    }
    if (priv->tx_pending && ((__eth_tx_desc.Status & ETH_DMATxDesc_OWN) == 0U)) {
        priv->tx_pending = MR_FALSE;
        tx_errors = ETH_DMATxDesc_ES;
        if (priv->config.csum.tx) {
            tx_errors |= ETH_DMATxDesc_IHE | ETH_DMATxDesc_PCE;
        }
        events |= ((__eth_tx_desc.Status & tx_errors) != 0U)
                      ? MR_ETH_WAIT_TX_ERROR : MR_ETH_WAIT_TX_DONE;
    }
    return events;
}
#else
static void eth_hw_stop(const eth_priv_t *priv) {
    MR_IGNORE(priv);

    /* Reset the MAC and disable the internal 10M PHY */
    R8_ETH_EIE = 0U;
    R8_ETH_ECON1 = RB_ETH_ECON1_TXRST | RB_ETH_ECON1_RXRST;
    MR_IGNORE(R8_ETH_ECON1);
    R8_ETH_ECON1 = 0U;
    EXTEN->EXTEN_CTR &= ~EXTEN_ETH_10M_EN;
}

static mr_err_t eth_hw_prepare(eth_priv_t *priv, const mr_eth_config_t *config) {
    const mr_uint8_t *mac;
    mr_uintptr_t rx, tx;

    /* The 16-bit DMA address registers only see the first 64 KiB of SRAM */
    rx = (mr_uintptr_t)__eth_rx_buf;
    tx = (mr_uintptr_t)__eth_tx_buf;
    if ((rx < CH32_ETH10M_SRAM_BASE) || (tx < CH32_ETH10M_SRAM_BASE)
        || (rx - CH32_ETH10M_SRAM_BASE > CH32_ETH10M_SRAM_SIZE - CH32_ETH_DMA_SIZE)
        || (tx - CH32_ETH10M_SRAM_BASE > CH32_ETH10M_SRAM_SIZE - CH32_ETH_DMA_SIZE)) {
        return -MR_ERANGE;
    }

    R8_ETH_EIE = RB_ETH_EIE_R_EN50;

    /* Reset the MAC and program its registers */
    R8_ETH_ECON1 = RB_ETH_ECON1_TXRST | RB_ETH_ECON1_RXRST;
    R8_ETH_ECON1 = 0U;
    R8_ETH_ECON2 = (R8_ETH_ECON2 & ~RB_ETH_ECON2_RX) | CH32_ETH10M_ANALOG;
    R8_ETH_ERXFCON = 0U;
    mac = config->mac;
    R8_ETH_MAADRL1 = mac[5];
    R8_ETH_MAADRL2 = mac[4];
    R8_ETH_MAADRL3 = mac[3];
    R8_ETH_MAADRL4 = mac[2];
    R8_ETH_MAADRL5 = mac[1];
    R8_ETH_MAADRL6 = mac[0];
    R8_ETH_MACON1 = RB_ETH_MACON1_MARXEN;
    R8_ETH_MACON2 = PADCFG_AUTO_3 | RB_ETH_MACON2_TXCRCEN;
    R16_ETH_MAMXFL = CH32_ETH_DMA_SIZE;
    R16_ETH_ETXST = (mr_uint16_t)tx;
    R16_ETH_ERXST = (mr_uint16_t)rx;
    EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;
    priv->rx_pending = 0;
    return 0;
}

static void eth_hw_start(void) {
    /* Acknowledge pending events and enable interrupts */
    R8_ETH_EIR = 0xffU;
    R8_ETH_ESTAT = RB_ETH_ESTAT_INT | RB_ETH_ESTAT_BUFER;
    R8_ETH_EIE |= CH32_ETH10M_IRQS;
    R8_ETH_ECON1 |= RB_ETH_ECON1_RXEN;
}

static mr_ssize_t eth_hw_read(eth_priv_t *priv, void *buf, mr_size_t size) {
    mr_size_t bytes;
    mr_ssize_t ret;

    /* Consume the pending frame captured by the IRQ */
    ret = priv->rx_pending;
    if (ret == 0) {
        return 0;
    }

    /* Validate the received frame */
    if (ret > 0) {
        bytes = (mr_size_t)ret;
        if ((bytes > size) || ((bytes > MR_ETH_HEADER_LEN + priv->config.mtu)
                               && (bytes > MR_ETH_FRAME_MIN))) {
            ret = -MR_ERANGE;
        } else {
            eth_dma_barrier();
            mr_memcpy(buf, __eth_rx_buf, bytes);
        }
    }
    priv->rx_pending = 0;

    /* Re-enable the receiver */
    R8_ETH_ESTAT = RB_ETH_ESTAT_BUFER;
    R8_ETH_EIR = RB_ETH_EIR_RXIF | RB_ETH_EIR_RXERIF;
    eth_dma_barrier();
    R8_ETH_ECON1 |= RB_ETH_ECON1_RXEN;
    return ret;
}

static void eth_hw_write(const eth_priv_t *priv, mr_size_t size) {
    MR_IGNORE(priv);

    /* Arm the frame length and trigger the transmission */
    R16_ETH_ETXLN = (mr_uint16_t)size;
    eth_dma_barrier();
    R8_ETH_ECON1 |= RB_ETH_ECON1_TXRTS;
}

static mr_uint32_t eth_hw_events(eth_priv_t *priv) {
    mr_uint8_t irq, status;
    mr_uint32_t events;
    mr_uint16_t bytes;

    /* Read the pending interrupt status */
    irq = R8_ETH_EIR;
    status = R8_ETH_ESTAT;
    events = 0U;
    if (((irq & (RB_ETH_EIR_RXIF | RB_ETH_EIR_RXERIF)) != 0U)
        && (priv->rx_pending == 0)) {
        /* The next frame must not overwrite this one before the caller reads it */
        R8_ETH_ECON1 &= ~RB_ETH_ECON1_RXEN;
        bytes = R16_ETH_ERXLN;
        if (((irq & RB_ETH_EIR_RXERIF) != 0U)
            || ((status & (RB_ETH_ESTAT_BUFER | RB_ETH_ESTAT_RXCRCER
                           | RB_ETH_ESTAT_RXNIBBLE | RB_ETH_ESTAT_RXMORE)) != 0U)
            || (bytes < MR_ETH_FRAME_MIN) || (bytes > MR_ETH_FRAME_MAX)) {
            priv->rx_pending = -MR_EIO;
            events |= MR_ETH_WAIT_RX_ERROR;
        } else {
            /* ETH10M reports the DMA byte count without FCS */
            priv->rx_pending = (mr_ssize_t)bytes;
            events |= MR_ETH_WAIT_RX_READY;
        }
    }
    if (priv->tx_pending
        && ((irq & (RB_ETH_EIR_TXIF | RB_ETH_EIR_TXERIF)) != 0U)) {
        priv->tx_pending = MR_FALSE;
        events |= ((irq & RB_ETH_EIR_TXERIF) != 0U)
                      ? MR_ETH_WAIT_TX_ERROR : MR_ETH_WAIT_TX_DONE;
    }
    R8_ETH_EIR = irq;
    R8_ETH_ESTAT = RB_ETH_ESTAT_INT;
    return events;
}
#endif /* defined(CH32_ETH_USE_DMA) */

#if defined(MR_USE_DRIVER_ETH_STREAM)
static void eth_stream_return(const mr_driver_t *drv, mr_eth_frame_t *frame,
                              mr_ssize_t result, mr_bool_t rx) {
    mr_uint32_t event;

    if (frame == MR_NULL) {
        return;
    }

    /* Publish frame status before notification */
    frame->status = (result < 0) ? (mr_err_t)result : 0;
    if (rx) {
        frame->size = (result < 0) ? 0U : (mr_size_t)result;
        event = (result < 0) ? MR_ETH_WAIT_RX_ERROR : MR_ETH_WAIT_RX_READY;
    } else {
        event = (result < 0) ? MR_ETH_WAIT_TX_ERROR : MR_ETH_WAIT_TX_DONE;
    }

    /* Notify waiters while the driver is alive */
    if (!MR_DRIVER_IS_DYING(drv)) {
        mr_eth_event_raise((mr_driver_t *)drv, event, frame);
    }
}

static mr_eth_frame_t *eth_stream_cancel(eth_stream_t *stream) {
    mr_eth_frame_t *frame;

    /* Detach the frame from its stream */
    frame = stream->frame;
    stream->frame = MR_NULL;
    stream->enabled = MR_FALSE;
    return frame;
}
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

static void eth_stop(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    mr_eth_frame_t *rx, *tx;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Lock */
    mask = mr_port_irq_disable();

    /* Stop the hardware and cancel pending work */
    priv->active = MR_FALSE;
    NVIC_DisableIRQ(ETH_IRQn);
    eth_hw_stop(priv);
    NVIC_ClearPendingIRQ(ETH_IRQn);
    priv->tx_pending = MR_FALSE;
    priv->fault = MR_FALSE;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    rx = eth_stream_cancel(&priv->rx_stream);
    tx = eth_stream_cancel(&priv->tx_stream);
    priv->tx_stream_inflight = MR_FALSE;
    priv->link_up = MR_FALSE;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Unlock */
    mr_port_irq_enable(mask);

    /* Unprepare the board resources */
    if (priv->unprepare != MR_NULL) {
        priv->unprepare();
    }

    /* Return cancelled frames to their waiters */
#if defined(MR_USE_DRIVER_ETH_STREAM)
    eth_stream_return(drv, rx, -MR_EPERM, MR_TRUE);
    eth_stream_return(drv, tx, -MR_EPERM, MR_FALSE);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
}

static mr_err_t eth_configure(const mr_driver_t *drv,
                              const mr_eth_config_t *config) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Stop the MAC when the configuration turns it off */
    if (config->speed == MR_ETH_SPEED_OFF) {
        if (priv->active) {
            eth_stop(drv);
        }
        return 0;
    }

    /* Check ETH capabilities */
    if (((config->flow.rx || config->flow.tx)
         && (config->duplex == MR_ETH_DUPLEX_HALF))
        || (config->speed > MR_ETH_SPEED_100M)
        || ((config->speed == MR_ETH_SPEED_100M) && !eth_phy_supports_100(priv))) {
        return -MR_ENOSYS;
    }
#if !defined(CH32_ETH_USE_DMA)
    /* ETH10M does not share the descriptor checksum engine */
    if (config->flow.tx || config->csum.rx || config->csum.tx) {
        return -MR_ENOSYS;
    }
#else
    if (priv->phy_addr > 31U) {
        return -MR_EINVAL;
    }
#endif /* !defined(CH32_ETH_USE_DMA) */
#if defined(MR_USE_CH32V4X7)
    if (!priv->internal_phy) {
        return -MR_ENOSYS;
    }
    if ((priv->mdio_div != CH32_ETH_DEFAULT_MDIO)
        && !eth_mdio_div_valid(priv->mdio_div)) {
        return -MR_EINVAL;
    }
#endif /* defined(MR_USE_CH32V4X7) */
#if defined(MR_USE_CH32V20X)
    if (config->loopback == MR_ETH_LOOPBACK_MAC) {
        return -MR_ENOSYS;
    }
#endif /* defined(MR_USE_CH32V20X) */
    /* Prepare the board resources */
    if (priv->active) {
        eth_stop(drv);
    }
    ret = priv->prepare();
    if (ret != 0) {
        return ret;
    }

    /* Prepare the hardware and the PHY */
    ret = eth_hw_prepare(priv, config);
    if (ret == 0) {
        ret = eth_phy_configure(priv, config);
    }
    if (ret != 0) {
        eth_stop(drv);
        return ret;
    }

    /* Apply the resolved MAC mode */
    priv->config = *config;
    eth_mac_link((config->speed == MR_ETH_SPEED_100M)
                     ? MR_ETH_SPEED_100M : MR_ETH_SPEED_10M,
                 (config->duplex == MR_ETH_DUPLEX_HALF)
                     ? MR_ETH_DUPLEX_HALF : MR_ETH_DUPLEX_FULL);

    /* Lock */
    mask = mr_port_irq_disable();

    /* Start the MAC */
    NVIC_ClearPendingIRQ(ETH_IRQn);
    NVIC_SetPriority(ETH_IRQn, (mr_uint8_t)priv->irq_priority);
    priv->active = MR_TRUE;
    eth_hw_start();
    NVIC_EnableIRQ(ETH_IRQn);

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t eth_link(const mr_driver_t *drv, mr_eth_link_t *link) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Refresh the link before applying the MAC mode */
    ret = eth_link_refresh(priv, link);

    /* Lock */
    mask = mr_port_irq_disable();

    /* Apply the resolved link mode to the MAC */
    if (priv->active && !priv->fault) {
        if ((ret == 0) && link->up) {
            eth_mac_link(link->speed, link->duplex);
            eth_mac_flow(link->flow.rx, link->flow.tx);
        } else {
            eth_mac_flow(MR_FALSE, MR_FALSE);
        }
    }
#if defined(MR_USE_DRIVER_ETH_STREAM)
    priv->link_up = (ret == 0) && link->up && priv->active && !priv->fault;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_ssize_t eth_read(const mr_driver_t *drv, void *buf, mr_size_t size) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_ssize_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check the receive state */
    if (!priv->active) {
        ret = 0;
    } else if (priv->fault) {
        ret = -MR_EIO;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    } else if (priv->rx_stream.enabled) {
        ret = -MR_EBUSY;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    } else {
        ret = eth_hw_read(priv, buf, size);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static void eth_send(eth_priv_t *priv, const void *buf, mr_size_t size) {
    /* Copy the frame into the backend TX buffer */
    mr_memcpy(__eth_tx_buf, buf, size);
    priv->tx_pending = MR_TRUE;
    eth_hw_write(priv, size);
}

static mr_ssize_t eth_write(const mr_driver_t *drv, const void *buf,
                            mr_size_t size) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_eth_link_t link;
    mr_err_t ret;
    int mask;

#if defined(MR_USE_DRIVER_ETH_STREAM)
    /* Lock */
    mask = mr_port_irq_disable();

    /* Check TX stream ownership */
    if (priv->tx_stream.enabled) {
        /* Unlock */
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Refresh the link before transmitting */
    ret = eth_link(drv, &link);
    if (ret != 0) {
        return ret;
    }
    if (!link.up) {
        return -MR_EAGAIN;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check the transmit state */
    if (!priv->active) {
        ret = -MR_ENODEV;
    } else if (priv->fault) {
        ret = -MR_EIO;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    } else if (priv->tx_stream.enabled) {
        ret = -MR_EBUSY;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    } else if (priv->tx_pending) {
        ret = -MR_EAGAIN;
    } else {
        eth_send(priv, buf, size);
        ret = (mr_ssize_t)size;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

#if defined(MR_USE_DRIVER_ETH_STREAM)
static mr_err_t eth_stream_start(eth_priv_t *priv, eth_stream_t *stream) {
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check the stream state */
    if (!priv->active) {
        ret = -MR_ENODEV;
    } else if (priv->fault) {
        ret = -MR_EIO;
    } else if (stream->enabled) {
        ret = -MR_EBUSY;
    } else {
        stream->enabled = MR_TRUE;
        ret = 0;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t eth_rx_stream_start(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Start the RX stream */
    return eth_stream_start(priv, &priv->rx_stream);
}

static mr_err_t eth_tx_stream_start(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_eth_link_t link;
    mr_err_t ret;

    /* Refresh the link and start the TX stream */
    ret = eth_link(drv, &link);
    return (ret != 0) ? ret : eth_stream_start(priv, &priv->tx_stream);
}

static mr_err_t eth_stream_stop(const mr_driver_t *drv, eth_stream_t *stream,
                                mr_bool_t rx) {
    mr_eth_frame_t *frame;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();
    frame = eth_stream_cancel(stream);

    /* Unlock */
    mr_port_irq_enable(mask);

    /* Hardware only accesses backend buffers, including after TX cancel */
    eth_stream_return(drv, frame, -MR_EPERM, rx);
    return 0;
}

static mr_err_t eth_rx_stream_stop(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Stop the RX stream */
    return eth_stream_stop(drv, &priv->rx_stream, MR_TRUE);
}

static mr_err_t eth_tx_stream_stop(const mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Stop the TX stream */
    return eth_stream_stop(drv, &priv->tx_stream, MR_FALSE);
}

static mr_err_t eth_stream_is_busy(const eth_stream_t *stream, mr_bool_t *busy) {
    int mask;

    /* Get stream state */
    mask = mr_port_irq_disable();
    *busy = stream->enabled;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t eth_rx_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    const eth_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get RX stream state */
    return eth_stream_is_busy(&priv->rx_stream, busy);
}

static mr_err_t eth_tx_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    const eth_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get TX stream state */
    return eth_stream_is_busy(&priv->tx_stream, busy);
}

static mr_bool_t eth_stream_conflicts(const mr_eth_frame_t *frame,
                                      mr_size_t size,
                                      const mr_eth_frame_t *pending,
                                      mr_size_t pending_size) {
    mr_uintptr_t addr, other;

    if (frame == pending) {
        return MR_TRUE;
    }

    /* Use start distances to avoid address overflow */
    addr = (mr_uintptr_t)frame->buf;
    other = (mr_uintptr_t)pending->buf;
    return (addr <= other) ? ((other - addr) < size)
                           : ((addr - other) < pending_size);
}

static mr_err_t eth_rx_stream_submit(const mr_driver_t *drv,
                                     mr_eth_frame_t *frame) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_eth_frame_t *tx;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check the RX stream state */
    tx = priv->tx_stream.frame;
    if (!priv->rx_stream.enabled) {
        ret = -MR_EPERM;
    } else if ((frame == priv->rx_stream.frame)
               || ((tx != MR_NULL) && eth_stream_conflicts(frame,
                                                          frame->capacity,
                                                          tx, tx->size))) {
        ret = -MR_EBUSY;
    } else if (priv->rx_stream.frame != MR_NULL) {
        ret = -MR_EAGAIN;
    } else {
        priv->rx_stream.frame = frame;
        /* Service frames already waiting in the MAC without a fresh edge */
        NVIC_SetPendingIRQ(ETH_IRQn);
        ret = 0;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t eth_tx_stream_submit(const mr_driver_t *drv,
                                     mr_eth_frame_t *frame) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_eth_frame_t *rx;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check the TX stream state */
    rx = priv->rx_stream.frame;
    if (!priv->tx_stream.enabled) {
        ret = -MR_EPERM;
    } else if ((frame == priv->tx_stream.frame)
               || ((rx != MR_NULL) && eth_stream_conflicts(frame, frame->size,
                                                          rx, rx->capacity))) {
        ret = -MR_EBUSY;
    } else if (!priv->link_up || priv->tx_pending) {
        ret = -MR_EAGAIN;
    } else {
        priv->tx_stream.frame = frame;
        priv->tx_stream_inflight = MR_TRUE;
        eth_send(priv, frame->buf, frame->size);
        ret = 0;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

MR_UNUSED static void eth_irq_handle(mr_driver_t *drv) {
    eth_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_eth_data_t *data;
    mr_uint32_t events;
    int mask;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    mr_ssize_t rx_result, tx_result;
    mr_eth_frame_t *rx, *tx;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check the MAC state */
    if (!priv->active) {
        /* Unlock */
        mr_port_irq_enable(mask);
        return;
    }
    events = eth_hw_events(priv);
#if defined(MR_USE_DRIVER_ETH_STREAM)
    /* Detach completions before notifying waiters that can submit again */
    rx = MR_NULL;
    tx = MR_NULL;
    rx_result = -MR_EIO;
    tx_result = -MR_EIO;
    if (priv->fault) {
        rx = eth_stream_cancel(&priv->rx_stream);
        tx = eth_stream_cancel(&priv->tx_stream);
        priv->link_up = MR_FALSE;
    } else if (priv->rx_stream.enabled) {
        events &= ~MR_ETH_WAIT_RX_READY;
        rx = priv->rx_stream.frame;
        if (rx != MR_NULL) {
            rx_result = eth_hw_read(priv, rx->buf, rx->capacity);
            if (rx_result == 0) {
                rx = MR_NULL;
            } else {
                priv->rx_stream.frame = MR_NULL;
            }
        }
    }
    if (rx != MR_NULL) {
        events &= ~(MR_ETH_WAIT_RX_READY | MR_ETH_WAIT_RX_ERROR);
    }
    if (priv->tx_stream_inflight && !priv->tx_pending) {
        if (!priv->fault) {
            tx = priv->tx_stream.frame;
            priv->tx_stream.frame = MR_NULL;
            tx_result = ((events & MR_ETH_WAIT_TX_DONE) != 0U) ? 0 : -MR_EIO;
        }
        priv->tx_stream_inflight = MR_FALSE;
        events &= ~(MR_ETH_WAIT_TX_DONE | MR_ETH_WAIT_TX_ERROR);
    }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Unlock */
    mr_port_irq_enable(mask);

    /* Return detached frames to their waiters */
#if defined(MR_USE_DRIVER_ETH_STREAM)
    eth_stream_return(drv, rx, rx_result, MR_TRUE);
    eth_stream_return(drv, tx, tx_result, MR_FALSE);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Raise events for the remaining event masks */
    data = MR_DRIVER_DATA(drv);
    if (MR_DRIVER_IS_SETUP(drv) && !MR_DRIVER_IS_DYING(drv)
        && MR_OBJECT_IS_INITED(&data->event)) {
        if ((events & MR_ETH_WAIT_RX_ERROR) != 0U) {
            mr_eth_event_raise(drv, MR_ETH_WAIT_RX_ERROR, MR_NULL);
        }
        if ((events & MR_ETH_WAIT_RX_READY) != 0U) {
            mr_eth_event_raise(drv, MR_ETH_WAIT_RX_READY, MR_NULL);
        }
        if ((events & MR_ETH_WAIT_TX_ERROR) != 0U) {
            mr_eth_event_raise(drv, MR_ETH_WAIT_TX_ERROR, MR_NULL);
        }
        if ((events & MR_ETH_WAIT_TX_DONE) != 0U) {
            mr_eth_event_raise(drv, MR_ETH_WAIT_TX_DONE, MR_NULL);
        }
    }
}

/* Instance install */
#define CH32_HT_ETH_INSTALL(_id, _ops)                                         \
    extern mr_err_t MR_HT_DRIVER_PROP(_id, wch_prepare)(void);                 \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_unprepare),                     \
                (extern void MR_HT_DRIVER_PROP(_id, wch_unprepare)(void);),    \
                ())                                                            \
    static eth_priv_t MR_CAT(__eth_priv_, _id) = {                             \
        .prepare = MR_HT_DRIVER_PROP(_id, wch_prepare),                        \
        .unprepare = MR_HT_DRIVER_PROP_OR(_id, wch_unprepare, MR_NULL),        \
        .phy_addr = MR_HT_DRIVER_PROP_OR(_id, wch_phy_address, 1U),            \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U),       \
        .internal_phy = __CH32_HT_ETH_INTERNAL_PHY(_id),                       \
        __CH32_HT_ETH_RMII_PHY(_id)                                            \
        __CH32_HT_ETH_MDIO(_id)                                                \
    };                                                                         \
    void ETH_IRQHandler(void) MR_CH32_INTERRUPT;                               \
    void ETH_IRQHandler(void) {                                                \
        eth_irq_handle(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));                  \
    }                                                                          \
    MR_HT_ETH_INSTALL(_id, &MR_CAT(__eth_priv_, _id), _ops)
#if defined(CH32_ETH_USE_DMA)
/* Internal PHY selection and management clock definition */
#define __CH32_HT_ETH_INTERNAL_PHY(_id)                                        \
    MR_HT_DRIVER_PROP_OR(_id, wch_internal_phy, 1U)
#define __CH32_HT_ETH_MDIO(_id)                                                \
    .mdio_div =                                                                \
        MR_HT_DRIVER_PROP_OR(_id, wch_mdio_div, CH32_ETH_DEFAULT_MDIO),
#else
#define __CH32_HT_ETH_INTERNAL_PHY(_id)     (1U)
#define __CH32_HT_ETH_MDIO(_id)
#endif /* defined(CH32_ETH_USE_DMA) */

#if defined(MR_USE_CH32V30X)
#define __CH32_HT_ETH_RMII_PHY(_id)                                            \
    .internal_rmii_phy = MR_HT_DRIVER_HAS_PROP(_id, wch_internal_rmii_phy),
#else
#define __CH32_HT_ETH_RMII_PHY(_id)
#endif /* defined(MR_USE_CH32V30X) */
MR_HT_DRIVER_FOREACH(CH32_HT_ETH_INSTALL, &__eth_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ETH) */

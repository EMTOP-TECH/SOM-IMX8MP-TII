// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare XPCS helpers
 *
 * Author: Jose Abreu <Jose.Abreu@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pcs/pcs-xpcs.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/property.h>

#include "pcs-xpcs.h"

#define phylink_pcs_to_xpcs(pl_pcs) \
	container_of((pl_pcs), struct dw_xpcs, pcs)

static const int xpcs_usxgmii_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_10gkr_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_xlgmii_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseDR_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_10gbaser_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseER_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_sgmii_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_10baseT_Half_BIT,
	ETHTOOL_LINK_MODE_10baseT_Full_BIT,
	ETHTOOL_LINK_MODE_100baseT_Half_BIT,
	ETHTOOL_LINK_MODE_100baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_1000basex_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_2500basex_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
	ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const int xpcs_mx95_10g_features[] = {
	ETHTOOL_LINK_MODE_Pause_BIT,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT,
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
	ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT,
	__ETHTOOL_LINK_MODE_MASK_NBITS,
};

static const phy_interface_t xpcs_usxgmii_interfaces[] = {
	PHY_INTERFACE_MODE_USXGMII,
};

static const phy_interface_t xpcs_10gkr_interfaces[] = {
	PHY_INTERFACE_MODE_10GKR,
};

static const phy_interface_t xpcs_xlgmii_interfaces[] = {
	PHY_INTERFACE_MODE_XLGMII,
};

static const phy_interface_t xpcs_10gbaser_interfaces[] = {
	PHY_INTERFACE_MODE_10GBASER,
};

static const phy_interface_t xpcs_sgmii_interfaces[] = {
	PHY_INTERFACE_MODE_SGMII,
};

static const phy_interface_t xpcs_1000basex_interfaces[] = {
	PHY_INTERFACE_MODE_1000BASEX,
};

static const phy_interface_t xpcs_2500basex_interfaces[] = {
	PHY_INTERFACE_MODE_2500BASEX,
};

enum {
	DW_XPCS_USXGMII,
	DW_XPCS_10GKR,
	DW_XPCS_XLGMII,
	DW_XPCS_10GBASER,
	DW_XPCS_SGMII,
	DW_XPCS_1000BASEX,
	DW_XPCS_2500BASEX,
	DW_XPCS_INTERFACE_MAX,
};

struct dw_xpcs_compat {
	const int *supported;
	const phy_interface_t *interface;
	int num_interfaces;
	int an_mode;
	int (*pma_config)(struct dw_xpcs *xpcs);
};

struct dw_xpcs_desc {
	u32 id;
	u32 mask;
	const struct dw_xpcs_compat *compat;
};

static const struct dw_xpcs_compat *
xpcs_find_compat(const struct dw_xpcs_desc *desc, phy_interface_t interface)
{
	int i, j;

	for (i = 0; i < DW_XPCS_INTERFACE_MAX; i++) {
		const struct dw_xpcs_compat *compat = &desc->compat[i];

		for (j = 0; j < compat->num_interfaces; j++)
			if (compat->interface[j] == interface)
				return compat;
	}

	return NULL;
}

int xpcs_get_an_mode(struct dw_xpcs *xpcs, phy_interface_t interface)
{
	const struct dw_xpcs_compat *compat;

	compat = xpcs_find_compat(xpcs->desc, interface);
	if (!compat)
		return -ENODEV;

	return compat->an_mode;
}
EXPORT_SYMBOL_GPL(xpcs_get_an_mode);

static bool __xpcs_linkmode_supported(const struct dw_xpcs_compat *compat,
				      enum ethtool_link_mode_bit_indices linkmode)
{
	int i;

	for (i = 0; compat->supported[i] != __ETHTOOL_LINK_MODE_MASK_NBITS; i++)
		if (compat->supported[i] == linkmode)
			return true;

	return false;
}

#define xpcs_linkmode_supported(compat, mode) \
	__xpcs_linkmode_supported(compat, ETHTOOL_LINK_MODE_ ## mode ## _BIT)

int xpcs_phy_read(struct dw_xpcs *xpcs, int dev, u32 reg)
{
	struct mii_bus *bus = xpcs->phydev->bus;
	int addr = xpcs->phydev->addr;

	return mdiobus_c45_read(bus, addr, dev, reg);
}

int xpcs_phy_write(struct dw_xpcs *xpcs, int dev, u32 reg, u16 val)
{
	struct mii_bus *bus = xpcs->phydev->bus;
	int addr = xpcs->phydev->addr;

	return mdiobus_c45_write(bus, addr, dev, reg, val);
}

int xpcs_read(struct dw_xpcs *xpcs, int dev, u32 reg)
{
	return mdiodev_c45_read(xpcs->mdiodev, dev, reg);
}

int xpcs_write(struct dw_xpcs *xpcs, int dev, u32 reg, u16 val)
{
	return mdiodev_c45_write(xpcs->mdiodev, dev, reg, val);
}

static int xpcs_modify_changed(struct dw_xpcs *xpcs, int dev, u32 reg,
			       u16 mask, u16 set)
{
	return mdiodev_c45_modify_changed(xpcs->mdiodev, dev, reg, mask, set);
}

static int xpcs_read_vendor(struct dw_xpcs *xpcs, int dev, u32 reg)
{
	return xpcs_read(xpcs, dev, DW_VENDOR | reg);
}

static int xpcs_write_vendor(struct dw_xpcs *xpcs, int dev, int reg,
			     u16 val)
{
	return xpcs_write(xpcs, dev, DW_VENDOR | reg, val);
}

int xpcs_read_vpcs(struct dw_xpcs *xpcs, int reg)
{
	return xpcs_read_vendor(xpcs, MDIO_MMD_PCS, reg);
}

int xpcs_write_vpcs(struct dw_xpcs *xpcs, int reg, u16 val)
{
	return xpcs_write_vendor(xpcs, MDIO_MMD_PCS, reg, val);
}

static int xpcs_poll_reset(struct dw_xpcs *xpcs, int dev)
{
	/* Poll until the reset bit clears (50ms per retry == 0.6 sec) */
	unsigned int retries = 12;
	int ret;

	do {
		msleep(50);
		ret = xpcs_read(xpcs, dev, MDIO_CTRL1);
		if (ret < 0)
			return ret;
	} while (ret & MDIO_CTRL1_RESET && --retries);

	return (ret & MDIO_CTRL1_RESET) ? -ETIMEDOUT : 0;
}

static int xpcs_soft_reset(struct dw_xpcs *xpcs,
			   const struct dw_xpcs_compat *compat)
{
	int ret, dev;

	switch (compat->an_mode) {
	case DW_AN_C73:
	case DW_10GBASER:
		dev = MDIO_MMD_PCS;
		break;
	case DW_AN_C37_SGMII:
	case DW_2500BASEX:
	case DW_AN_C37_1000BASEX:
		dev = MDIO_MMD_VEND2;
		break;
	default:
		return -EINVAL;
	}

	ret = xpcs_write(xpcs, dev, MDIO_CTRL1, MDIO_CTRL1_RESET);
	if (ret < 0)
		return ret;

	return xpcs_poll_reset(xpcs, dev);
}

#define xpcs_warn(__xpcs, __state, __args...) \
({ \
	if ((__state)->link) \
		dev_warn(&(__xpcs)->mdiodev->dev, ##__args); \
})

static int xpcs_read_fault_c73(struct dw_xpcs *xpcs,
			       struct phylink_link_state *state,
			       u16 pcs_stat1)
{
	int ret;

	if (pcs_stat1 & MDIO_STAT1_FAULT) {
		xpcs_warn(xpcs, state, "Link fault condition detected!\n");
		return -EFAULT;
	}

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_STAT2);
	if (ret < 0)
		return ret;

	if (ret & MDIO_STAT2_RXFAULT)
		xpcs_warn(xpcs, state, "Receiver fault detected!\n");
	if (ret & MDIO_STAT2_TXFAULT)
		xpcs_warn(xpcs, state, "Transmitter fault detected!\n");

	ret = xpcs_read_vendor(xpcs, MDIO_MMD_PCS, DW_VR_XS_PCS_DIG_STS);
	if (ret < 0)
		return ret;

	if (ret & DW_RXFIFO_ERR) {
		xpcs_warn(xpcs, state, "FIFO fault condition detected!\n");
		return -EFAULT;
	}

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_PCS_10GBRT_STAT1);
	if (ret < 0)
		return ret;

	if (!(ret & MDIO_PCS_10GBRT_STAT1_BLKLK))
		xpcs_warn(xpcs, state, "Link is not locked!\n");

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_PCS_10GBRT_STAT2);
	if (ret < 0)
		return ret;

	if (ret & MDIO_PCS_10GBRT_STAT2_ERR) {
		xpcs_warn(xpcs, state, "Link has errors!\n");
		return -EFAULT;
	}

	return 0;
}

static void xpcs_config_usxgmii(struct dw_xpcs *xpcs, int speed)
{
	int ret, speed_sel;

	switch (speed) {
	case SPEED_10:
		speed_sel = DW_USXGMII_10;
		break;
	case SPEED_100:
		speed_sel = DW_USXGMII_100;
		break;
	case SPEED_1000:
		speed_sel = DW_USXGMII_1000;
		break;
	case SPEED_2500:
		speed_sel = DW_USXGMII_2500;
		break;
	case SPEED_5000:
		speed_sel = DW_USXGMII_5000;
		break;
	case SPEED_10000:
		speed_sel = DW_USXGMII_10000;
		break;
	default:
		/* Nothing to do here */
		return;
	}

	ret = xpcs_read_vpcs(xpcs, MDIO_CTRL1);
	if (ret < 0)
		goto out;

	ret = xpcs_write_vpcs(xpcs, MDIO_CTRL1, ret | DW_USXGMII_EN);
	if (ret < 0)
		goto out;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1);
	if (ret < 0)
		goto out;

	ret &= ~DW_USXGMII_SS_MASK;
	ret |= speed_sel | DW_USXGMII_FULL;

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1, ret);
	if (ret < 0)
		goto out;

	ret = xpcs_read_vpcs(xpcs, MDIO_CTRL1);
	if (ret < 0)
		goto out;

	ret = xpcs_write_vpcs(xpcs, MDIO_CTRL1, ret | DW_USXGMII_RST);
	if (ret < 0)
		goto out;

	return;

out:
	pr_err("%s: XPCS access returned %pe\n", __func__, ERR_PTR(ret));
}

static int _xpcs_config_aneg_c73(struct dw_xpcs *xpcs,
				 const struct dw_xpcs_compat *compat)
{
	int ret, adv;

	/* By default, in USXGMII mode XPCS operates at 10G baud and
	 * replicates data to achieve lower speeds. Hereby, in this
	 * default configuration we need to advertise all supported
	 * modes and not only the ones we want to use.
	 */

	/* SR_AN_ADV3 */
	adv = 0;
	if (xpcs_linkmode_supported(compat, 2500baseX_Full))
		adv |= DW_C73_2500KX;

	/* TODO: 5000baseKR */

	ret = xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV3, adv);
	if (ret < 0)
		return ret;

	/* SR_AN_ADV2 */
	adv = 0;
	if (xpcs_linkmode_supported(compat, 1000baseKX_Full))
		adv |= DW_C73_1000KX;
	if (xpcs_linkmode_supported(compat, 10000baseKX4_Full))
		adv |= DW_C73_10000KX4;
	if (xpcs_linkmode_supported(compat, 10000baseKR_Full))
		adv |= DW_C73_10000KR;

	ret = xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV2, adv);
	if (ret < 0)
		return ret;

	/* SR_AN_ADV1 */
	adv = DW_C73_AN_ADV_SF;
	if (xpcs_linkmode_supported(compat, Pause))
		adv |= DW_C73_PAUSE;
	if (xpcs_linkmode_supported(compat, Asym_Pause))
		adv |= DW_C73_ASYM_PAUSE;

	return xpcs_write(xpcs, MDIO_MMD_AN, DW_SR_AN_ADV1, adv);
}

static int xpcs_config_aneg_c73(struct dw_xpcs *xpcs,
				const struct dw_xpcs_compat *compat)
{
	int ret;

	ret = _xpcs_config_aneg_c73(xpcs, compat);
	if (ret < 0)
		return ret;

	ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_CTRL1);
	if (ret < 0)
		return ret;

	ret |= MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART;

	return xpcs_write(xpcs, MDIO_MMD_AN, MDIO_CTRL1, ret);
}

static int xpcs_aneg_done_c73(struct dw_xpcs *xpcs,
			      struct phylink_link_state *state,
			      const struct dw_xpcs_compat *compat, u16 an_stat1)
{
	int ret;

	if (an_stat1 & MDIO_AN_STAT1_COMPLETE) {
		ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_AN_LPA);
		if (ret < 0)
			return ret;

		/* Check if Aneg outcome is valid */
		if (!(ret & DW_C73_AN_ADV_SF)) {
			xpcs_config_aneg_c73(xpcs, compat);
			return 0;
		}

		return 1;
	}

	return 0;
}

static int xpcs_read_lpa_c73(struct dw_xpcs *xpcs,
			     struct phylink_link_state *state, u16 an_stat1)
{
	u16 lpa[3];
	int i, ret;

	if (!(an_stat1 & MDIO_AN_STAT1_LPABLE)) {
		phylink_clear(state->lp_advertising, Autoneg);
		return 0;
	}

	phylink_set(state->lp_advertising, Autoneg);

	/* Read Clause 73 link partner advertisement */
	for (i = ARRAY_SIZE(lpa); --i >= 0; ) {
		ret = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_AN_LPA + i);
		if (ret < 0)
			return ret;

		lpa[i] = ret;
	}

	mii_c73_mod_linkmode(state->lp_advertising, lpa);

	return 0;
}

static int xpcs_get_max_xlgmii_speed(struct dw_xpcs *xpcs,
				     struct phylink_link_state *state)
{
	unsigned long *adv = state->advertising;
	int speed = SPEED_UNKNOWN;
	int bit;

	for_each_set_bit(bit, adv, __ETHTOOL_LINK_MODE_MASK_NBITS) {
		int new_speed = SPEED_UNKNOWN;

		switch (bit) {
		case ETHTOOL_LINK_MODE_25000baseCR_Full_BIT:
		case ETHTOOL_LINK_MODE_25000baseKR_Full_BIT:
		case ETHTOOL_LINK_MODE_25000baseSR_Full_BIT:
			new_speed = SPEED_25000;
			break;
		case ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT:
		case ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT:
		case ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT:
		case ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT:
			new_speed = SPEED_40000;
			break;
		case ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseKR_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseSR_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseCR_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT:
		case ETHTOOL_LINK_MODE_50000baseDR_Full_BIT:
			new_speed = SPEED_50000;
			break;
		case ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT:
		case ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT:
			new_speed = SPEED_100000;
			break;
		default:
			continue;
		}

		if (new_speed > speed)
			speed = new_speed;
	}

	return speed;
}

static void xpcs_resolve_pma(struct dw_xpcs *xpcs,
			     struct phylink_link_state *state)
{
	state->pause = MLO_PAUSE_TX | MLO_PAUSE_RX;
	state->duplex = DUPLEX_FULL;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_10GKR:
		state->speed = SPEED_10000;
		break;
	case PHY_INTERFACE_MODE_XLGMII:
		state->speed = xpcs_get_max_xlgmii_speed(xpcs, state);
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
	}
}

static int xpcs_validate(struct phylink_pcs *pcs, unsigned long *supported,
			 const struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(xpcs_supported) = { 0, };
	const struct dw_xpcs_compat *compat;
	struct dw_xpcs *xpcs;
	int i;

	xpcs = phylink_pcs_to_xpcs(pcs);
	compat = xpcs_find_compat(xpcs->desc, state->interface);
	if (!compat)
		return -EINVAL;

	/* Populate the supported link modes for this PHY interface type.
	 * FIXME: what about the port modes and autoneg bit? This masks
	 * all those away.
	 */
	for (i = 0; compat->supported[i] != __ETHTOOL_LINK_MODE_MASK_NBITS; i++)
		set_bit(compat->supported[i], xpcs_supported);

	linkmode_and(supported, supported, xpcs_supported);

	return 0;
}

static void xpcs_disable(struct phylink_pcs *pcs)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	if (xpcs && xpcs->info.pma == NXP_MX95_XPCS_ID)
		xpcs_phy_reset(xpcs);
}

void xpcs_get_interfaces(struct dw_xpcs *xpcs, unsigned long *interfaces)
{
	int i, j;

	for (i = 0; i < DW_XPCS_INTERFACE_MAX; i++) {
		const struct dw_xpcs_compat *compat = &xpcs->desc->compat[i];

		for (j = 0; j < compat->num_interfaces; j++)
			__set_bit(compat->interface[j], interfaces);
	}
}
EXPORT_SYMBOL_GPL(xpcs_get_interfaces);

int xpcs_config_eee(struct dw_xpcs *xpcs, int mult_fact_100ns, int enable)
{
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_EEE_MCTRL0);
	if (ret < 0)
		return ret;

	if (enable) {
	/* Enable EEE */
		ret = DW_VR_MII_EEE_LTX_EN | DW_VR_MII_EEE_LRX_EN |
		      DW_VR_MII_EEE_TX_QUIET_EN | DW_VR_MII_EEE_RX_QUIET_EN |
		      DW_VR_MII_EEE_TX_EN_CTRL | DW_VR_MII_EEE_RX_EN_CTRL |
		      mult_fact_100ns << DW_VR_MII_EEE_MULT_FACT_100NS_SHIFT;
	} else {
		ret &= ~(DW_VR_MII_EEE_LTX_EN | DW_VR_MII_EEE_LRX_EN |
		       DW_VR_MII_EEE_TX_QUIET_EN | DW_VR_MII_EEE_RX_QUIET_EN |
		       DW_VR_MII_EEE_TX_EN_CTRL | DW_VR_MII_EEE_RX_EN_CTRL |
		       DW_VR_MII_EEE_MULT_FACT_100NS);
	}

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_EEE_MCTRL0, ret);
	if (ret < 0)
		return ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_EEE_MCTRL1);
	if (ret < 0)
		return ret;

	if (enable)
		ret |= DW_VR_MII_EEE_TRN_LPI;
	else
		ret &= ~DW_VR_MII_EEE_TRN_LPI;

	return xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_EEE_MCTRL1, ret);
}
EXPORT_SYMBOL_GPL(xpcs_config_eee);

static int xpcs_config_aneg_c37_sgmii(struct dw_xpcs *xpcs,
				      unsigned int neg_mode)
{
	int ret, mdio_ctrl, tx_conf;

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID)
		xpcs_write_vpcs(xpcs, DW_VR_XS_PCS_DIG_CTRL1, DW_CL37_BP | DW_EN_VSMMD1);

	/* For AN for C37 SGMII mode, the settings are :-
	 * 1) VR_MII_MMD_CTRL Bit(12) [AN_ENABLE] = 0b (Disable SGMII AN in case
	      it is already enabled)
	 * 2) VR_MII_AN_CTRL Bit(2:1)[PCS_MODE] = 10b (SGMII AN)
	 * 3) VR_MII_AN_CTRL Bit(3) [TX_CONFIG] = 0b (MAC side SGMII)
	 *    DW xPCS used with DW EQoS MAC is always MAC side SGMII.
	 * 4) VR_MII_DIG_CTRL1 Bit(9) [MAC_AUTO_SW] = 1b (Automatic
	 *    speed/duplex mode change by HW after SGMII AN complete)
	 * 5) VR_MII_MMD_CTRL Bit(12) [AN_ENABLE] = 1b (Enable SGMII AN)
	 *
	 * Note: Since it is MAC side SGMII, there is no need to set
	 *	 SR_MII_AN_ADV. MAC side SGMII receives AN Tx Config from
	 *	 PHY about the link state change after C28 AN is completed
	 *	 between PHY and Link Partner. There is also no need to
	 *	 trigger AN restart for MAC-side SGMII.
	 */
	mdio_ctrl = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_CTRL);
	if (mdio_ctrl < 0)
		return mdio_ctrl;

	if (mdio_ctrl & AN_CL37_EN) {
		ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_CTRL,
				 mdio_ctrl & ~AN_CL37_EN);
		if (ret < 0)
			return ret;
	}

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_CTRL);
	if (ret < 0)
		return ret;

	ret &= ~(DW_VR_MII_PCS_MODE_MASK | DW_VR_MII_TX_CONFIG_MASK);
	ret |= (DW_VR_MII_PCS_MODE_C37_SGMII <<
		DW_VR_MII_AN_CTRL_PCS_MODE_SHIFT &
		DW_VR_MII_PCS_MODE_MASK);
	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID) {
		ret |= DW_VR_MII_AN_CTRL_8BIT;
		/* Hardware requires it to be PHY side SGMII */
		tx_conf = DW_VR_MII_TX_CONFIG_PHY_SIDE_SGMII;
	} else {
		tx_conf = DW_VR_MII_TX_CONFIG_MAC_SIDE_SGMII;
	}
	ret |= tx_conf << DW_VR_MII_AN_CTRL_TX_CONFIG_SHIFT &
		DW_VR_MII_TX_CONFIG_MASK;
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_CTRL, ret);
	if (ret < 0)
		return ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_DIG_CTRL1);
	if (ret < 0)
		return ret;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		ret |= DW_VR_MII_DIG_CTRL1_MAC_AUTO_SW;
	else
		ret &= ~DW_VR_MII_DIG_CTRL1_MAC_AUTO_SW;

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID)
		ret |= DW_VR_MII_DIG_CTRL1_PHY_MODE_CTRL;

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_DIG_CTRL1, ret);
	if (ret < 0)
		return ret;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_CTRL,
				 mdio_ctrl | AN_CL37_EN);

	return ret;
}

static int xpcs_config_aneg_c37_1000basex(struct dw_xpcs *xpcs,
					  unsigned int neg_mode,
					  const unsigned long *advertising)
{
	phy_interface_t interface = PHY_INTERFACE_MODE_1000BASEX;
	int ret, mdio_ctrl, adv;
	bool changed = 0;

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID)
		xpcs_write_vpcs(xpcs, DW_VR_XS_PCS_DIG_CTRL1, DW_CL37_BP | DW_EN_VSMMD1);

	/* According to Chap 7.12, to set 1000BASE-X C37 AN, AN must
	 * be disabled first:-
	 * 1) VR_MII_MMD_CTRL Bit(12)[AN_ENABLE] = 0b
	 * 2) VR_MII_AN_CTRL Bit(2:1)[PCS_MODE] = 00b (1000BASE-X C37)
	 */
	mdio_ctrl = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_CTRL);
	if (mdio_ctrl < 0)
		return mdio_ctrl;

	if (mdio_ctrl & AN_CL37_EN) {
		ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_CTRL,
				 mdio_ctrl & ~AN_CL37_EN);
		if (ret < 0)
			return ret;
	}

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_CTRL);
	if (ret < 0)
		return ret;

	ret &= ~DW_VR_MII_PCS_MODE_MASK;
	if (!xpcs->pcs.poll)
		ret |= DW_VR_MII_AN_INTR_EN;
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_CTRL, ret);
	if (ret < 0)
		return ret;

	/* Check for advertising changes and update the C45 MII ADV
	 * register accordingly.
	 */
	adv = phylink_mii_c22_pcs_encode_advertisement(interface,
						       advertising);
	if (adv >= 0) {
		ret = xpcs_modify_changed(xpcs, MDIO_MMD_VEND2,
					  MII_ADVERTISE, 0xffff, adv);
		if (ret < 0)
			return ret;

		changed = ret;
	}

	/* Clear CL37 AN complete status */
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS, 0);
	if (ret < 0)
		return ret;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
		ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_CTRL,
				 mdio_ctrl | AN_CL37_EN);
		if (ret < 0)
			return ret;
	}

	return changed;
}

static int xpcs_config_2500basex(struct dw_xpcs *xpcs)
{
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_DIG_CTRL1);
	if (ret < 0)
		return ret;
	ret |= DW_VR_MII_DIG_CTRL1_2G5_EN;
	ret &= ~DW_VR_MII_DIG_CTRL1_MAC_AUTO_SW;
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_DIG_CTRL1, ret);
	if (ret < 0)
		return ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_CTRL);
	if (ret < 0)
		return ret;
	ret &= ~AN_CL37_EN;
	ret |= SGMII_SPEED_SS6;
	ret &= ~SGMII_SPEED_SS13;
	return xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_CTRL, ret);
}

int xpcs_do_config(struct dw_xpcs *xpcs, phy_interface_t interface,
		   const unsigned long *advertising, unsigned int neg_mode)
{
	const struct dw_xpcs_compat *compat;
	int ret;

	compat = xpcs_find_compat(xpcs->desc, interface);
	if (!compat)
		return -ENODEV;

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID) {
		ret = txgbe_xpcs_switch_mode(xpcs, interface);
		if (ret)
			return ret;
	}

	switch (compat->an_mode) {
	case DW_10GBASER:
		break;
	case DW_AN_C73:
		if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
			ret = xpcs_config_aneg_c73(xpcs, compat);
			if (ret)
				return ret;
		}
		break;
	case DW_AN_C37_SGMII:
		ret = xpcs_config_aneg_c37_sgmii(xpcs, neg_mode);
		if (ret)
			return ret;
		break;
	case DW_AN_C37_1000BASEX:
		ret = xpcs_config_aneg_c37_1000basex(xpcs, neg_mode,
						     advertising);
		if (ret)
			return ret;
		break;
	case DW_2500BASEX:
		ret = xpcs_config_2500basex(xpcs);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	if (compat->pma_config) {
		ret = compat->pma_config(xpcs);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xpcs_do_config);

static int xpcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
		       phy_interface_t interface,
		       const unsigned long *advertising,
		       bool permit_pause_to_mac)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	return xpcs_do_config(xpcs, interface, advertising, neg_mode);
}

static int xpcs_get_state_c73(struct dw_xpcs *xpcs,
			      struct phylink_link_state *state,
			      const struct dw_xpcs_compat *compat)
{
	bool an_enabled;
	int pcs_stat1;
	int an_stat1;
	int ret;

	/* The link status bit is latching-low, so it is important to
	 * avoid unnecessary re-reads of this register to avoid missing
	 * a link-down event.
	 */
	pcs_stat1 = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_STAT1);
	if (pcs_stat1 < 0) {
		state->link = false;
		return pcs_stat1;
	}

	/* Link needs to be read first ... */
	state->link = !!(pcs_stat1 & MDIO_STAT1_LSTATUS);

	/* ... and then we check the faults. */
	ret = xpcs_read_fault_c73(xpcs, state, pcs_stat1);
	if (ret) {
		ret = xpcs_soft_reset(xpcs, compat);
		if (ret)
			return ret;

		state->link = 0;

		return xpcs_do_config(xpcs, state->interface, NULL,
				      PHYLINK_PCS_NEG_INBAND_ENABLED);
	}

	/* There is no point doing anything else if the link is down. */
	if (!state->link)
		return 0;

	an_enabled = linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				       state->advertising);
	if (an_enabled) {
		/* The link status bit is latching-low, so it is important to
		 * avoid unnecessary re-reads of this register to avoid missing
		 * a link-down event.
		 */
		an_stat1 = xpcs_read(xpcs, MDIO_MMD_AN, MDIO_STAT1);
		if (an_stat1 < 0) {
			state->link = false;
			return an_stat1;
		}

		state->an_complete = xpcs_aneg_done_c73(xpcs, state, compat,
							an_stat1);
		if (!state->an_complete) {
			state->link = false;
			return 0;
		}

		ret = xpcs_read_lpa_c73(xpcs, state, an_stat1);
		if (ret < 0) {
			state->link = false;
			return ret;
		}

		phylink_resolve_c73(state);
	} else {
		xpcs_resolve_pma(xpcs, state);
	}

	return 0;
}

static int xpcs_get_state_c37_sgmii(struct dw_xpcs *xpcs,
				    struct phylink_link_state *state)
{
	int ret;

	/* Reset link_state */
	state->link = false;
	state->speed = SPEED_UNKNOWN;
	state->duplex = DUPLEX_UNKNOWN;
	state->pause = 0;

	/* For C37 SGMII mode, we check DW_VR_MII_AN_INTR_STS for link
	 * status, speed and duplex.
	 */
	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS);
	if (ret < 0)
		return ret;

	if (ret & DW_VR_MII_C37_ANSGM_SP_LNKSTS) {
		int speed_value;

		state->link = true;

		speed_value = (ret & DW_VR_MII_AN_STS_C37_ANSGM_SP) >>
			      DW_VR_MII_AN_STS_C37_ANSGM_SP_SHIFT;
		if (speed_value == DW_VR_MII_C37_ANSGM_SP_1000)
			state->speed = SPEED_1000;
		else if (speed_value == DW_VR_MII_C37_ANSGM_SP_100)
			state->speed = SPEED_100;
		else
			state->speed = SPEED_10;

		if (ret & DW_VR_MII_AN_STS_C37_ANSGM_FD)
			state->duplex = DUPLEX_FULL;
		else
			state->duplex = DUPLEX_HALF;
	} else if (ret == DW_VR_MII_AN_STS_C37_ANCMPLT_INTR) {
		int speed, duplex;

		state->link = true;

		speed = xpcs_read(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1);
		if (speed < 0)
			return speed;

		speed &= SGMII_SPEED_SS13 | SGMII_SPEED_SS6;
		if (speed == SGMII_SPEED_SS6)
			state->speed = SPEED_1000;
		else if (speed == SGMII_SPEED_SS13)
			state->speed = SPEED_100;
		else if (speed == 0)
			state->speed = SPEED_10;

		duplex = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_ADVERTISE);
		if (duplex < 0)
			return duplex;

		if (duplex & DW_FULL_DUPLEX)
			state->duplex = DUPLEX_FULL;
		else if (duplex & DW_HALF_DUPLEX)
			state->duplex = DUPLEX_HALF;

		xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS, 0);
	}

	return 0;
}

static int xpcs_get_state_c37_1000basex(struct dw_xpcs *xpcs,
					struct phylink_link_state *state)
{
	int lpa, bmsr;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			      state->advertising)) {
		/* Reset link state */
		state->link = false;

		lpa = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_LPA);
		if (lpa < 0 || lpa & LPA_RFAULT)
			return lpa;

		bmsr = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_BMSR);
		if (bmsr < 0)
			return bmsr;

		/* Clear AN complete interrupt */
		if (!xpcs->pcs.poll) {
			int an_intr;

			an_intr = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS);
			if (an_intr & DW_VR_MII_AN_STS_C37_ANCMPLT_INTR) {
				an_intr &= ~DW_VR_MII_AN_STS_C37_ANCMPLT_INTR;
				xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS, an_intr);
			}
		}

		phylink_mii_c22_pcs_decode_state(state, bmsr, lpa);
	}

	return 0;
}

static int xpcs_get_state_2500basex(struct dw_xpcs *xpcs,
				    struct phylink_link_state *state)
{
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, DW_VR_MII_MMD_STS);
	if (ret < 0) {
		state->link = 0;
		return ret;
	}

	state->link = !!(ret & DW_VR_MII_MMD_STS_LINK_STS);
	if (!state->link)
		return 0;

	state->speed = SPEED_2500;
	state->pause |= MLO_PAUSE_TX | MLO_PAUSE_RX;
	state->duplex = DUPLEX_FULL;

	return 0;
}

static void xpcs_get_state(struct phylink_pcs *pcs,
			   struct phylink_link_state *state)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);
	const struct dw_xpcs_compat *compat;
	int ret;
	int stat1;

	compat = xpcs_find_compat(xpcs->desc, state->interface);
	if (!compat)
		return;

	switch (compat->an_mode) {
	case DW_10GBASER:
		stat1 = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_STAT1);
		if (stat1 < 0) {
			state->link = false;
			break;
		}

		if (stat1 & MDIO_STAT1_FAULT)
			xpcs_do_config(xpcs, state->interface, NULL,
				       PHYLINK_PCS_NEG_NONE);

		phylink_mii_c45_pcs_get_state(xpcs->mdiodev, state);
		break;
	case DW_AN_C73:
		ret = xpcs_get_state_c73(xpcs, state, compat);
		if (ret) {
			pr_err("xpcs_get_state_c73 returned %pe\n",
			       ERR_PTR(ret));
			return;
		}
		break;
	case DW_AN_C37_SGMII:
		ret = xpcs_get_state_c37_sgmii(xpcs, state);
		if (ret) {
			pr_err("xpcs_get_state_c37_sgmii returned %pe\n",
			       ERR_PTR(ret));
		}
		break;
	case DW_AN_C37_1000BASEX:
		ret = xpcs_get_state_c37_1000basex(xpcs, state);
		if (ret) {
			pr_err("xpcs_get_state_c37_1000basex returned %pe\n",
			       ERR_PTR(ret));
		}
		break;
	case DW_2500BASEX:
		ret = xpcs_get_state_2500basex(xpcs, state);
		if (ret) {
			pr_err("xpcs_get_state_2500basex returned %pe\n",
			       ERR_PTR(ret));
		}
		break;
	default:
		return;
	}
}

static void xpcs_link_up_sgmii(struct dw_xpcs *xpcs, unsigned int neg_mode,
			       int speed, int duplex)
{
	int val, ret;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		return;

	val = mii_bmcr_encode_fixed(speed, duplex);
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1, val);
	if (ret)
		pr_err("%s: xpcs_write returned %pe\n", __func__, ERR_PTR(ret));
}

static void xpcs_link_up_1000basex(struct dw_xpcs *xpcs, unsigned int neg_mode,
				   int speed, int duplex)
{
	int val, ret;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		return;

	switch (speed) {
	case SPEED_1000:
		val = BMCR_SPEED1000;
		break;
	case SPEED_100:
	case SPEED_10:
	default:
		pr_err("%s: speed = %d\n", __func__, speed);
		return;
	}

	if (duplex == DUPLEX_FULL)
		val |= BMCR_FULLDPLX;
	else
		pr_err("%s: half duplex not supported\n", __func__);

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1, val);
	if (ret)
		pr_err("%s: xpcs_write returned %pe\n", __func__, ERR_PTR(ret));
}

void xpcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
		  phy_interface_t interface, int speed, int duplex)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	if (interface == PHY_INTERFACE_MODE_USXGMII)
		return xpcs_config_usxgmii(xpcs, speed);
	if (interface == PHY_INTERFACE_MODE_SGMII)
		return xpcs_link_up_sgmii(xpcs, neg_mode, speed, duplex);
	if (interface == PHY_INTERFACE_MODE_1000BASEX)
		return xpcs_link_up_1000basex(xpcs, neg_mode, speed, duplex);
}
EXPORT_SYMBOL_GPL(xpcs_link_up);

static void xpcs_an_restart(struct phylink_pcs *pcs)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);
	int ret;

	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1);
	if (ret >= 0) {
		ret |= BMCR_ANRESTART;
		xpcs_write(xpcs, MDIO_MMD_VEND2, MDIO_CTRL1, ret);
	}
}

static int xpcs_get_id(struct dw_xpcs *xpcs)
{
	int ret;
	u32 id;

	/* First, search C73 PCS using PCS MMD 3. Return ENODEV if communication
	 * failed indicating that device couldn't be reached.
	 */
	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MII_PHYSID1);
	if (ret < 0)
		return -ENODEV;

	id = ret << 16;

	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MII_PHYSID2);
	if (ret < 0)
		return ret;

	id |= ret;

	/* If Device IDs are not all zeros or ones, then 10GBase-X/R or C73
	 * KR/KX4 PCS found. Otherwise fallback to detecting 1000Base-X or C37
	 * PCS in MII MMD 31.
	 */
	if (!id || id == 0xffffffff) {
		ret = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_PHYSID1);
		if (ret < 0)
			return ret;

		id = ret << 16;

		ret = xpcs_read(xpcs, MDIO_MMD_VEND2, MII_PHYSID2);
		if (ret < 0)
			return ret;

		id |= ret;
	}

	/* Set the PCS ID if it hasn't been pre-initialized */
	if (xpcs->info.pcs == DW_XPCS_ID_NATIVE)
		xpcs->info.pcs = id;

	/* Find out PMA/PMD ID from MMD 1 device ID registers */
	ret = xpcs_read(xpcs, MDIO_MMD_PMAPMD, MDIO_DEVID1);
	if (ret < 0)
		return ret;

	id = ret;

	ret = xpcs_read(xpcs, MDIO_MMD_PMAPMD, MDIO_DEVID2);
	if (ret < 0)
		return ret;

	/* Note the inverted dword order and masked out Model/Revision numbers
	 * with respect to what is done with the PCS ID...
	 */
	ret = (ret >> 10) & 0x3F;
	id |= ret << 16;

	/* Set the PMA ID if it hasn't been pre-initialized */
	if (xpcs->info.pma == DW_XPCS_PMA_ID_NATIVE)
		xpcs->info.pma = id;

	return 0;
}

static const struct dw_xpcs_compat synopsys_xpcs_compat[DW_XPCS_INTERFACE_MAX] = {
	[DW_XPCS_USXGMII] = {
		.supported = xpcs_usxgmii_features,
		.interface = xpcs_usxgmii_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_usxgmii_interfaces),
		.an_mode = DW_AN_C73,
	},
	[DW_XPCS_10GKR] = {
		.supported = xpcs_10gkr_features,
		.interface = xpcs_10gkr_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_10gkr_interfaces),
		.an_mode = DW_AN_C73,
	},
	[DW_XPCS_XLGMII] = {
		.supported = xpcs_xlgmii_features,
		.interface = xpcs_xlgmii_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_xlgmii_interfaces),
		.an_mode = DW_AN_C73,
	},
	[DW_XPCS_10GBASER] = {
		.supported = xpcs_10gbaser_features,
		.interface = xpcs_10gbaser_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_10gbaser_interfaces),
		.an_mode = DW_10GBASER,
	},
	[DW_XPCS_SGMII] = {
		.supported = xpcs_sgmii_features,
		.interface = xpcs_sgmii_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_sgmii_interfaces),
		.an_mode = DW_AN_C37_SGMII,
	},
	[DW_XPCS_1000BASEX] = {
		.supported = xpcs_1000basex_features,
		.interface = xpcs_1000basex_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_1000basex_interfaces),
		.an_mode = DW_AN_C37_1000BASEX,
	},
	[DW_XPCS_2500BASEX] = {
		.supported = xpcs_2500basex_features,
		.interface = xpcs_2500basex_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_2500basex_interfaces),
		.an_mode = DW_2500BASEX,
	},
};

static const struct dw_xpcs_compat nxp_sja1105_xpcs_compat[DW_XPCS_INTERFACE_MAX] = {
	[DW_XPCS_SGMII] = {
		.supported = xpcs_sgmii_features,
		.interface = xpcs_sgmii_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_sgmii_interfaces),
		.an_mode = DW_AN_C37_SGMII,
		.pma_config = nxp_sja1105_sgmii_pma_config,
	},
};

static const struct dw_xpcs_compat nxp_sja1110_xpcs_compat[DW_XPCS_INTERFACE_MAX] = {
	[DW_XPCS_SGMII] = {
		.supported = xpcs_sgmii_features,
		.interface = xpcs_sgmii_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_sgmii_interfaces),
		.an_mode = DW_AN_C37_SGMII,
		.pma_config = nxp_sja1110_sgmii_pma_config,
	},
	[DW_XPCS_2500BASEX] = {
		.supported = xpcs_2500basex_features,
		.interface = xpcs_2500basex_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_2500basex_interfaces),
		.an_mode = DW_2500BASEX,
		.pma_config = nxp_sja1110_2500basex_pma_config,
	},
};

static const struct dw_xpcs_compat nxp_mx95_xpcs_compat[DW_XPCS_INTERFACE_MAX] = {
	[DW_XPCS_10GBASER] = {
		.supported = xpcs_mx95_10g_features,
		.interface = xpcs_10gbaser_interfaces,
		.num_interfaces = ARRAY_SIZE(xpcs_10gbaser_interfaces),
		.an_mode = DW_10GBASER,
		.pma_config = xpcs_phy_usxgmii_pma_config,//
	},
};

static const struct dw_xpcs_desc xpcs_desc_list[] = {
	{
		.id = DW_XPCS_ID,
		.mask = DW_XPCS_ID_MASK,
		.compat = synopsys_xpcs_compat,
	}, {
		.id = NXP_SJA1105_XPCS_ID,
		.mask = DW_XPCS_ID_MASK,
		.compat = nxp_sja1105_xpcs_compat,
	}, {
		.id = NXP_SJA1110_XPCS_ID,
		.mask = DW_XPCS_ID_MASK,
		.compat = nxp_sja1110_xpcs_compat,
	}, {
		.id = NXP_MX95_XPCS_ID,
		.mask = DW_XPCS_ID_MASK,
		.compat = nxp_mx95_xpcs_compat,
	},
};

static const struct phylink_pcs_ops xpcs_phylink_ops = {
	.pcs_validate = xpcs_validate,
	.pcs_disable = xpcs_disable,
	.pcs_config = xpcs_config,
	.pcs_get_state = xpcs_get_state,
	.pcs_an_restart = xpcs_an_restart,
	.pcs_link_up = xpcs_link_up,
};

static u32 xpcs_register_phy(struct dw_xpcs *xpcs, struct mii_bus *bus)
{
	u32 xpcs_phy_id;
	int ret = 0;

	xpcs_phy_reg_lock(xpcs);

	xpcs_phy_id = xpcs_phy_get_id(xpcs);
	ret = xpcs_phy_check_id(xpcs_phy_id);
	if (!ret)
		return -ENODEV;

	xpcs->info.pcs = xpcs_phy_id;
	xpcs->info.pma = xpcs_phy_id;

	return 0;
}

static struct dw_xpcs *xpcs_create_data(struct mdio_device *mdiodev,
					struct mdio_device *phydev)
{
	struct dw_xpcs *xpcs;

	xpcs = kzalloc(sizeof(*xpcs), GFP_KERNEL);
	if (!xpcs)
		return ERR_PTR(-ENOMEM);

	mdio_device_get(mdiodev);
	xpcs->mdiodev = mdiodev;
	if (phydev) {
		mdio_device_get(phydev);
		xpcs->phydev = phydev;
	}
	xpcs->pcs.ops = &xpcs_phylink_ops;
	xpcs->pcs.neg_mode = true;
	xpcs->pcs.poll = true;

	return xpcs;
}

static void xpcs_free_data(struct dw_xpcs *xpcs)
{
	mdio_device_put(xpcs->mdiodev);
	if (xpcs->phydev)
		mdio_device_put(xpcs->phydev);
	kfree(xpcs);
}

static int xpcs_init_clks(struct dw_xpcs *xpcs)
{
	static const char *ids[DW_XPCS_NUM_CLKS] = {
		[DW_XPCS_CORE_CLK] = "core",
		[DW_XPCS_PAD_CLK] = "pad",
	};
	struct device *dev = &xpcs->mdiodev->dev;
	int ret, i;

	for (i = 0; i < DW_XPCS_NUM_CLKS; ++i)
		xpcs->clks[i].id = ids[i];

	ret = clk_bulk_get_optional(dev, DW_XPCS_NUM_CLKS, xpcs->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");

	ret = clk_bulk_prepare_enable(DW_XPCS_NUM_CLKS, xpcs->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable clocks\n");

	return 0;
}

static void xpcs_clear_clks(struct dw_xpcs *xpcs)
{
	clk_bulk_disable_unprepare(DW_XPCS_NUM_CLKS, xpcs->clks);

	clk_bulk_put(DW_XPCS_NUM_CLKS, xpcs->clks);
}

static int xpcs_init_id(struct dw_xpcs *xpcs)
{
	const struct dw_xpcs_info *info;
	int i, ret;

	info = dev_get_platdata(&xpcs->mdiodev->dev);
	if (!info) {
		xpcs->info.pcs = DW_XPCS_ID_NATIVE;
		xpcs->info.pma = DW_XPCS_PMA_ID_NATIVE;
	} else {
		xpcs->info = *info;
	}

	if (xpcs->phydev)
		ret = xpcs_register_phy(xpcs, xpcs->mdiodev->bus);
	else
		ret = xpcs_get_id(xpcs);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(xpcs_desc_list); i++) {
		const struct dw_xpcs_desc *desc = &xpcs_desc_list[i];

		if ((xpcs->info.pcs & desc->mask) != desc->id)
			continue;

		xpcs->desc = desc;

		break;
	}

	if (!xpcs->desc)
		return -ENODEV;

	return 0;
}

static int xpcs_init_iface(struct dw_xpcs *xpcs, phy_interface_t interface)
{
	const struct dw_xpcs_compat *compat;

	compat = xpcs_find_compat(xpcs->desc, interface);
	if (!compat)
		return -EINVAL;

	if (xpcs->info.pma == WX_TXGBE_XPCS_PMA_10G_ID) {
		xpcs->pcs.poll = false;
		return 0;
	}

	return xpcs_soft_reset(xpcs, compat);
}

static struct dw_xpcs *xpcs_create(struct mdio_device *mdiodev,
				   struct mdio_device *phydev,
				   phy_interface_t interface)
{
	struct dw_xpcs *xpcs;
	int ret;

	xpcs = xpcs_create_data(mdiodev, phydev);
	if (IS_ERR(xpcs))
		return xpcs;

	ret = xpcs_init_clks(xpcs);
	if (ret)
		goto out_free_data;

	ret = xpcs_init_id(xpcs);
	if (ret)
		goto out_clear_clks;

	ret = xpcs_init_iface(xpcs, interface);
	if (ret)
		goto out_clear_clks;

	return xpcs;

out_clear_clks:
	xpcs_clear_clks(xpcs);

out_free_data:
	xpcs_free_data(xpcs);

	return ERR_PTR(ret);
}

/**
 * xpcs_create_mdiodev() - create a DW xPCS instance with the MDIO @addr
 * @bus: pointer to the MDIO-bus descriptor for the device to be looked at
 * @addr: device MDIO-bus ID
 * @interface: requested PHY interface
 *
 * Return: a pointer to the DW XPCS handle if successful, otherwise -ENODEV if
 * the PCS device couldn't be found on the bus and other negative errno related
 * to the data allocation and MDIO-bus communications.
 */
struct dw_xpcs *xpcs_create_mdiodev(struct mii_bus *bus, int addr,
				    phy_interface_t interface)
{
	struct mdio_device *mdiodev;
	struct dw_xpcs *xpcs;

	mdiodev = mdio_device_create(bus, addr);
	if (IS_ERR(mdiodev))
		return ERR_CAST(mdiodev);

	xpcs = xpcs_create(mdiodev, NULL, interface);

	/* xpcs_create() has taken a refcount on the mdiodev if it was
	 * successful. If xpcs_create() fails, this will free the mdio
	 * device here. In any case, we don't need to hold our reference
	 * anymore, and putting it here will allow mdio_device_put() in
	 * xpcs_destroy() to automatically free the mdio device.
	 */
	mdio_device_put(mdiodev);

	return xpcs;
}
EXPORT_SYMBOL_GPL(xpcs_create_mdiodev);

/**
 * xpcs_create_fwnode() - Create a DW xPCS instance from @fwnode
 * @fwnode: fwnode handle poining to the DW XPCS device
 * @interface: requested PHY interface
 *
 * Return: a pointer to the DW XPCS handle if successful, otherwise -ENODEV if
 * the fwnode device is unavailable or the PCS device couldn't be found on the
 * bus, -EPROBE_DEFER if the respective MDIO-device instance couldn't be found,
 * other negative errno related to the data allocations and MDIO-bus
 * communications.
 */
struct dw_xpcs *xpcs_create_fwnode(struct fwnode_handle *fwnode,
				   phy_interface_t interface)
{
	struct mdio_device *mdiodev;
	struct dw_xpcs *xpcs;

	if (!fwnode_device_is_available(fwnode))
		return ERR_PTR(-ENODEV);

	mdiodev = fwnode_mdio_find_device(fwnode);
	if (!mdiodev)
		return ERR_PTR(-EPROBE_DEFER);

	xpcs = xpcs_create(mdiodev, NULL, interface);

	/* xpcs_create() has taken a refcount on the mdiodev if it was
	 * successful. If xpcs_create() fails, this will free the mdio
	 * device here. In any case, we don't need to hold our reference
	 * anymore, and putting it here will allow mdio_device_put() in
	 * xpcs_destroy() to automatically free the mdio device.
	 */
	mdio_device_put(mdiodev);

	return xpcs;
}
EXPORT_SYMBOL_GPL(xpcs_create_fwnode);

struct phylink_pcs *xpcs_create_mdiodev_with_phy(struct mii_bus *bus,
						 int mdioaddr, int phyaddr,
						 phy_interface_t interface)
{
	struct mdio_device *mdiodev, *phydev;
	struct dw_xpcs *xpcs;
	void *err_ptr;

	mdiodev = mdio_device_create(bus, mdioaddr);
	if (IS_ERR(mdiodev))
		return ERR_CAST(mdiodev);

	phydev = mdio_device_create(bus, phyaddr);
	if (IS_ERR(phydev)) {
		err_ptr = ERR_CAST(phydev);
		goto err_phydev;
	}

	xpcs = xpcs_create(mdiodev, phydev, interface);
	if (IS_ERR(xpcs)) {
		err_ptr = ERR_CAST(xpcs);
		goto err_xpcs;
	}

	/* xpcs_create() has taken a refcount on the mdiodev if it was
	 * successful. If xpcs_create() fails, this will free the mdio
	 * device here. In any case, we don't need to hold our reference
	 * anymore, and putting it here will allow mdio_device_put() in
	 * xpcs_destroy() to automatically free the mdio device.
	 */
	mdio_device_put(mdiodev);
	mdio_device_put(phydev);

	return &xpcs->pcs;

err_xpcs:
	mdio_device_free(phydev);
err_phydev:
	mdio_device_free(mdiodev);

	return err_ptr;
}
EXPORT_SYMBOL_GPL(xpcs_create_mdiodev_with_phy);

void xpcs_pcs_destroy(struct phylink_pcs *pcs)
{
	struct dw_xpcs *xpcs = phylink_pcs_to_xpcs(pcs);

	if (!xpcs)
		return;

	xpcs_clear_clks(xpcs);
	xpcs_free_data(xpcs);
}
EXPORT_SYMBOL_GPL(xpcs_pcs_destroy);

void xpcs_destroy(struct dw_xpcs *xpcs)
{
	if (!xpcs)
		return;

	xpcs_clear_clks(xpcs);

	xpcs_free_data(xpcs);
}
EXPORT_SYMBOL_GPL(xpcs_destroy);

MODULE_DESCRIPTION("Synopsys DesignWare XPCS library");
MODULE_LICENSE("GPL v2");

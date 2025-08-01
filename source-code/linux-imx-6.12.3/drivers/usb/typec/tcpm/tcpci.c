// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2015-2017 Google, Inc
 *
 * USB Type-C Port Controller Interface.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/usb/pd.h>
#include <linux/usb/tcpci.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/typec.h>

#define	PD_RETRY_COUNT_DEFAULT			3
#define	PD_RETRY_COUNT_3_0_OR_HIGHER		2
#define	AUTO_DISCHARGE_DEFAULT_THRESHOLD_MV	3500
#define	VSINKPD_MIN_IR_DROP_MV			750
#define	VSRC_NEW_MIN_PERCENT			95
#define	VSRC_VALID_MIN_MV			500
#define	VPPS_NEW_MIN_PERCENT			95
#define	VPPS_VALID_MIN_MV			100
#define	VSINKDISCONNECT_PD_MIN_PERCENT		90

struct tcpci {
	struct device *dev;

	struct tcpm_port *port;

	struct regmap *regmap;
	unsigned int alert_mask;

	bool controls_vbus;

	struct tcpc_dev tcpc;
	struct tcpci_data *data;
};

struct tcpci_chip {
	struct tcpci *tcpci;
	struct tcpci_data data;
};

struct tcpm_port *tcpci_get_tcpm_port(struct tcpci *tcpci)
{
	return tcpci->port;
}
EXPORT_SYMBOL_GPL(tcpci_get_tcpm_port);

static inline struct tcpci *tcpc_to_tcpci(struct tcpc_dev *tcpc)
{
	return container_of(tcpc, struct tcpci, tcpc);
}

static int tcpci_read16(struct tcpci *tcpci, unsigned int reg, u16 *val)
{
	return regmap_raw_read(tcpci->regmap, reg, val, sizeof(u16));
}

static int tcpci_write16(struct tcpci *tcpci, unsigned int reg, u16 val)
{
	return regmap_raw_write(tcpci->regmap, reg, &val, sizeof(u16));
}

static int tcpci_check_std_output_cap(struct regmap *regmap, u8 mask)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(regmap, TCPC_STD_OUTPUT_CAP, &reg);
	if (ret < 0)
		return ret;

	return (reg & mask) == mask;
}

static int tcpci_set_cc(struct tcpc_dev *tcpc, enum typec_cc_status cc)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	bool vconn_pres;
	enum typec_cc_polarity polarity = TYPEC_POLARITY_CC1;
	unsigned int reg;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_POWER_STATUS, &reg);
	if (ret < 0)
		return ret;

	vconn_pres = !!(reg & TCPC_POWER_STATUS_VCONN_PRES);
	if (vconn_pres) {
		ret = regmap_read(tcpci->regmap, TCPC_TCPC_CTRL, &reg);
		if (ret < 0)
			return ret;

		if (reg & TCPC_TCPC_CTRL_ORIENTATION)
			polarity = TYPEC_POLARITY_CC2;
	}

	switch (cc) {
	case TYPEC_CC_RA:
		reg = (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RA)
		       | FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RA));
		break;
	case TYPEC_CC_RD:
		reg = (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RD)
		       | FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RD));
		break;
	case TYPEC_CC_RP_DEF:
		reg = (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RP)
		       | FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RP)
		       | FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				    TCPC_ROLE_CTRL_RP_VAL_DEF));
		break;
	case TYPEC_CC_RP_1_5:
		reg = (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RP)
		       | FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RP)
		       | FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				    TCPC_ROLE_CTRL_RP_VAL_1_5));
		break;
	case TYPEC_CC_RP_3_0:
		reg = (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RP)
		       | FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RP)
		       | FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				    TCPC_ROLE_CTRL_RP_VAL_3_0));
		break;
	case TYPEC_CC_OPEN:
	default:
		reg = (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_OPEN)
		       | FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_OPEN));
		break;
	}

	if (vconn_pres) {
		if (polarity == TYPEC_POLARITY_CC2) {
			reg &= ~TCPC_ROLE_CTRL_CC1;
			reg |= FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_OPEN);
		} else {
			reg &= ~TCPC_ROLE_CTRL_CC2;
			reg |= FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_OPEN);
		}
	}

	ret = regmap_write(tcpci->regmap, TCPC_ROLE_CTRL, reg);
	if (ret < 0)
		return ret;

	return 0;
}

static int tcpci_apply_rc(struct tcpc_dev *tcpc, enum typec_cc_status cc,
			  enum typec_cc_polarity polarity)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_ROLE_CTRL, &reg);
	if (ret < 0)
		return ret;

	/*
	 * APPLY_RC state is when ROLE_CONTROL.CC1 != ROLE_CONTROL.CC2 and vbus autodischarge on
	 * disconnect is disabled. Bail out when ROLE_CONTROL.CC1 != ROLE_CONTROL.CC2.
	 */
	if (FIELD_GET(TCPC_ROLE_CTRL_CC2, reg) != FIELD_GET(TCPC_ROLE_CTRL_CC1, reg))
		return 0;

	return regmap_update_bits(tcpci->regmap, TCPC_ROLE_CTRL, polarity == TYPEC_POLARITY_CC1 ?
				  TCPC_ROLE_CTRL_CC2 : TCPC_ROLE_CTRL_CC1,
				  TCPC_ROLE_CTRL_CC_OPEN);
}

static int tcpci_start_toggling(struct tcpc_dev *tcpc,
				enum typec_port_type port_type,
				enum typec_cc_status cc)
{
	int ret;
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg = TCPC_ROLE_CTRL_DRP;

	/* Handle vendor drp toggling */
	if (tcpci->data->start_drp_toggling) {
		ret = tcpci->data->start_drp_toggling(tcpci, tcpci->data, cc);
		if (ret < 0)
			return ret;
	}

	switch (cc) {
	default:
	case TYPEC_CC_RP_DEF:
		reg |= FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				  TCPC_ROLE_CTRL_RP_VAL_DEF);
		break;
	case TYPEC_CC_RP_1_5:
		reg |= FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				  TCPC_ROLE_CTRL_RP_VAL_1_5);
		break;
	case TYPEC_CC_RP_3_0:
		reg |= FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				  TCPC_ROLE_CTRL_RP_VAL_3_0);
		break;
	}

	if (cc == TYPEC_CC_RD)
		reg |= (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RD)
			| FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RD));
	else
		reg |= (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RP)
			| FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RP));
	ret = regmap_write(tcpci->regmap, TCPC_ROLE_CTRL, reg);
	if (ret < 0)
		return ret;
	return regmap_write(tcpci->regmap, TCPC_COMMAND,
			    TCPC_CMD_LOOK4CONNECTION);
}

static int tcpci_get_cc(struct tcpc_dev *tcpc,
			enum typec_cc_status *cc1, enum typec_cc_status *cc2)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg, role_control;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_ROLE_CTRL, &role_control);
	if (ret < 0)
		return ret;

	ret = regmap_read(tcpci->regmap, TCPC_CC_STATUS, &reg);
	if (ret < 0)
		return ret;

	*cc1 = tcpci_to_typec_cc(FIELD_GET(TCPC_CC_STATUS_CC1, reg),
				 reg & TCPC_CC_STATUS_TERM ||
				 tcpc_presenting_rd(role_control, CC1));
	*cc2 = tcpci_to_typec_cc(FIELD_GET(TCPC_CC_STATUS_CC2, reg),
				 reg & TCPC_CC_STATUS_TERM ||
				 tcpc_presenting_rd(role_control, CC2));

	return 0;
}

static int tcpci_set_polarity(struct tcpc_dev *tcpc,
			      enum typec_cc_polarity polarity)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;
	enum typec_cc_status cc1, cc2;

	/* Obtain Rp setting from role control */
	ret = regmap_read(tcpci->regmap, TCPC_ROLE_CTRL, &reg);
	if (ret < 0)
		return ret;

	ret = tcpci_get_cc(tcpc, &cc1, &cc2);
	if (ret < 0)
		return ret;

	/*
	 * When port has drp toggling enabled, ROLE_CONTROL would only have the initial
	 * terminations for the toggling and does not indicate the final cc
	 * terminations when ConnectionResult is 0 i.e. drp toggling stops and
	 * the connection is resolved. Infer port role from TCPC_CC_STATUS based on the
	 * terminations seen. The port role is then used to set the cc terminations.
	 */
	if (reg & TCPC_ROLE_CTRL_DRP) {
		/* Disable DRP for the OPEN setting to take effect */
		reg = reg & ~TCPC_ROLE_CTRL_DRP;

		if (polarity == TYPEC_POLARITY_CC2) {
			reg &= ~TCPC_ROLE_CTRL_CC2;
			/* Local port is source */
			if (cc2 == TYPEC_CC_RD)
				/* Role control would have the Rp setting when DRP was enabled */
				reg |= FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RP);
			else
				reg |= FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RD);
		} else {
			reg &= ~TCPC_ROLE_CTRL_CC1;
			/* Local port is source */
			if (cc1 == TYPEC_CC_RD)
				/* Role control would have the Rp setting when DRP was enabled */
				reg |= FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RP);
			else
				reg |= FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RD);
		}
	}

	if (polarity == TYPEC_POLARITY_CC2)
		reg |= FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_OPEN);
	else
		reg |= FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_OPEN);
	ret = regmap_write(tcpci->regmap, TCPC_ROLE_CTRL, reg);
	if (ret < 0)
		return ret;

	return regmap_write(tcpci->regmap, TCPC_TCPC_CTRL,
			   (polarity == TYPEC_POLARITY_CC2) ?
			   TCPC_TCPC_CTRL_ORIENTATION : 0);
}

static int tcpci_set_orientation(struct tcpc_dev *tcpc,
				 enum typec_orientation orientation)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;

	switch (orientation) {
	case TYPEC_ORIENTATION_NONE:
		/* We can't put a single output into high impedance */
		fallthrough;
	case TYPEC_ORIENTATION_NORMAL:
		reg = TCPC_CONFIG_STD_OUTPUT_ORIENTATION_NORMAL;
		break;
	case TYPEC_ORIENTATION_REVERSE:
		reg = TCPC_CONFIG_STD_OUTPUT_ORIENTATION_FLIPPED;
		break;
	}

	return regmap_update_bits(tcpci->regmap, TCPC_CONFIG_STD_OUTPUT,
				  TCPC_CONFIG_STD_OUTPUT_ORIENTATION_MASK, reg);
}

static void tcpci_set_partner_usb_comm_capable(struct tcpc_dev *tcpc, bool capable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);

	if (tcpci->data->set_partner_usb_comm_capable)
		tcpci->data->set_partner_usb_comm_capable(tcpci, tcpci->data, capable);
}

static int tcpci_set_vconn(struct tcpc_dev *tcpc, bool enable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	int ret;

	/* Handle vendor set vconn */
	if (tcpci->data->set_vconn) {
		ret = tcpci->data->set_vconn(tcpci, tcpci->data, enable);
		if (ret < 0)
			return ret;
	}

	return regmap_update_bits(tcpci->regmap, TCPC_POWER_CTRL,
				TCPC_POWER_CTRL_VCONN_ENABLE,
				enable ? TCPC_POWER_CTRL_VCONN_ENABLE : 0);
}

static int tcpci_enable_auto_vbus_discharge(struct tcpc_dev *dev, bool enable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(dev);
	int ret;

	ret = regmap_update_bits(tcpci->regmap, TCPC_POWER_CTRL, TCPC_POWER_CTRL_AUTO_DISCHARGE,
				 enable ? TCPC_POWER_CTRL_AUTO_DISCHARGE : 0);
	return ret;
}

static int tcpci_set_auto_vbus_discharge_threshold(struct tcpc_dev *dev, enum typec_pwr_opmode mode,
						   bool pps_active, u32 requested_vbus_voltage_mv)
{
	struct tcpci *tcpci = tcpc_to_tcpci(dev);
	unsigned int pwr_ctrl, threshold = 0;
	int ret;

	/*
	 * Indicates that vbus is going to go away due PR_SWAP, hard reset etc.
	 * Do not discharge vbus here.
	 */
	if (requested_vbus_voltage_mv == 0)
		goto write_thresh;

	ret = regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &pwr_ctrl);
	if (ret < 0)
		return ret;

	if (pwr_ctrl & TCPC_FAST_ROLE_SWAP_EN) {
		/* To prevent disconnect when the source is fast role swap is capable. */
		threshold = AUTO_DISCHARGE_DEFAULT_THRESHOLD_MV;
	} else if (mode == TYPEC_PWR_MODE_PD) {
		if (pps_active)
			threshold = ((VPPS_NEW_MIN_PERCENT * requested_vbus_voltage_mv / 100) -
				     VSINKPD_MIN_IR_DROP_MV - VPPS_VALID_MIN_MV) *
				     VSINKDISCONNECT_PD_MIN_PERCENT / 100;
		else
			threshold = ((VSRC_NEW_MIN_PERCENT * requested_vbus_voltage_mv / 100) -
				     VSINKPD_MIN_IR_DROP_MV - VSRC_VALID_MIN_MV) *
				     VSINKDISCONNECT_PD_MIN_PERCENT / 100;
	} else {
		/* 3.5V for non-pd sink */
		threshold = AUTO_DISCHARGE_DEFAULT_THRESHOLD_MV;
	}

	threshold = threshold / TCPC_VBUS_SINK_DISCONNECT_THRESH_LSB_MV;

	if (threshold > TCPC_VBUS_SINK_DISCONNECT_THRESH_MAX)
		return -EINVAL;

write_thresh:
	return tcpci_write16(tcpci, TCPC_VBUS_SINK_DISCONNECT_THRESH, threshold);
}

static int tcpci_enable_frs(struct tcpc_dev *dev, bool enable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(dev);
	int ret;

	/* To prevent disconnect during FRS, set disconnect threshold to 3.5V */
	ret = tcpci_write16(tcpci, TCPC_VBUS_SINK_DISCONNECT_THRESH, enable ? 0 : 0x8c);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(tcpci->regmap, TCPC_POWER_CTRL, TCPC_FAST_ROLE_SWAP_EN, enable ?
				 TCPC_FAST_ROLE_SWAP_EN : 0);

	return ret;
}

static void tcpci_frs_sourcing_vbus(struct tcpc_dev *dev)
{
	struct tcpci *tcpci = tcpc_to_tcpci(dev);

	if (tcpci->data->frs_sourcing_vbus)
		tcpci->data->frs_sourcing_vbus(tcpci, tcpci->data);
}

static void tcpci_check_contaminant(struct tcpc_dev *dev)
{
	struct tcpci *tcpci = tcpc_to_tcpci(dev);

	if (tcpci->data->check_contaminant)
		tcpci->data->check_contaminant(tcpci, tcpci->data);
}

static int tcpci_set_bist_data(struct tcpc_dev *tcpc, bool enable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);

	return regmap_update_bits(tcpci->regmap, TCPC_TCPC_CTRL, TCPC_TCPC_CTRL_BIST_TM,
				 enable ? TCPC_TCPC_CTRL_BIST_TM : 0);
}

static int tcpci_set_roles(struct tcpc_dev *tcpc, bool attached,
			   enum typec_role role, enum typec_data_role data)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	reg = FIELD_PREP(TCPC_MSG_HDR_INFO_REV, PD_REV20);
	if (role == TYPEC_SOURCE)
		reg |= TCPC_MSG_HDR_INFO_PWR_ROLE;
	if (data == TYPEC_HOST)
		reg |= TCPC_MSG_HDR_INFO_DATA_ROLE;
	ret = regmap_write(tcpci->regmap, TCPC_MSG_HDR_INFO, reg);
	if (ret < 0)
		return ret;

	return 0;
}

static int tcpci_set_pd_rx(struct tcpc_dev *tcpc, bool enable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg = 0;
	int ret;

	if (enable) {
		reg = TCPC_RX_DETECT_SOP | TCPC_RX_DETECT_HARD_RESET;
		if (tcpci->data->cable_comm_capable)
			reg |= TCPC_RX_DETECT_SOP1;
	}
	ret = regmap_write(tcpci->regmap, TCPC_RX_DETECT, reg);
	if (ret < 0)
		return ret;

	return 0;
}

static int tcpci_get_vbus(struct tcpc_dev *tcpc)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_POWER_STATUS, &reg);
	if (ret < 0)
		return ret;

	return !!(reg & TCPC_POWER_STATUS_VBUS_PRES);
}

static bool tcpci_is_vbus_vsafe0v(struct tcpc_dev *tcpc)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_EXTENDED_STATUS, &reg);
	if (ret < 0)
		return false;

	return !!(reg & TCPC_EXTENDED_STATUS_VSAFE0V);
}

static int tcpci_vbus_force_discharge(struct tcpc_dev *tcpc, bool enable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	if (enable)
		regmap_write(tcpci->regmap,
			TCPC_VBUS_VOLTAGE_ALARM_LO_CFG, 0x1c);
	else
		regmap_write(tcpci->regmap,
			TCPC_VBUS_VOLTAGE_ALARM_LO_CFG, 0);

	regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);

	if (enable)
		reg |= TCPC_POWER_CTRL_FORCEDISCH;
	else
		reg &= ~TCPC_POWER_CTRL_FORCEDISCH;
	ret = regmap_write(tcpci->regmap, TCPC_POWER_CTRL, reg);
	if (ret < 0)
		return ret;

	return 0;
}

static int tcpci_set_vbus(struct tcpc_dev *tcpc, bool source, bool sink)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	int ret;

	if (tcpci->data->set_vbus) {
		ret = tcpci->data->set_vbus(tcpci, tcpci->data, source, sink);
		/* Bypass when ret > 0 */
		if (ret != 0)
			return ret < 0 ? ret : 0;
	}

	/* Disable both source and sink first before enabling anything */

	if (!source) {
		ret = regmap_write(tcpci->regmap, TCPC_COMMAND,
				   TCPC_CMD_DISABLE_SRC_VBUS);
		if (ret < 0)
			return ret;
	}

	if (!sink) {
		ret = regmap_write(tcpci->regmap, TCPC_COMMAND,
				   TCPC_CMD_DISABLE_SINK_VBUS);
		if (ret < 0)
			return ret;
	}

	if (!source && !sink)
		tcpci_vbus_force_discharge(tcpc, true);

	if (source) {
		ret = regmap_write(tcpci->regmap, TCPC_COMMAND,
				   TCPC_CMD_SRC_VBUS_DEFAULT);
		if (ret < 0)
			return ret;
	}

	if (sink) {
		ret = regmap_write(tcpci->regmap, TCPC_COMMAND,
				   TCPC_CMD_SINK_VBUS);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int tcpci_pd_transmit(struct tcpc_dev *tcpc, enum tcpm_transmit_type type,
			     const struct pd_message *msg, unsigned int negotiated_rev)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	u16 header = msg ? le16_to_cpu(msg->header) : 0;
	unsigned int reg, cnt;
	int ret;

	cnt = msg ? pd_header_cnt(header) * 4 : 0;
	/**
	 * TCPCI spec forbids direct access of TCPC_TX_DATA.
	 * But, since some of the chipsets offer this capability,
	 * it's fair to support both.
	 */
	if (tcpci->data->TX_BUF_BYTE_x_hidden) {
		u8 buf[TCPC_TRANSMIT_BUFFER_MAX_LEN] = {0,};
		u8 pos = 0;

		/* Payload + header + TCPC_TX_BYTE_CNT */
		buf[pos++] = cnt + 2;

		if (msg)
			memcpy(&buf[pos], &msg->header, sizeof(msg->header));

		pos += sizeof(header);

		if (cnt > 0)
			memcpy(&buf[pos], msg->payload, cnt);

		pos += cnt;
		ret = regmap_raw_write(tcpci->regmap, TCPC_TX_BYTE_CNT, buf, pos);
		if (ret < 0)
			return ret;
	} else {
		ret = regmap_write(tcpci->regmap, TCPC_TX_BYTE_CNT, cnt + 2);
		if (ret < 0)
			return ret;

		ret = tcpci_write16(tcpci, TCPC_TX_HDR, header);
		if (ret < 0)
			return ret;

		if (cnt > 0) {
			ret = regmap_raw_write(tcpci->regmap, TCPC_TX_DATA, &msg->payload, cnt);
			if (ret < 0)
				return ret;
		}
	}

	/* nRetryCount is 3 in PD2.0 spec where 2 in PD3.0 spec */
	reg = FIELD_PREP(TCPC_TRANSMIT_RETRY,
			 (negotiated_rev > PD_REV20
			  ? PD_RETRY_COUNT_3_0_OR_HIGHER
			  : PD_RETRY_COUNT_DEFAULT));
	reg |= FIELD_PREP(TCPC_TRANSMIT_TYPE, type);
	ret = regmap_write(tcpci->regmap, TCPC_TRANSMIT, reg);
	if (ret < 0)
		return ret;

	return 0;
}

static bool tcpci_cable_comm_capable(struct tcpc_dev *tcpc)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);

	return tcpci->data->cable_comm_capable;
}

static bool tcpci_attempt_vconn_swap_discovery(struct tcpc_dev *tcpc)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);

	if (tcpci->data->attempt_vconn_swap_discovery)
		return tcpci->data->attempt_vconn_swap_discovery(tcpci, tcpci->data);

	return false;
}

static int tcpci_init(struct tcpc_dev *tcpc)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned long timeout = jiffies + msecs_to_jiffies(2000); /* XXX */
	unsigned int reg;
	int ret;

	while (time_before_eq(jiffies, timeout)) {
		ret = regmap_read(tcpci->regmap, TCPC_POWER_STATUS, &reg);
		if (ret < 0)
			return ret;
		if (!(reg & TCPC_POWER_STATUS_UNINIT))
			break;
		usleep_range(10000, 20000);
	}
	if (time_after(jiffies, timeout))
		return -ETIMEDOUT;

	ret = tcpci_write16(tcpci, TCPC_FAULT_STATUS, TCPC_FAULT_STATUS_ALL_REG_RST_TO_DEFAULT);
	if (ret < 0)
		return ret;

	/* Handle vendor init */
	if (tcpci->data->init) {
		ret = tcpci->data->init(tcpci, tcpci->data);
		if (ret < 0)
			return ret;
	}

	/* Clear all events */
	ret = tcpci_write16(tcpci, TCPC_ALERT, 0xffff);
	if (ret < 0)
		return ret;

	/* Clear fault condition */
	regmap_write(tcpci->regmap, TCPC_FAULT_STATUS, 0x80);

	if (tcpci->controls_vbus)
		reg = TCPC_POWER_STATUS_VBUS_PRES;
	else
		reg = 0;
	ret = regmap_write(tcpci->regmap, TCPC_POWER_STATUS_MASK, reg);
	if (ret < 0)
		return ret;

	/* Enable Vbus detection */
	ret = regmap_write(tcpci->regmap, TCPC_COMMAND,
			   TCPC_CMD_ENABLE_VBUS_DETECT);
	if (ret < 0)
		return ret;

	reg = TCPC_ALERT_TX_SUCCESS | TCPC_ALERT_TX_FAILED |
		TCPC_ALERT_TX_DISCARDED | TCPC_ALERT_RX_STATUS |
		TCPC_ALERT_RX_HARD_RST | TCPC_ALERT_CC_STATUS |
		TCPC_ALERT_V_ALARM_LO | TCPC_ALERT_FAULT;
	if (tcpci->controls_vbus)
		reg |= TCPC_ALERT_POWER_STATUS;
	/* Enable VSAFE0V status interrupt when detecting VSAFE0V is supported */
	if (tcpci->data->vbus_vsafe0v) {
		reg |= TCPC_ALERT_EXTENDED_STATUS;
		ret = regmap_write(tcpci->regmap, TCPC_EXTENDED_STATUS_MASK,
				   TCPC_EXTENDED_STATUS_VSAFE0V);
		if (ret < 0)
			return ret;
	}

	tcpci->alert_mask = reg;

	return 0;
}

irqreturn_t tcpci_irq(struct tcpci *tcpci)
{
	u16 status;
	int ret;
	int irq_ret;
	unsigned int raw;

	tcpci_read16(tcpci, TCPC_ALERT, &status);
	irq_ret = status & tcpci->alert_mask;

process_status:
	/*
	 * Clear alert status for everything except RX_STATUS, which shouldn't
	 * be cleared until we have successfully retrieved message.
	 */
	if (status & ~TCPC_ALERT_RX_STATUS)
		tcpci_write16(tcpci, TCPC_ALERT,
			      status & ~TCPC_ALERT_RX_STATUS);

	if (status & TCPC_ALERT_CC_STATUS)
		tcpm_cc_change(tcpci->port);

	if (status & TCPC_ALERT_POWER_STATUS) {
		/* Read power status to clear the event */
		regmap_read(tcpci->regmap, TCPC_POWER_STATUS, &raw);
		regmap_read(tcpci->regmap, TCPC_POWER_STATUS_MASK, &raw);

		/*
		 * If power status mask has been reset, then the TCPC
		 * has reset.
		 */
		if (raw == 0xff)
			tcpm_tcpc_reset(tcpci->port);
		else
			tcpm_vbus_change(tcpci->port);
	}

	if (status & TCPC_ALERT_V_ALARM_LO)
		tcpci_vbus_force_discharge(&tcpci->tcpc, false);

	if (status & TCPC_ALERT_RX_STATUS) {
		struct pd_message msg;
		unsigned int cnt, payload_cnt;
		u16 header;

		regmap_read(tcpci->regmap, TCPC_RX_BYTE_CNT, &cnt);
		/*
		 * 'cnt' corresponds to READABLE_BYTE_COUNT in section 4.4.14
		 * of the TCPCI spec [Rev 2.0 Ver 1.0 October 2017] and is
		 * defined in table 4-36 as one greater than the number of
		 * bytes received. And that number includes the header. So:
		 */
		if (cnt > 3)
			payload_cnt = cnt - (1 + sizeof(msg.header));
		else
			payload_cnt = 0;

		tcpci_read16(tcpci, TCPC_RX_HDR, &header);
		msg.header = cpu_to_le16(header);

		if (WARN_ON(payload_cnt > sizeof(msg.payload)))
			payload_cnt = sizeof(msg.payload);

		if (payload_cnt > 0)
			regmap_raw_read(tcpci->regmap, TCPC_RX_DATA,
					&msg.payload, payload_cnt);

		/* Read complete, clear RX status alert bit */
		tcpci_write16(tcpci, TCPC_ALERT, TCPC_ALERT_RX_STATUS);

		tcpm_pd_receive(tcpci->port, &msg, TCPC_TX_SOP);
	}

	if (tcpci->data->vbus_vsafe0v && (status & TCPC_ALERT_EXTENDED_STATUS)) {
		ret = regmap_read(tcpci->regmap, TCPC_EXTENDED_STATUS, &raw);
		if (!ret && (raw & TCPC_EXTENDED_STATUS_VSAFE0V))
			tcpm_vbus_change(tcpci->port);
	}

	/* Clear the fault status anyway */
	if (status & TCPC_ALERT_FAULT) {
		regmap_read(tcpci->regmap, TCPC_FAULT_STATUS, &raw);
		regmap_write(tcpci->regmap, TCPC_FAULT_STATUS,
				raw | TCPC_FAULT_STATUS_CLEAR);
	}

	if (status & TCPC_ALERT_RX_HARD_RST)
		tcpm_pd_hard_reset(tcpci->port);

	if (status & TCPC_ALERT_TX_SUCCESS)
		tcpm_pd_transmit_complete(tcpci->port, TCPC_TX_SUCCESS);
	else if (status & TCPC_ALERT_TX_DISCARDED)
		tcpm_pd_transmit_complete(tcpci->port, TCPC_TX_DISCARDED);
	else if (status & TCPC_ALERT_TX_FAILED)
		tcpm_pd_transmit_complete(tcpci->port, TCPC_TX_FAILED);

	tcpci_read16(tcpci, TCPC_ALERT, &status);

	if (status & tcpci->alert_mask)
		goto process_status;

	return IRQ_RETVAL(irq_ret);
}
EXPORT_SYMBOL_GPL(tcpci_irq);

static irqreturn_t _tcpci_irq(int irq, void *dev_id)
{
	struct tcpci_chip *chip = dev_id;

	return tcpci_irq(chip->tcpci);
}

static const struct regmap_config tcpci_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0x7F, /* 0x80 .. 0xFF are vendor defined */
};

static int tcpci_parse_config(struct tcpci *tcpci)
{
	tcpci->controls_vbus = true; /* XXX */

	tcpci->tcpc.fwnode = device_get_named_child_node(tcpci->dev,
							 "connector");
	if (!tcpci->tcpc.fwnode) {
		dev_err(tcpci->dev, "Can't find connector node.\n");
		return -EINVAL;
	}

	return 0;
}

struct tcpci *tcpci_register_port(struct device *dev, struct tcpci_data *data)
{
	struct tcpci *tcpci;
	int err;

	tcpci = devm_kzalloc(dev, sizeof(*tcpci), GFP_KERNEL);
	if (!tcpci)
		return ERR_PTR(-ENOMEM);

	tcpci->dev = dev;
	tcpci->data = data;
	tcpci->regmap = data->regmap;

	tcpci->tcpc.init = tcpci_init;
	tcpci->tcpc.get_vbus = tcpci_get_vbus;
	tcpci->tcpc.set_vbus = tcpci_set_vbus;
	tcpci->tcpc.set_cc = tcpci_set_cc;
	tcpci->tcpc.apply_rc = tcpci_apply_rc;
	tcpci->tcpc.get_cc = tcpci_get_cc;
	tcpci->tcpc.set_polarity = tcpci_set_polarity;
	tcpci->tcpc.set_vconn = tcpci_set_vconn;
	tcpci->tcpc.start_toggling = tcpci_start_toggling;

	tcpci->tcpc.set_pd_rx = tcpci_set_pd_rx;
	tcpci->tcpc.set_roles = tcpci_set_roles;
	tcpci->tcpc.pd_transmit = tcpci_pd_transmit;
	tcpci->tcpc.set_bist_data = tcpci_set_bist_data;
	tcpci->tcpc.enable_frs = tcpci_enable_frs;
	tcpci->tcpc.frs_sourcing_vbus = tcpci_frs_sourcing_vbus;
	tcpci->tcpc.set_partner_usb_comm_capable = tcpci_set_partner_usb_comm_capable;
	tcpci->tcpc.cable_comm_capable = tcpci_cable_comm_capable;
	tcpci->tcpc.attempt_vconn_swap_discovery = tcpci_attempt_vconn_swap_discovery;

	if (tcpci->data->check_contaminant)
		tcpci->tcpc.check_contaminant = tcpci_check_contaminant;

	if (tcpci->data->auto_discharge_disconnect) {
		tcpci->tcpc.enable_auto_vbus_discharge = tcpci_enable_auto_vbus_discharge;
		tcpci->tcpc.set_auto_vbus_discharge_threshold =
			tcpci_set_auto_vbus_discharge_threshold;
		regmap_update_bits(tcpci->regmap, TCPC_POWER_CTRL, TCPC_POWER_CTRL_BLEED_DISCHARGE,
				   TCPC_POWER_CTRL_BLEED_DISCHARGE);
	}

	if (tcpci->data->vbus_vsafe0v)
		tcpci->tcpc.is_vbus_vsafe0v = tcpci_is_vbus_vsafe0v;

	if (tcpci->data->set_orientation)
		tcpci->tcpc.set_orientation = tcpci_set_orientation;

	err = tcpci_parse_config(tcpci);
	if (err < 0)
		return ERR_PTR(err);

	tcpci->port = tcpm_register_port(tcpci->dev, &tcpci->tcpc);
	if (IS_ERR(tcpci->port)) {
		fwnode_handle_put(tcpci->tcpc.fwnode);
		return ERR_CAST(tcpci->port);
	}

	return tcpci;
}
EXPORT_SYMBOL_GPL(tcpci_register_port);

void tcpci_unregister_port(struct tcpci *tcpci)
{
	tcpm_unregister_port(tcpci->port);
	fwnode_handle_put(tcpci->tcpc.fwnode);
}
EXPORT_SYMBOL_GPL(tcpci_unregister_port);

static int tcpci_probe(struct i2c_client *client)
{
	struct tcpci_chip *chip;
	int err;
	u16 val = 0;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->data.regmap = devm_regmap_init_i2c(client, &tcpci_regmap_config);
	if (IS_ERR(chip->data.regmap))
		return PTR_ERR(chip->data.regmap);

	i2c_set_clientdata(client, chip);

	/* Disable chip interrupts before requesting irq */
	err = regmap_raw_write(chip->data.regmap, TCPC_ALERT_MASK, &val,
			       sizeof(u16));
	if (err < 0)
		return err;

	err = tcpci_check_std_output_cap(chip->data.regmap,
					 TCPC_STD_OUTPUT_CAP_ORIENTATION);
	if (err < 0)
		return err;

	chip->data.set_orientation = err;

	chip->tcpci = tcpci_register_port(&client->dev, &chip->data);
	if (IS_ERR(chip->tcpci))
		return PTR_ERR(chip->tcpci);

	irq_set_status_flags(client->irq, IRQ_DISABLE_UNLAZY);
	err = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					_tcpci_irq,
					IRQF_SHARED | IRQF_ONESHOT,
					dev_name(&client->dev), chip);
	if (err < 0)
		goto unregister_port;

	/* Enable chip interrupts at last */
	err = tcpci_write16(chip->tcpci, TCPC_ALERT_MASK, chip->tcpci->alert_mask);
	if (err < 0)
		goto unregister_port;

	device_set_wakeup_capable(chip->tcpci->dev, true);

	return 0;

unregister_port:
	tcpci_unregister_port(chip->tcpci);
	return err;
}

static void tcpci_remove(struct i2c_client *client)
{
	struct tcpci_chip *chip = i2c_get_clientdata(client);
	int err;

	/* Disable chip interrupts before unregistering port */
	err = tcpci_write16(chip->tcpci, TCPC_ALERT_MASK, 0);
	if (err < 0)
		dev_warn(&client->dev, "Failed to disable irqs (%pe)\n", ERR_PTR(err));

	tcpci_unregister_port(chip->tcpci);
	irq_clear_status_flags(client->irq, IRQ_DISABLE_UNLAZY);
}

static int __maybe_unused tcpci_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(i2c->irq);
	else
		disable_irq(i2c->irq);

	return 0;
}


static int __maybe_unused tcpci_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(i2c->irq);
	else
		enable_irq(i2c->irq);

	return 0;
}

static const struct dev_pm_ops tcpci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tcpci_suspend, tcpci_resume)
};

static const struct i2c_device_id tcpci_id[] = {
	{ "tcpci" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tcpci_id);

#ifdef CONFIG_OF
static const struct of_device_id tcpci_of_match[] = {
	{ .compatible = "nxp,ptn5110", },
	{ .compatible = "tcpci", },
	{},
};
MODULE_DEVICE_TABLE(of, tcpci_of_match);
#endif

static struct i2c_driver tcpci_i2c_driver = {
	.driver = {
		.name = "tcpci",
		.pm = &tcpci_pm_ops,
		.of_match_table = of_match_ptr(tcpci_of_match),
	},
	.probe = tcpci_probe,
	.remove = tcpci_remove,
	.id_table = tcpci_id,
};
module_i2c_driver(tcpci_i2c_driver);

MODULE_DESCRIPTION("USB Type-C Port Controller Interface driver");
MODULE_LICENSE("GPL");

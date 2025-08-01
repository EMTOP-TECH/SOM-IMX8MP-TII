/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2015-2017 Google, Inc
 *
 * USB Type-C Port Controller Interface.
 */

#ifndef __LINUX_USB_TCPCI_H
#define __LINUX_USB_TCPCI_H

#include <linux/usb/typec.h>
#include <linux/usb/tcpm.h>

#define TCPC_VENDOR_ID			0x0
#define TCPC_PRODUCT_ID			0x2
#define TCPC_BCD_DEV			0x4
#define TCPC_TC_REV			0x6
#define TCPC_PD_REV			0x8
#define TCPC_PD_INT_REV			0xa

#define TCPC_ALERT			0x10
#define TCPC_ALERT_EXTND		BIT(14)
#define TCPC_ALERT_EXTENDED_STATUS	BIT(13)
#define TCPC_ALERT_VBUS_DISCNCT		BIT(11)
#define TCPC_ALERT_RX_BUF_OVF		BIT(10)
#define TCPC_ALERT_FAULT		BIT(9)
#define TCPC_ALERT_V_ALARM_LO		BIT(8)
#define TCPC_ALERT_V_ALARM_HI		BIT(7)
#define TCPC_ALERT_TX_SUCCESS		BIT(6)
#define TCPC_ALERT_TX_DISCARDED		BIT(5)
#define TCPC_ALERT_TX_FAILED		BIT(4)
#define TCPC_ALERT_RX_HARD_RST		BIT(3)
#define TCPC_ALERT_RX_STATUS		BIT(2)
#define TCPC_ALERT_POWER_STATUS		BIT(1)
#define TCPC_ALERT_CC_STATUS		BIT(0)

#define TCPC_ALERT_MASK			0x12
#define TCPC_POWER_STATUS_MASK		0x14

#define TCPC_FAULT_STATUS_MASK			0x15
#define TCPC_FAULT_STATUS_MASK_VCONN_OC		BIT(1)
#define TCPC_FAULT_STATUS_CLEAR		BIT(7)

#define TCPC_EXTENDED_STATUS_MASK		0x16
#define TCPC_EXTENDED_STATUS_MASK_VSAFE0V	BIT(0)

#define TCPC_ALERT_EXTENDED_MASK	0x17
#define TCPC_SINK_FAST_ROLE_SWAP	BIT(0)

#define TCPC_CONFIG_STD_OUTPUT		0x18
#define TCPC_CONFIG_STD_OUTPUT_ORIENTATION_MASK		BIT(0)
#define TCPC_CONFIG_STD_OUTPUT_ORIENTATION_NORMAL	0
#define TCPC_CONFIG_STD_OUTPUT_ORIENTATION_FLIPPED	1

#define TCPC_TCPC_CTRL			0x19
#define TCPC_TCPC_CTRL_ORIENTATION	BIT(0)
#define PLUG_ORNT_CC1			0
#define PLUG_ORNT_CC2			1
#define TCPC_TCPC_CTRL_BIST_TM		BIT(1)
#define TCPC_TCPC_CTRL_EN_LK4CONN_ALRT	BIT(6)

#define TCPC_EXTENDED_STATUS		0x20
#define TCPC_EXTENDED_STATUS_VSAFE0V	BIT(0)

#define TCPC_ROLE_CTRL			0x1a
#define TCPC_ROLE_CTRL_DRP		BIT(6)
#define TCPC_ROLE_CTRL_RP_VAL		GENMASK(5, 4)
#define TCPC_ROLE_CTRL_RP_VAL_DEF	0x0
#define TCPC_ROLE_CTRL_RP_VAL_1_5	0x1
#define TCPC_ROLE_CTRL_RP_VAL_3_0	0x2
#define TCPC_ROLE_CTRL_CC2		GENMASK(3, 2)
#define TCPC_ROLE_CTRL_CC1		GENMASK(1, 0)
#define TCPC_ROLE_CTRL_CC_RA		0x0
#define TCPC_ROLE_CTRL_CC_RP		0x1
#define TCPC_ROLE_CTRL_CC_RD		0x2
#define TCPC_ROLE_CTRL_CC_OPEN		0x3

#define TCPC_FAULT_CTRL			0x1b

#define TCPC_POWER_CTRL			0x1c
#define TCPC_POWER_CTRL_VCONN_ENABLE	BIT(0)
#define TCPC_POWER_CTRL_FORCEDISCH	BIT(2)
#define TCPC_POWER_CTRL_BLEED_DISCHARGE	BIT(3)
#define TCPC_POWER_CTRL_AUTO_DISCHARGE	BIT(4)
#define TCPC_DIS_VOLT_ALRM		BIT(5)
#define TCPC_POWER_CTRL_VBUS_VOLT_MON	BIT(6)
#define TCPC_FAST_ROLE_SWAP_EN		BIT(7)

#define TCPC_CC_STATUS			0x1d
#define TCPC_CC_STATUS_TOGGLING		BIT(5)
#define TCPC_CC_STATUS_TERM		BIT(4)
#define TCPC_CC_STATUS_TERM_RP		0
#define TCPC_CC_STATUS_TERM_RD		1
#define TCPC_CC_STATUS_CC2		GENMASK(3, 2)
#define TCPC_CC_STATUS_CC1		GENMASK(1, 0)
#define TCPC_CC_STATE_SRC_OPEN		0

#define TCPC_POWER_STATUS		0x1e
#define TCPC_POWER_STATUS_DBG_ACC_CON	BIT(7)
#define TCPC_POWER_STATUS_UNINIT	BIT(6)
#define TCPC_POWER_STATUS_SOURCING_VBUS	BIT(4)
#define TCPC_POWER_STATUS_VBUS_DET	BIT(3)
#define TCPC_POWER_STATUS_VBUS_PRES	BIT(2)
#define TCPC_POWER_STATUS_VCONN_PRES	BIT(1)
#define TCPC_POWER_STATUS_SINKING_VBUS	BIT(0)

#define TCPC_FAULT_STATUS		0x1f
#define TCPC_FAULT_STATUS_ALL_REG_RST_TO_DEFAULT BIT(7)
#define TCPC_FAULT_STATUS_VCONN_OC	BIT(1)

#define TCPC_ALERT_EXTENDED		0x21

#define TCPC_COMMAND			0x23
#define TCPC_CMD_WAKE_I2C		0x11
#define TCPC_CMD_DISABLE_VBUS_DETECT	0x22
#define TCPC_CMD_ENABLE_VBUS_DETECT	0x33
#define TCPC_CMD_DISABLE_SINK_VBUS	0x44
#define TCPC_CMD_SINK_VBUS		0x55
#define TCPC_CMD_DISABLE_SRC_VBUS	0x66
#define TCPC_CMD_SRC_VBUS_DEFAULT	0x77
#define TCPC_CMD_SRC_VBUS_HIGH		0x88
#define TCPC_CMD_LOOK4CONNECTION	0x99
#define TCPC_CMD_RXONEMORE		0xAA
#define TCPC_CMD_I2C_IDLE		0xFF

#define TCPC_DEV_CAP_1			0x24
#define TCPC_DEV_CAP_2			0x26
#define TCPC_STD_INPUT_CAP		0x28
#define TCPC_STD_OUTPUT_CAP		0x29
#define TCPC_STD_OUTPUT_CAP_ORIENTATION	BIT(0)

#define TCPC_MSG_HDR_INFO		0x2e
#define TCPC_MSG_HDR_INFO_DATA_ROLE	BIT(3)
#define TCPC_MSG_HDR_INFO_REV		GENMASK(2, 1)
#define TCPC_MSG_HDR_INFO_PWR_ROLE	BIT(0)

#define TCPC_RX_DETECT			0x2f
#define TCPC_RX_DETECT_HARD_RESET	BIT(5)
#define TCPC_RX_DETECT_SOP		BIT(0)
#define TCPC_RX_DETECT_SOP1		BIT(1)
#define TCPC_RX_DETECT_SOP2		BIT(2)
#define TCPC_RX_DETECT_DBG1		BIT(3)
#define TCPC_RX_DETECT_DBG2		BIT(4)

#define TCPC_RX_BYTE_CNT		0x30
#define TCPC_RX_BUF_FRAME_TYPE		0x31
#define TCPC_RX_BUF_FRAME_TYPE_SOP	0
#define TCPC_RX_BUF_FRAME_TYPE_SOP1	1
#define TCPC_RX_HDR			0x32
#define TCPC_RX_DATA			0x34 /* through 0x4f */

#define TCPC_TRANSMIT			0x50
#define TCPC_TRANSMIT_RETRY		GENMASK(5, 4)
#define TCPC_TRANSMIT_TYPE		GENMASK(2, 0)

#define TCPC_TX_BYTE_CNT		0x51
#define TCPC_TX_HDR			0x52
#define TCPC_TX_DATA			0x54 /* through 0x6f */

#define TCPC_VBUS_VOLTAGE			0x70
#define TCPC_VBUS_VOLTAGE_MASK			0x3ff
#define TCPC_VBUS_VOLTAGE_LSB_MV		25
#define TCPC_VBUS_SINK_DISCONNECT_THRESH	0x72
#define TCPC_VBUS_SINK_DISCONNECT_THRESH_LSB_MV	25
#define TCPC_VBUS_SINK_DISCONNECT_THRESH_MAX	0x3ff
#define TCPC_VBUS_STOP_DISCHARGE_THRESH		0x74
#define TCPC_VBUS_VOLTAGE_ALARM_HI_CFG		0x76
#define TCPC_VBUS_VOLTAGE_ALARM_LO_CFG		0x78

/* I2C_WRITE_BYTE_COUNT + 1 when TX_BUF_BYTE_x is only accessible I2C_WRITE_BYTE_COUNT */
#define TCPC_TRANSMIT_BUFFER_MAX_LEN		31

#define tcpc_presenting_rd(reg, cc) \
	(!(TCPC_ROLE_CTRL_DRP & (reg)) && \
	 FIELD_GET(TCPC_ROLE_CTRL_## cc, reg) == TCPC_ROLE_CTRL_CC_RD)

struct tcpci;

/*
 * @TX_BUF_BYTE_x_hidden:
 *		optional; Set when TX_BUF_BYTE_x can only be accessed through I2C_WRITE_BYTE_COUNT.
 * @frs_sourcing_vbus:
 *		Optional; Callback to perform chip specific operations when FRS
 *		is sourcing vbus.
 * @auto_discharge_disconnect:
 *		Optional; Enables TCPC to autonomously discharge vbus on disconnect.
 * @vbus_vsafe0v:
 *		optional; Set when TCPC can detect whether vbus is at VSAFE0V.
 * @set_partner_usb_comm_capable:
 *		Optional; The USB Communications Capable bit indicates if port
 *		partner is capable of communication over the USB data lines
 *		(e.g. D+/- or SS Tx/Rx). Called to notify the status of the bit.
 * @check_contaminant:
 *		Optional; The callback is invoked when chiplevel drivers indicated
 *		that the USB port needs to be checked for contaminant presence.
 *		Chip level drivers are expected to check for contaminant and call
 *		tcpm_clean_port when the port is clean to put the port back into
 *		toggling state.
 * @cable_comm_capable
 *		optional; Set when TCPC can communicate with cable plugs over SOP'
 * @attempt_vconn_swap_discovery:
 *		Optional; The callback is called by the TCPM when the result of
 *		a Discover Identity request indicates that the port partner is
 *		a receptacle capable of modal operation. Chip level TCPCI drivers
 *		can implement their own policy to determine if and when a Vconn
 *		swap following Discover Identity on SOP' occurs.
 *		Return true when the TCPM is allowed to request a Vconn swap
 *		after Discovery Identity on SOP.
 * @set_orientation:
 *		Optional; Enable setting the connector orientation
 *		CONFIG_STANDARD_OUTPUT (0x18) bit0.
 */
struct tcpci_data {
	struct regmap *regmap;
	unsigned char TX_BUF_BYTE_x_hidden:1;
	unsigned char auto_discharge_disconnect:1;
	unsigned char vbus_vsafe0v:1;
	unsigned char cable_comm_capable:1;
	unsigned char set_orientation:1;

	int (*init)(struct tcpci *tcpci, struct tcpci_data *data);
	int (*set_vconn)(struct tcpci *tcpci, struct tcpci_data *data,
			 bool enable);
	int (*start_drp_toggling)(struct tcpci *tcpci, struct tcpci_data *data,
				  enum typec_cc_status cc);
	int (*set_vbus)(struct tcpci *tcpci, struct tcpci_data *data, bool source, bool sink);
	void (*frs_sourcing_vbus)(struct tcpci *tcpci, struct tcpci_data *data);
	void (*set_partner_usb_comm_capable)(struct tcpci *tcpci, struct tcpci_data *data,
					     bool capable);
	void (*check_contaminant)(struct tcpci *tcpci, struct tcpci_data *data);
	bool (*attempt_vconn_swap_discovery)(struct tcpci *tcpci, struct tcpci_data *data);
};

struct tcpci *tcpci_register_port(struct device *dev, struct tcpci_data *data);
void tcpci_unregister_port(struct tcpci *tcpci);
irqreturn_t tcpci_irq(struct tcpci *tcpci);

struct tcpm_port;
struct tcpm_port *tcpci_get_tcpm_port(struct tcpci *tcpci);

static inline enum typec_cc_status tcpci_to_typec_cc(unsigned int cc, bool sink)
{
	switch (cc) {
	case 0x1:
		return sink ? TYPEC_CC_RP_DEF : TYPEC_CC_RA;
	case 0x2:
		return sink ? TYPEC_CC_RP_1_5 : TYPEC_CC_RD;
	case 0x3:
		if (sink)
			return TYPEC_CC_RP_3_0;
		fallthrough;
	case TCPC_CC_STATE_SRC_OPEN:
	default:
		return TYPEC_CC_OPEN;
	}
}
#endif /* __LINUX_USB_TCPCI_H */

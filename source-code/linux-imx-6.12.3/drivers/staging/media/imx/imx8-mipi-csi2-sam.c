// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale i.MX8MN/P SoC series MIPI-CSI V3.3 receiver driver
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2019 NXP
 * Copyright 2020 NXP
 *
 * Samsung S5P/EXYNOS SoC series MIPI-CSI receiver driver
 *
 * Copyright (C) 2011 - 2013 Samsung Electronics Co., Ltd.
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2019 NXP
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>
#include <linux/reset.h>
#include <linux/version.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>

#define CSIS_DRIVER_NAME		"mxc-mipi-csi2-sam"
#define CSIS_SUBDEV_NAME		"mxc-mipi-csi2"
#define CSIS_MAX_ENTITIES		2
#define CSIS0_MAX_LANES			4
#define CSIS1_MAX_LANES			2

#define MIPI_CSIS_OF_NODE_NAME		"csi"

#define MIPI_CSIS_VC0_PAD_SINK		0
#define MIPI_CSIS_VC1_PAD_SINK		1
#define MIPI_CSIS_VC2_PAD_SINK		2
#define MIPI_CSIS_VC3_PAD_SINK		3

#define MIPI_CSIS_VC0_PAD_SOURCE	4
#define MIPI_CSIS_VC1_PAD_SOURCE	5
#define MIPI_CSIS_VC2_PAD_SOURCE	6
#define MIPI_CSIS_VC3_PAD_SOURCE	7
#define MIPI_CSIS_VCX_PADS_NUM		8


#define MIPI_CSIS_DEF_PIX_WIDTH		1920
#define MIPI_CSIS_DEF_PIX_HEIGHT	1080

/* Register map definition */

/* CSIS version */
#define MIPI_CSIS_VERSION			0x00

/* CSIS common control */
#define MIPI_CSIS_CMN_CTRL			0x04
#define MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW	(1 << 16)
#define MIPI_CSIS_CMN_CTRL_HDR_MODE		(1 << 11)
#define MIPI_CSIS_CMN_CTRL_INTER_MODE		(1 << 10)
#define MIPI_CSIS_CMN_CTRL_LANE_NR_OFFSET	8
#define MIPI_CSIS_CMN_CTRL_LANE_NR_MASK		(3 << 8)
#define MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW_CTRL	(1 << 2)
#define MIPI_CSIS_CMN_CTRL_RESET		(1 << 1)
#define MIPI_CSIS_CMN_CTRL_ENABLE		(1 << 0)

/* CSIS clock control */
#define MIPI_CSIS_CLK_CTRL			0x08
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH3(x)	(x << 28)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH2(x)	(x << 24)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH1(x)	(x << 20)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH0(x)	(x << 16)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_EN_MSK	(0xf << 4)
#define MIPI_CSIS_CLK_CTRL_WCLK_SRC		(1 << 0)

/* CSIS Interrupt mask */
#define MIPI_CSIS_INTMSK			0x10
#define MIPI_CSIS_INTMSK_EVEN_BEFORE		(1 << 31)
#define MIPI_CSIS_INTMSK_EVEN_AFTER		(1 << 30)
#define MIPI_CSIS_INTMSK_ODD_BEFORE		(1 << 29)
#define MIPI_CSIS_INTMSK_ODD_AFTER		(1 << 28)
#define MIPI_CSIS_INTMSK_FRAME_START		(1 << 24)
#define MIPI_CSIS_INTMSK_FRAME_END		(1 << 20)
#define MIPI_CSIS_INTMSK_ERR_SOT_HS		(1 << 16)
#define MIPI_CSIS_INTMSK_ERR_LOST_FS		(1 << 12)
#define MIPI_CSIS_INTMSK_ERR_LOST_FE		(1 << 8)
#define MIPI_CSIS_INTMSK_ERR_OVER		(1 << 4)
#define MIPI_CSIS_INTMSK_ERR_WRONG_CFG		(1 << 3)
#define MIPI_CSIS_INTMSK_ERR_ECC		(1 << 2)
#define MIPI_CSIS_INTMSK_ERR_CRC		(1 << 1)
#define MIPI_CSIS_INTMSK_ERR_UNKNOWN		(1 << 0)

/* CSIS Interrupt source */
#define MIPI_CSIS_INTSRC			0x14
#define MIPI_CSIS_INTSRC_EVEN_BEFORE		(1 << 31)
#define MIPI_CSIS_INTSRC_EVEN_AFTER		(1 << 30)
#define MIPI_CSIS_INTSRC_EVEN			(0x3 << 30)
#define MIPI_CSIS_INTSRC_ODD_BEFORE		(1 << 29)
#define MIPI_CSIS_INTSRC_ODD_AFTER		(1 << 28)
#define MIPI_CSIS_INTSRC_ODD			(0x3 << 28)
#define MIPI_CSIS_INTSRC_NON_IMAGE_DATA		(0xf << 28)
#define MIPI_CSIS_INTSRC_FRAME_START		(1 << 24)
#define MIPI_CSIS_INTSRC_FRAME_END		(1 << 20)
#define MIPI_CSIS_INTSRC_ERR_SOT_HS		(1 << 16)
#define MIPI_CSIS_INTSRC_ERR_LOST_FS		(1 << 12)
#define MIPI_CSIS_INTSRC_ERR_LOST_FE		(1 << 8)
#define MIPI_CSIS_INTSRC_ERR_OVER		(1 << 4)
#define MIPI_CSIS_INTSRC_ERR_WRONG_CFG		(1 << 3)
#define MIPI_CSIS_INTSRC_ERR_ECC		(1 << 2)
#define MIPI_CSIS_INTSRC_ERR_CRC		(1 << 1)
#define MIPI_CSIS_INTSRC_ERR_UNKNOWN		(1 << 0)
#define MIPI_CSIS_INTSRC_ERRORS			0xfffff

/* D-PHY status control */
#define MIPI_CSIS_DPHYSTATUS			0x20
#define MIPI_CSIS_DPHYSTATUS_ULPS_DAT		(1 << 8)
#define MIPI_CSIS_DPHYSTATUS_STOPSTATE_DAT	(1 << 4)
#define MIPI_CSIS_DPHYSTATUS_ULPS_CLK		(1 << 1)
#define MIPI_CSIS_DPHYSTATUS_STOPSTATE_CLK	(1 << 0)

/* D-PHY common control */
#define MIPI_CSIS_DPHYCTRL			0x24
#define MIPI_CSIS_DPHYCTRL_HSS_MASK		(0xff << 24)
#define MIPI_CSIS_DPHYCTRL_HSS_OFFSET		24
#define MIPI_CSIS_DPHYCTRL_SCLKS_MASK		(0x3 << 22)
#define MIPI_CSIS_DPHYCTRL_SCLKS_OFFSET		22
#define MIPI_CSIS_DPHYCTRL_DPDN_SWAP_CLK	(1 << 6)
#define MIPI_CSIS_DPHYCTRL_DPDN_SWAP_DAT	(1 << 5)
#define MIPI_CSIS_DPHYCTRL_ENABLE_DAT		(1 << 1)
#define MIPI_CSIS_DPHYCTRL_ENABLE_CLK		(1 << 0)
#define MIPI_CSIS_DPHYCTRL_ENABLE		(0x1f << 0)

/* D-PHY Master and Slave Control register Low */
#define MIPI_CSIS_DPHYBCTRL_L		0x30
/* D-PHY Master and Slave Control register High */
#define MIPI_CSIS_DPHYBCTRL_H		0x34
/* D-PHY Slave Control register Low */
#define MIPI_CSIS_DPHYSCTRL_L		0x38
/* D-PHY Slave Control register High */
#define MIPI_CSIS_DPHYSCTRL_H		0x3c


/* ISP Configuration register */
#define MIPI_CSIS_ISPCONFIG_CH0				0x40
#define MIPI_CSIS_ISPCONFIG_CH0_PIXEL_MODE_MASK		(0x3 << 12)
#define MIPI_CSIS_ISPCONFIG_CH0_PIXEL_MODE_SHIFT	12

#define MIPI_CSIS_ISPCONFIG_CH1				0x50
#define MIPI_CSIS_ISPCONFIG_CH1_PIXEL_MODE_MASK		(0x3 << 12)
#define MIPI_CSIS_ISPCONFIG_CH1_PIXEL_MODE_SHIFT	12

#define MIPI_CSIS_ISPCONFIG_CH2				0x60
#define MIPI_CSIS_ISPCONFIG_CH2_PIXEL_MODE_MASK		(0x3 << 12)
#define MIPI_CSIS_ISPCONFIG_CH2_PIXEL_MODE_SHIFT	12

#define MIPI_CSIS_ISPCONFIG_CH3				0x70
#define MIPI_CSIS_ISPCONFIG_CH3_PIXEL_MODE_MASK		(0x3 << 12)
#define MIPI_CSIS_ISPCONFIG_CH3_PIXEL_MODE_SHIFT	12

#define PIXEL_MODE_SINGLE_PIXEL_MODE			0x0
#define PIXEL_MODE_DUAL_PIXEL_MODE			0x1
#define PIXEL_MODE_QUAD_PIXEL_MODE			0x2
#define PIXEL_MODE_INVALID_PIXEL_MODE			0x3


#define MIPI_CSIS_ISPCFG_MEM_FULL_GAP_MSK	(0xff << 24)
#define MIPI_CSIS_ISPCFG_MEM_FULL_GAP(x)	(x << 24)
#define MIPI_CSIS_ISPCFG_DOUBLE_CMPNT		(1 << 12)
#define MIPI_CSIS_ISPCFG_ALIGN_32BIT		(1 << 11)
#define MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT	(0x1e << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW8		(0x2a << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW10		(0x2b << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW12		(0x2c << 2)
#define MIPI_CSIS_ISPCFG_FMT_RGB888		(0x24 << 2)
#define MIPI_CSIS_ISPCFG_FMT_RGB565		(0x22 << 2)
/* User defined formats, x = 1...4 */
#define MIPI_CSIS_ISPCFG_FMT_USER(x)		((0x30 + x - 1) << 2)
#define MIPI_CSIS_ISPCFG_FMT_MASK		(0x3f << 2)

/* ISP Image Resolution register */
#define MIPI_CSIS_ISPRESOL_CH0			0x44
#define MIPI_CSIS_ISPRESOL_CH1			0x54
#define MIPI_CSIS_ISPRESOL_CH2			0x64
#define MIPI_CSIS_ISPRESOL_CH3			0x74
#define CSIS_MAX_PIX_WIDTH			0xffff
#define CSIS_MAX_PIX_HEIGHT			0xffff

/* ISP SYNC register */
#define MIPI_CSIS_ISPSYNC_CH0			0x48
#define MIPI_CSIS_ISPSYNC_CH1			0x58
#define MIPI_CSIS_ISPSYNC_CH2			0x68
#define MIPI_CSIS_ISPSYNC_CH3			0x78

#define MIPI_CSIS_ISPSYNC_HSYNC_LINTV_OFFSET	18
#define MIPI_CSIS_ISPSYNC_VSYNC_SINTV_OFFSET 	12
#define MIPI_CSIS_ISPSYNC_VSYNC_EINTV_OFFSET	0

#define MIPI_CSIS_FRAME_COUNTER_CH0	0x0100
#define MIPI_CSIS_FRAME_COUNTER_CH1	0x0104
#define MIPI_CSIS_FRAME_COUNTER_CH2	0x0108
#define MIPI_CSIS_FRAME_COUNTER_CH3	0x010C

/* Non-image packet data buffers */
#define MIPI_CSIS_PKTDATA_ODD		0x2000
#define MIPI_CSIS_PKTDATA_EVEN		0x3000
#define MIPI_CSIS_PKTDATA_SIZE		SZ_4K

#define DEFAULT_SCLK_CSIS_FREQ		166000000UL

/* display_mix_clk_en_csr */
#define DISP_MIX_GASKET_0_CTRL			0x00
#define GASKET_0_CTRL_DATA_TYPE(x)		(((x) & (0x3F)) << 8)
#define GASKET_0_CTRL_DATA_TYPE_MASK		((0x3FUL) << (8))

#define GASKET_0_CTRL_DATA_TYPE_YUV420_8	0x18
#define GASKET_0_CTRL_DATA_TYPE_YUV420_10	0x19
#define GASKET_0_CTRL_DATA_TYPE_LE_YUV420_8	0x1a
#define GASKET_0_CTRL_DATA_TYPE_CS_YUV420_8	0x1c
#define GASKET_0_CTRL_DATA_TYPE_CS_YUV420_10	0x1d
#define GASKET_0_CTRL_DATA_TYPE_YUV422_8	0x1e
#define GASKET_0_CTRL_DATA_TYPE_YUV422_10	0x1f
#define GASKET_0_CTRL_DATA_TYPE_RGB565		0x22
#define GASKET_0_CTRL_DATA_TYPE_RGB666		0x23
#define GASKET_0_CTRL_DATA_TYPE_RGB888		0x24
#define GASKET_0_CTRL_DATA_TYPE_RAW6		0x28
#define GASKET_0_CTRL_DATA_TYPE_RAW7		0x29
#define GASKET_0_CTRL_DATA_TYPE_RAW8		0x2a
#define GASKET_0_CTRL_DATA_TYPE_RAW10		0x2b
#define GASKET_0_CTRL_DATA_TYPE_RAW12		0x2c
#define GASKET_0_CTRL_DATA_TYPE_RAW14		0x2d

#define GASKET_0_CTRL_DUAL_COMP_ENABLE		BIT(1)
#define GASKET_0_CTRL_ENABLE			BIT(0)

#define DISP_MIX_GASKET_0_HSIZE			0x04
#define DISP_MIX_GASKET_0_VSIZE			0x08

#define ISP_DEWARP_CTRL				0x138
#define ISP_DEWARP_CTRL_ISP_0_DISABLE		BIT(0)
#define ISP_DEWARP_CTRL_ISP_1_DISABLE		BIT(1)
#define ISP_DEWARP_CTRL_ISP_0_DATA_TYPE(x)	(((x) & (0x3F)) << 3)
#define ISP_DEWARP_CTRL_ISP_0_LEFT_JUST_MODE	BIT(9)
#define ISP_DEWARP_CTRL_ISP_1_DATA_TYPE(x)	(((x) & (0x3F)) << 13)
#define ISP_DEWARP_CTRL_ISP_1_LEFT_JUST_MODE	BIT(19)
#define ISP_DEWARP_CTRL_ID_MODE(x)		(((x) & (0x3)) << 23)
#define ISP_DEWARP_CTRL_DATA_TYPE_RAW6		0x28
#define ISP_DEWARP_CTRL_DATA_TYPE_RAW7		0x29
#define ISP_DEWARP_CTRL_DATA_TYPE_RAW8		0x2a
#define ISP_DEWARP_CTRL_DATA_TYPE_RAW10		0x2b
#define ISP_DEWARP_CTRL_DATA_TYPE_RAW12		0x2c
#define ISP_DEWARP_CTRL_DATA_TYPE_RAW14		0x2d
#define ISP_DEWARP_CTRL_ID_MODE_DISABLE		0x0
#define ISP_DEWARP_CTRL_ID_MODE_012		0x1
#define ISP_DEWARP_CTRL_ID_MODE_01		0x2
#define ISP_DEWARP_CTRL_ID_MODE_02		0x3

struct csi_state;
struct mipi_csis_event {
	u32 mask;
	const char * const name;
	unsigned int counter;
};

/**
 * struct csis_pix_format - CSIS pixel format description
 * @pix_width_alignment: horizontal pixel alignment, width will be
 *                       multiple of 2^pix_width_alignment
 * @code: corresponding media bus code
 * @fmt_reg: MIPI_CSIS_CONFIG register value
 * @data_alignment: MIPI-CSI data alignment in bits
 */
struct csis_pix_format {
	unsigned int pix_width_alignment;
	u32 code;
	u32 fmt_reg;
	u8 data_alignment;
};

struct csis_pktbuf {
	u32 *data;
	unsigned int len;
};

struct csis_hw_reset1 {
	struct regmap *src;
	u8 req_src;
	u8 rst_bit;
};

enum {
	VVCSIOC_RESET = 0x100,
	VVCSIOC_POWERON,
	VVCSIOC_POWEROFF,
	VVCSIOC_STREAMON,
	VVCSIOC_STREAMOFF,
	VVCSIOC_S_FMT,
	VVCSIOC_S_HDR,
};

struct csi_sam_format {
	int64_t format;
	__u32 width;
	__u32 height;
};

struct mipi_csis_rst_ops {
	int (*parse)(struct csi_state *state);
	int (*assert)(struct csi_state *state);
	int (*deassert)(struct csi_state *state);
};

struct mipi_csis_gate_clk_ops {
	int (*gclk_get)(struct csi_state *state);
	int (*gclk_enable)(struct csi_state *state);
	int (*gclk_disable)(struct csi_state *state);
};

struct mipi_csis_pdata {
	struct mipi_csis_rst_ops *rst_ops;
	struct mipi_csis_gate_clk_ops *gclk_ops;
	bool use_mix_gpr;
};

static const struct mipi_csis_event mipi_csis_events[] = {
	/* Errors */
	{ MIPI_CSIS_INTSRC_ERR_SOT_HS,	"SOT Error" },
	{ MIPI_CSIS_INTSRC_ERR_LOST_FS,	"Lost Frame Start Error" },
	{ MIPI_CSIS_INTSRC_ERR_LOST_FE,	"Lost Frame End Error" },
	{ MIPI_CSIS_INTSRC_ERR_OVER,	"FIFO Overflow Error" },
	{ MIPI_CSIS_INTSRC_ERR_ECC,	"ECC Error" },
	{ MIPI_CSIS_INTSRC_ERR_CRC,	"CRC Error" },
	{ MIPI_CSIS_INTSRC_ERR_UNKNOWN,	"Unknown Error" },
	/* Non-image data receive events */
	{ MIPI_CSIS_INTSRC_EVEN_BEFORE,	"Non-image data before even frame" },
	{ MIPI_CSIS_INTSRC_EVEN_AFTER,	"Non-image data after even frame" },
	{ MIPI_CSIS_INTSRC_ODD_BEFORE,	"Non-image data before odd frame" },
	{ MIPI_CSIS_INTSRC_ODD_AFTER,	"Non-image data after odd frame" },
	/* Frame start/end */
	{ MIPI_CSIS_INTSRC_FRAME_START,	"Frame Start" },
	{ MIPI_CSIS_INTSRC_FRAME_END,	"Frame End" },
};
#define MIPI_CSIS_NUM_EVENTS ARRAY_SIZE(mipi_csis_events)

/**
 * struct csi_state - the driver's internal state data structure
 * @lock: mutex serializing the subdev and power management operations,
 *        protecting @format and @flags members
 * @sd: v4l2_subdev associated with CSIS device instance
 * @index: the hardware instance index
 * @pdev: CSIS platform device
 * @phy: pointer to the CSIS generic PHY
 * @regs: mmaped I/O registers memory
 * @supplies: CSIS regulator supplies
 * @clock: CSIS clocks
 * @irq: requested s5p-mipi-csis irq number
 * @flags: the state variable for power and streaming control
 * @clock_frequency: device bus clock frequency
 * @hs_settle: HS-RX settle time
 * @clk_settle: Clk settle time
 * @num_lanes: number of MIPI-CSI data lanes used
 * @max_num_lanes: maximum number of MIPI-CSI data lanes supported
 * @wclk_ext: CSI wrapper clock: 0 - bus clock, 1 - external SCLK_CAM
 * @csis_fmt: current CSIS pixel format
 * @format: common media bus format for the source and sink pad
 * @slock: spinlock protecting structure members below
 * @pkt_buf: the frame embedded (non-image) data buffer
 * @events: MIPI-CSIS event (error) counters
 */
struct csi_state {
	struct v4l2_subdev	sd;
	struct mutex lock;
	struct device		*dev;
	struct v4l2_device	v4l2_dev;

	struct media_pad pads[MIPI_CSIS_VCX_PADS_NUM];

	u8 index;
	struct platform_device *pdev;
	struct phy *phy;
	void __iomem *regs;
	struct clk *mipi_clk;
	struct clk *disp_axi;
	struct clk *disp_apb;
	struct clk *csi_pclk;
	struct clk *csi_aclk;
	int irq;
	u32 flags;

	u32 clk_frequency;
	u32 hs_settle;
	u32 clk_settle;
	u32 num_lanes;
	u32 max_num_lanes;
	u8 wclk_ext;

	u8 vchannel;
	const struct csis_pix_format *csis_fmt;
	struct v4l2_mbus_framefmt format;

	spinlock_t slock;
	struct csis_pktbuf pkt_buf;
	struct mipi_csis_event events[MIPI_CSIS_NUM_EVENTS];

	struct v4l2_async_connection asd;
	struct v4l2_async_notifier  subdev_notifier;
	struct v4l2_async_subdev    *async_subdevs[2];

	struct csis_hw_reset1 hw_reset;
	struct regulator     *mipi_phy_regulator;

	struct regmap *gasket;
	struct regmap *mix_gpr;

	struct reset_control *soft_resetn;
	struct reset_control *clk_enable;
	struct reset_control *mipi_reset;
	struct reset_control *csi_rst_pclk;
	struct reset_control *csi_rst_aclk;

	struct mipi_csis_pdata const *pdata;
	bool hdr;
	u32 val;
};

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

static const struct csis_pix_format mipi_csis_formats[] = {
	{
		.code = MEDIA_BUS_FMT_YUYV8_2X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RGB888,
		.data_alignment = 24,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_VYUY8_2X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.data_alignment = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.data_alignment = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.data_alignment = 8,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.data_alignment = 8,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.data_alignment = 16,
	},
};

#define mipi_csis_write(__csis, __r, __v) writel(__v, __csis->regs + __r)
#define mipi_csis_read(__csis, __r) readl(__csis->regs + __r)

static void dump_csis_regs(struct csi_state *state, const char *label)
{
	struct {
		u32 offset;
		const char * const name;
	} registers[] = {
		{ 0x00, "CSIS_VERSION" },
		{ 0x04, "CSIS_CMN_CTRL" },
		{ 0x08, "CSIS_CLK_CTRL" },
		{ 0x10, "CSIS_INTMSK" },
		{ 0x14, "CSIS_INTSRC" },
		{ 0x20, "CSIS_DPHYSTATUS" },
		{ 0x24, "CSIS_DPHYCTRL" },
		{ 0x30, "CSIS_DPHYBCTRL_L" },
		{ 0x34, "CSIS_DPHYBCTRL_H" },
		{ 0x38, "CSIS_DPHYSCTRL_L" },
		{ 0x3C, "CSIS_DPHYSCTRL_H" },
		{ 0x40, "CSIS_ISPCONFIG_CH0" },
		{ 0x50, "CSIS_ISPCONFIG_CH1" },
		{ 0x60, "CSIS_ISPCONFIG_CH2" },
		{ 0x70, "CSIS_ISPCONFIG_CH3" },
		{ 0x44, "CSIS_ISPRESOL_CH0" },
		{ 0x54, "CSIS_ISPRESOL_CH1" },
		{ 0x64, "CSIS_ISPRESOL_CH2" },
		{ 0x74, "CSIS_ISPRESOL_CH3" },
		{ 0x48, "CSIS_ISPSYNC_CH0" },
		{ 0x58, "CSIS_ISPSYNC_CH1" },
		{ 0x68, "CSIS_ISPSYNC_CH2" },
		{ 0x78, "CSIS_ISPSYNC_CH3" },
	};
	u32 i;

	v4l2_dbg(2, debug, &state->sd, "--- %s ---\n", label);

	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		u32 cfg = mipi_csis_read(state, registers[i].offset);
		v4l2_dbg(2, debug, &state->sd, "%20s[%x]: 0x%.8x\n", registers[i].name, registers[i].offset, cfg);
	}
}

static void dump_gasket_regs(struct csi_state *state, const char *label)
{
	struct {
		u32 offset;
		const char * const name;
	} registers[] = {
		{ 0x60, "GPR_GASKET_0_CTRL" },
		{ 0x64, "GPR_GASKET_0_HSIZE" },
		{ 0x68, "GPR_GASKET_0_VSIZE" },
	};
	u32 i, cfg;

	v4l2_dbg(2, debug, &state->sd, "--- %s ---\n", label);

	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		regmap_read(state->gasket, registers[i].offset, &cfg);
		v4l2_dbg(2, debug, &state->sd, "%20s[%x]: 0x%.8x\n", registers[i].name, registers[i].offset, cfg);
	}
}

static inline struct csi_state *mipi_sd_to_csi_state(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi_state, sd);
}

static inline struct csi_state *notifier_to_mipi_dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct csi_state, subdev_notifier);
}

static struct media_pad *csis_get_remote_sensor_pad(struct csi_state *state)
{
	struct v4l2_subdev *subdev = &state->sd;
	struct media_pad *sink_pad, *source_pad;
	int i;

	while (1) {
		source_pad = NULL;
		for (i = 0; i < subdev->entity.num_pads; i++) {
			sink_pad = &subdev->entity.pads[i];

			if (sink_pad->flags & MEDIA_PAD_FL_SINK) {
				source_pad = media_pad_remote_pad_first(sink_pad);
				if (source_pad)
					break;
			}
		}
		/* return first pad point in the loop  */
		return source_pad;
	}

	if (i == subdev->entity.num_pads)
		v4l2_err(&state->sd, "%s, No remote pad found!\n", __func__);

	return NULL;
}

static struct v4l2_subdev *csis_get_remote_subdev(struct csi_state *state,
						  const char * const label)
{
	struct media_pad *source_pad;
	struct v4l2_subdev *sen_sd;

	/* Get remote source pad */
	source_pad = csis_get_remote_sensor_pad(state);
	if (!source_pad) {
		v4l2_err(&state->sd, "%s, No remote pad found!\n", label);
		return NULL;
	}

	/* Get remote source pad subdev */
	sen_sd = media_entity_to_v4l2_subdev(source_pad->entity);
	if (!sen_sd) {
		v4l2_err(&state->sd, "%s, No remote subdev found!\n", label);
		return NULL;
	}

	return sen_sd;
}

static const struct csis_pix_format *find_csis_format(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mipi_csis_formats); i++)
		if (code == mipi_csis_formats[i].code)
			return &mipi_csis_formats[i];
	return NULL;
}

static void mipi_csis_clean_irq(struct csi_state *state)
{
	u32 status;

	status = mipi_csis_read(state, MIPI_CSIS_INTSRC);
	mipi_csis_write(state, MIPI_CSIS_INTSRC, status);

	status = mipi_csis_read(state, MIPI_CSIS_INTMSK);
	mipi_csis_write(state, MIPI_CSIS_INTMSK, status);
}

static void mipi_csis_enable_interrupts(struct csi_state *state, bool on)
{
	u32 val;

	mipi_csis_clean_irq(state);

	val = mipi_csis_read(state, MIPI_CSIS_INTMSK);
	if (on)
		val |= 0x0FFFFF1F;
	else
		val &= ~0x0FFFFF1F;
	mipi_csis_write(state, MIPI_CSIS_INTMSK, val);
}

static void mipi_csis_sw_reset(struct csi_state *state)
{
	u32 val;

	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	val |= MIPI_CSIS_CMN_CTRL_RESET;
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val);

	udelay(20);
}

static int mipi_csis_phy_init(struct csi_state *state)
{
	state->mipi_phy_regulator = devm_regulator_get(state->dev, "mipi-phy");
	if (IS_ERR(state->mipi_phy_regulator)) {
		dev_err(state->dev, "Fail to get mipi-phy regulator\n");
		return PTR_ERR(state->mipi_phy_regulator);
	}

	regulator_set_voltage(state->mipi_phy_regulator, 1000000, 1000000);
	return 0;
}

static void mipi_csis_system_enable(struct csi_state *state, int on)
{
	u32 val, mask;

	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	if (on)
		val |= MIPI_CSIS_CMN_CTRL_ENABLE;
	else
		val &= ~MIPI_CSIS_CMN_CTRL_ENABLE;
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val);

	val = mipi_csis_read(state, MIPI_CSIS_DPHYCTRL);
	val &= ~MIPI_CSIS_DPHYCTRL_ENABLE;
	if (on) {
		mask = (1 << (state->num_lanes + 1)) - 1;
		val |= (mask & MIPI_CSIS_DPHYCTRL_ENABLE);
	}
	mipi_csis_write(state, MIPI_CSIS_DPHYCTRL, val);
}

/* Called with the state.lock mutex held */
static void __mipi_csis_set_format(struct csi_state *state)
{
	struct v4l2_mbus_framefmt *mf = &state->format;
	u32 val;

	v4l2_dbg(1, debug, &state->sd, "fmt: %#x, %d x %d\n",
		 mf->code, mf->width, mf->height);

	/* Color format */
	val = mipi_csis_read(state, MIPI_CSIS_ISPCONFIG_CH0);
	val &= ~MIPI_CSIS_ISPCFG_FMT_MASK;
	val |= state->csis_fmt->fmt_reg;
	mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH0, val);

	val = mipi_csis_read(state, MIPI_CSIS_ISPCONFIG_CH0);
	val &= ~MIPI_CSIS_ISPCONFIG_CH0_PIXEL_MODE_MASK;
	if (state->csis_fmt->fmt_reg == MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT)
		val |= (PIXEL_MODE_DUAL_PIXEL_MODE <<
			MIPI_CSIS_ISPCONFIG_CH0_PIXEL_MODE_SHIFT);
	mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH0, val);

	/* Pixel resolution */
	val = mf->width | (mf->height << 16);
	mipi_csis_write(state, MIPI_CSIS_ISPRESOL_CH0, val);

	if (state->hdr) {
		mipi_csis_write(state, MIPI_CSIS_ISPRESOL_CH1, val);
		mipi_csis_write(state, MIPI_CSIS_ISPRESOL_CH2, val);
		mipi_csis_write(state, MIPI_CSIS_ISPRESOL_CH3, val);
		val = state->csis_fmt->fmt_reg;
		mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH1, val | 1);
		mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH2, val | 2);
		mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH3, val | 3);
	}
}

static void mipi_csis_set_hsync_settle(struct csi_state *state)
{
	u32 val;

	val = mipi_csis_read(state, MIPI_CSIS_DPHYCTRL);
	val &= ~MIPI_CSIS_DPHYCTRL_HSS_MASK;
	val |= (state->hs_settle << 24) | (state->clk_settle << 22);
	mipi_csis_write(state, MIPI_CSIS_DPHYCTRL, val);
}

static void mipi_csis_set_params(struct csi_state *state)
{
	u32 val;

	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	val &= ~MIPI_CSIS_CMN_CTRL_LANE_NR_MASK;
	val |= (state->num_lanes - 1) << MIPI_CSIS_CMN_CTRL_LANE_NR_OFFSET;
	val |= MIPI_CSIS_CMN_CTRL_HDR_MODE;
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val);

	__mipi_csis_set_format(state);
	mipi_csis_set_hsync_settle(state);

	val = mipi_csis_read(state, MIPI_CSIS_ISPCONFIG_CH0);
	if (state->csis_fmt->data_alignment == 32)
		val |= MIPI_CSIS_ISPCFG_ALIGN_32BIT;
	else /* Normal output */
		val &= ~MIPI_CSIS_ISPCFG_ALIGN_32BIT;
	mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH0, val);

	val = (0 << MIPI_CSIS_ISPSYNC_HSYNC_LINTV_OFFSET) |
	      (0 << MIPI_CSIS_ISPSYNC_VSYNC_SINTV_OFFSET) |
	      (0 << MIPI_CSIS_ISPSYNC_VSYNC_EINTV_OFFSET);
	mipi_csis_write(state, MIPI_CSIS_ISPSYNC_CH0, val);

	val = mipi_csis_read(state, MIPI_CSIS_CLK_CTRL);
	val &= ~MIPI_CSIS_CLK_CTRL_WCLK_SRC;
	if (state->wclk_ext)
		val |= MIPI_CSIS_CLK_CTRL_WCLK_SRC;
	val |= MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH0(15);
	val &= ~MIPI_CSIS_CLK_CTRL_CLKGATE_EN_MSK;
	mipi_csis_write(state, MIPI_CSIS_CLK_CTRL, val);

	mipi_csis_write(state, MIPI_CSIS_DPHYBCTRL_L, 0x1f4);
	mipi_csis_write(state, MIPI_CSIS_DPHYBCTRL_H, 0);

	/* Update the shadow register. */
	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	val |= (MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW |
		MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW_CTRL);
	if (state->hdr) {
		val |= MIPI_CSIS_CMN_CTRL_HDR_MODE;
		val |= 0xE0000;
	}
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val);
}

static int mipi_csis_clk_enable(struct csi_state *state)
{
	struct device *dev = state->dev;
	int ret;

	ret = clk_prepare_enable(state->mipi_clk);
	if (ret) {
		dev_err(dev, "enable mipi_clk failed!\n");
		return ret;
	}

	ret = clk_prepare_enable(state->disp_axi);
	if (ret) {
		dev_err(dev, "enable disp_axi clk failed!\n");
		return ret;
	}

	ret = clk_prepare_enable(state->disp_apb);
	if (ret) {
		dev_err(dev, "enable disp_apb clk failed!\n");
		return ret;
	}

	return 0;
}

static void mipi_csis_clk_disable(struct csi_state *state)
{
	clk_disable_unprepare(state->mipi_clk);
	clk_disable_unprepare(state->disp_axi);
	clk_disable_unprepare(state->disp_apb);
}

static int mipi_csis_clk_get(struct csi_state *state)
{
	struct device *dev = &state->pdev->dev;
	int ret = true;

	state->mipi_clk = devm_clk_get(dev, "mipi_clk");
	if (IS_ERR(state->mipi_clk)) {
		dev_err(dev, "Could not get mipi csi clock\n");
		return -ENODEV;
	}

	state->disp_axi = devm_clk_get(dev, "disp_axi");
	if (IS_ERR(state->disp_axi)) {
		dev_warn(dev, "Could not get disp_axi clock\n");
		return -ENODEV;
	}

	state->disp_apb = devm_clk_get(dev, "disp_apb");
	if (IS_ERR(state->disp_apb)) {
		dev_warn(dev, "Could not get disp apb clock\n");
		return -ENODEV;
	}

	/* Set clock rate */
	if (state->clk_frequency) {
		ret = clk_set_rate(state->mipi_clk, state->clk_frequency);
		if (ret < 0) {
			dev_err(dev, "set rate filed, rate=%d\n", state->clk_frequency);
			return -EINVAL;
		}
	} else {
		dev_WARN(dev, "No clock frequency specified!\n");
	}

	return 0;
}

static int disp_mix_sft_parse_resets(struct csi_state *state)
{
	struct mipi_csis_pdata const *pdata = state->pdata;

	if (!pdata->rst_ops || !pdata->rst_ops->parse)
		return -EINVAL;

	return pdata->rst_ops->parse(state);
}

static int disp_mix_sft_rstn(struct csi_state *state, bool enable)
{
	struct mipi_csis_pdata const *pdata = state->pdata;
	int ret;

	if (!pdata->rst_ops ||
	    !pdata->rst_ops->assert ||
	    !pdata->rst_ops->deassert)
		return -EINVAL;

	ret = enable ? pdata->rst_ops->assert(state) :
		       pdata->rst_ops->deassert(state);
	return ret;
}

static int disp_mix_clks_get(struct csi_state *state)
{
	struct mipi_csis_pdata const *pdata = state->pdata;

	if (!pdata->gclk_ops || !pdata->gclk_ops->gclk_get)
		return -EINVAL;

	return pdata->gclk_ops->gclk_get(state);
}

static int disp_mix_clks_enable(struct csi_state *state, bool enable)
{
	struct mipi_csis_pdata const *pdata = state->pdata;
	int ret;

	if (!pdata->gclk_ops ||
	    !pdata->gclk_ops->gclk_enable ||
	    !pdata->gclk_ops->gclk_disable)
		return -EINVAL;

	ret = enable ? pdata->gclk_ops->gclk_enable(state) :
		       pdata->gclk_ops->gclk_disable(state);
	return ret;
}

static void disp_mix_gasket_config(struct csi_state *state)
{
	struct regmap *gasket = state->gasket;
	struct csis_pix_format const *fmt = state->csis_fmt;
	struct v4l2_mbus_framefmt *mf = &state->format;
	s32 fmt_val = -EINVAL;
	u32 val;

	switch (fmt->code) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RGB888;
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_2X8:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_YUV422_8;
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW8;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW8;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW8;
		break;
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW8;
		break;

	case MEDIA_BUS_FMT_SBGGR10_1X10:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW10;
		break;
	case MEDIA_BUS_FMT_SGBRG10_1X10:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW10;
		break;
	case MEDIA_BUS_FMT_SGRBG10_1X10:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW10;
		break;
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW10;
		break;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW12;
		break;
	case MEDIA_BUS_FMT_SGBRG12_1X12:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW12;
		break;
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW12;
		break;
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		fmt_val = GASKET_0_CTRL_DATA_TYPE_RAW12;
		break;
	default:
		pr_err("gasket not support format %d\n", fmt->code);
		return;
	}

	regmap_read(gasket, DISP_MIX_GASKET_0_CTRL, &val);
	if (fmt_val == GASKET_0_CTRL_DATA_TYPE_YUV422_8)
		val |= GASKET_0_CTRL_DUAL_COMP_ENABLE;
	val |= GASKET_0_CTRL_DATA_TYPE(fmt_val);
	regmap_write(gasket, DISP_MIX_GASKET_0_CTRL, val);

	if (WARN_ON(!mf->width || !mf->height))
		return;

	regmap_write(gasket, DISP_MIX_GASKET_0_HSIZE, mf->width);
	regmap_write(gasket, DISP_MIX_GASKET_0_VSIZE, mf->height);
}

static void disp_mix_gasket_enable(struct csi_state *state, bool enable)
{
	struct regmap *gasket = state->gasket;

	if (enable)
		regmap_update_bits(gasket, DISP_MIX_GASKET_0_CTRL,
					GASKET_0_CTRL_ENABLE,
					GASKET_0_CTRL_ENABLE);
	else
		regmap_update_bits(gasket, DISP_MIX_GASKET_0_CTRL,
					GASKET_0_CTRL_ENABLE,
					0);
}

static void mipi_csis_start_stream(struct csi_state *state)
{
	mipi_csis_sw_reset(state);

	disp_mix_gasket_config(state);
	mipi_csis_set_params(state);

	mipi_csis_system_enable(state, true);
	disp_mix_gasket_enable(state, true);
	mipi_csis_enable_interrupts(state, true);

	msleep(5);
}

static void mipi_csis_stop_stream(struct csi_state *state)
{
	mipi_csis_enable_interrupts(state, false);
	mipi_csis_system_enable(state, false);
	disp_mix_gasket_enable(state, false);
}

static void mipi_csis_clear_counters(struct csi_state *state)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&state->slock, flags);
	for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++)
		state->events[i].counter = 0;
	spin_unlock_irqrestore(&state->slock, flags);
}

static void mipi_csis_log_counters(struct csi_state *state, bool non_errors)
{
	int i = non_errors ? MIPI_CSIS_NUM_EVENTS : MIPI_CSIS_NUM_EVENTS - 4;
	unsigned long flags;

	spin_lock_irqsave(&state->slock, flags);

	for (i--; i >= 0; i--) {
		if (state->events[i].counter > 0 || debug)
			v4l2_info(&state->sd, "%s events: %d\n",
				  state->events[i].name,
				  state->events[i].counter);
	}
	spin_unlock_irqrestore(&state->slock, flags);
}

static int mipi_csi2_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations mipi_csi2_sd_media_ops = {
	.link_setup = mipi_csi2_link_setup,
};

/*
 * V4L2 subdev operations
 */
static int mipi_csis_s_power(struct v4l2_subdev *mipi_sd, int on)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sen_sd;

	/* Get remote source pad subdev */
	sen_sd = csis_get_remote_subdev(state, __func__);
	if (!sen_sd) {
		v4l2_err(&state->sd, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	return v4l2_subdev_call(sen_sd, core, s_power, on);
}

static int mipi_csis_s_stream(struct v4l2_subdev *mipi_sd, int enable)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);

	v4l2_dbg(1, debug, mipi_sd, "%s: %d, state: 0x%x\n",
		 __func__, enable, state->flags);

	if (enable) {
		pm_runtime_get_sync(state->dev);
		mipi_csis_clear_counters(state);
		mipi_csis_start_stream(state);
		dump_csis_regs(state, __func__);
		dump_gasket_regs(state, __func__);
	} else {
		mipi_csis_stop_stream(state);
		if (debug > 0)
			mipi_csis_log_counters(state, true);
		pm_runtime_put(state->dev);
	}

	return 0;
}

static int mipi_csis_set_fmt(struct v4l2_subdev *mipi_sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *format)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct csis_pix_format const *csis_fmt;
	struct media_pad *source_pad;
	struct v4l2_subdev *sen_sd;
	int ret;

	/* Get remote source pad */
	source_pad = csis_get_remote_sensor_pad(state);
	if (!source_pad) {
		v4l2_err(&state->sd, "%s, No remote pad found!\n", __func__);
		return -EINVAL;
	}

	/* Get remote source pad subdev */
	sen_sd = csis_get_remote_subdev(state, __func__);
	if (!sen_sd) {
		v4l2_err(&state->sd, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	format->pad = source_pad->index;
	ret = v4l2_subdev_call(sen_sd, pad, set_fmt, NULL, format);
	if (ret < 0) {
		v4l2_err(&state->sd, "%s, set sensor format fail\n", __func__);
		return -EINVAL;
	}

	csis_fmt = find_csis_format(mf->code);
	if (!csis_fmt) {
		csis_fmt = &mipi_csis_formats[0];
		mf->code = csis_fmt->code;
	}

	state->csis_fmt = csis_fmt;

	return 0;
}

static int mipi_csis_get_fmt(struct v4l2_subdev *mipi_sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *format)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_mbus_framefmt *mf = &state->format;
	struct media_pad *source_pad;
	struct v4l2_subdev *sen_sd;
	int ret;

	/* Get remote source pad */
	source_pad = csis_get_remote_sensor_pad(state);
	if (!source_pad) {
		v4l2_err(&state->sd, "%s, No remote pad found!\n", __func__);
		return -EINVAL;
	}

	/* Get remote source pad subdev */
	sen_sd = csis_get_remote_subdev(state, __func__);
	if (!sen_sd) {
		v4l2_err(&state->sd, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	format->pad = source_pad->index;
	ret = v4l2_subdev_call(sen_sd, pad, get_fmt, NULL, format);
	if (ret < 0) {
		v4l2_err(&state->sd, "%s, call get_fmt of subdev failed!\n", __func__);
		return ret;
	}

	memcpy(mf, &format->format, sizeof(struct v4l2_mbus_framefmt));
	return 0;
}

static int mipi_csis_s_rx_buffer(struct v4l2_subdev *mipi_sd, void *buf,
			       unsigned int *size)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	unsigned long flags;

	*size = min_t(unsigned int, *size, MIPI_CSIS_PKTDATA_SIZE);

	spin_lock_irqsave(&state->slock, flags);
	state->pkt_buf.data = buf;
	state->pkt_buf.len = *size;
	spin_unlock_irqrestore(&state->slock, flags);

	return 0;
}

static int mipi_csis_set_frame_interval(struct v4l2_subdev *mipi_sd,
					struct v4l2_subdev_state *sd_state,
					struct v4l2_subdev_frame_interval *interval)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sen_sd;

	/* Get remote source pad subdev */
	sen_sd = csis_get_remote_subdev(state, __func__);
	if (!sen_sd) {
		v4l2_err(&state->sd, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	return v4l2_subdev_call(sen_sd, pad, set_frame_interval, sd_state, interval);
}

static int mipi_csis_get_frame_interval(struct v4l2_subdev *mipi_sd,
					struct v4l2_subdev_state *sd_state,
					struct v4l2_subdev_frame_interval *interval)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sen_sd;

	/* Get remote source pad subdev */
	sen_sd = csis_get_remote_subdev(state, __func__);
	if (!sen_sd) {
		v4l2_err(&state->sd, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	return v4l2_subdev_call(sen_sd, pad, get_frame_interval, sd_state, interval);
}

static int mipi_csis_enum_framesizes(struct v4l2_subdev *mipi_sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_frame_size_enum *fse)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sen_sd;

	/* Get remote source pad subdev */
	sen_sd = csis_get_remote_subdev(state, __func__);
	if (!sen_sd) {
		v4l2_err(&state->sd, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	return v4l2_subdev_call(sen_sd, pad, enum_frame_size, NULL, fse);
}

static int mipi_csis_enum_frameintervals(struct v4l2_subdev *mipi_sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sen_sd;

	/* Get remote source pad subdev */
	sen_sd = csis_get_remote_subdev(state, __func__);
	if (!sen_sd) {
		v4l2_err(&state->sd, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	return v4l2_subdev_call(sen_sd, pad, enum_frame_interval, NULL, fie);
}

static int mipi_csis_log_status(struct v4l2_subdev *mipi_sd)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);

	mutex_lock(&state->lock);
	mipi_csis_log_counters(state, true);
	if (debug) {
		dump_csis_regs(state, __func__);
		dump_gasket_regs(state, __func__);
	}
	mutex_unlock(&state->lock);
	return 0;
}

static int csis_s_fmt(struct v4l2_subdev *sd, struct csi_sam_format *fmt)
{
	u32 code;
	const struct csis_pix_format *csis_format;
	struct csi_state *state = container_of(sd, struct csi_state, sd);

	switch (fmt->format) {
	case V4L2_PIX_FMT_SBGGR8:
	    code = MEDIA_BUS_FMT_SBGGR8_1X8;
	    break;
	case V4L2_PIX_FMT_SGBRG8:
	    code = MEDIA_BUS_FMT_SGBRG8_1X8;
	    break;
	case V4L2_PIX_FMT_SGRBG8:
	    code = MEDIA_BUS_FMT_SGRBG8_1X8;
	    break;
	case V4L2_PIX_FMT_SRGGB8:
	    code = MEDIA_BUS_FMT_SRGGB8_1X8;
	    break;
	case V4L2_PIX_FMT_SBGGR10:
	    code = MEDIA_BUS_FMT_SBGGR10_1X10;
	    break;
	case V4L2_PIX_FMT_SGBRG10:
	    code = MEDIA_BUS_FMT_SGBRG10_1X10;
	    break;
	case V4L2_PIX_FMT_SGRBG10:
	    code = MEDIA_BUS_FMT_SGRBG10_1X10;
	    break;
	case V4L2_PIX_FMT_SRGGB10:
	    code = MEDIA_BUS_FMT_SRGGB10_1X10;
	    break;
	case V4L2_PIX_FMT_SBGGR12:
	    code = MEDIA_BUS_FMT_SBGGR12_1X12;
	    break;
	case V4L2_PIX_FMT_SGBRG12:
	    code = MEDIA_BUS_FMT_SGBRG12_1X12;
	    break;
	case V4L2_PIX_FMT_SGRBG12:
	    code = MEDIA_BUS_FMT_SGRBG12_1X12;
	    break;
	case V4L2_PIX_FMT_SRGGB12:
	    code = MEDIA_BUS_FMT_SRGGB12_1X12;
	    break;
	default:
		return -EINVAL;
	}
	csis_format = find_csis_format(code);
	if (csis_format == NULL)
		return -EINVAL;

	state->csis_fmt = csis_format;
	state->format.width = fmt->width;
	state->format.height = fmt->height;
	state->format.code = csis_format->code;

	disp_mix_gasket_config(state);
	mipi_csis_set_params(state);
	return 0;
}

static int csis_s_hdr(struct v4l2_subdev *sd, bool enable)
{
	struct csi_state *state = container_of(sd, struct csi_state, sd);

	v4l2_dbg(2, debug, &state->sd, "%s: %d\n", __func__, enable);
	state->hdr = enable;
	return 0;
}

static int csis_ioc_qcap(struct v4l2_subdev *dev, void *args)
{
	struct csi_state *state = mipi_sd_to_csi_state(dev);
	struct v4l2_capability *cap = (struct v4l2_capability *)args;
	strscpy((char *)cap->driver, "csi_sam_subdev", sizeof(cap->driver));
	cap->bus_info[0] = state->index;
	return 0;
}

#ifdef CONFIG_HARDENED_USERCOPY
#define USER_TO_KERNEL(TYPE) \
	({\
		TYPE tmp; \
		arg = (void *)(&tmp); \
		copy_from_user(arg, arg_user, sizeof(TYPE));\
	})

#define KERNEL_TO_USER(TYPE) \
		copy_to_user(arg_user, arg, sizeof(TYPE));
#else
#define USER_TO_KERNEL(TYPE)
#define KERNEL_TO_USER(TYPE)
#endif
static long csis_priv_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg_user)
{
	int ret = 1;
	struct csi_state *state = container_of(sd, struct csi_state, sd);
	void *arg = arg_user;

	pm_runtime_get_sync(state->dev);

	switch (cmd) {
	case VVCSIOC_RESET:
		mipi_csis_sw_reset(state);
		ret = 0;
		break;
	case VVCSIOC_POWERON:
		ret = mipi_csis_s_power(sd, 1);
		break;
	case VVCSIOC_POWEROFF:
		ret = mipi_csis_s_power(sd, 0);
		break;
	case VVCSIOC_STREAMON:
		ret = mipi_csis_s_stream(sd, 1);
		break;
	case VVCSIOC_STREAMOFF:
		ret = mipi_csis_s_stream(sd, 0);
		break;
	case VVCSIOC_S_FMT: {
		USER_TO_KERNEL(struct csi_sam_format);
		ret = csis_s_fmt(sd, (struct csi_sam_format *)arg);
		break;
	}
	case VVCSIOC_S_HDR: {
		USER_TO_KERNEL(bool);
		ret = csis_s_hdr(sd, *(bool *) arg);
		break;
	}
	case VIDIOC_QUERYCAP:
		ret = csis_ioc_qcap(sd, arg);
		break;
	default:
		v4l2_err(&state->sd, "unsupported csi-sam command %d.", cmd);
		ret = -EINVAL;
		break;
	}
	pm_runtime_put(state->dev);

	return ret;
}


static struct v4l2_subdev_core_ops mipi_csis_core_ops = {
	.s_power = mipi_csis_s_power,
	.log_status = mipi_csis_log_status,
	.ioctl = csis_priv_ioctl,
};

static struct v4l2_subdev_video_ops mipi_csis_video_ops = {
	.s_rx_buffer = mipi_csis_s_rx_buffer,
	.s_stream = mipi_csis_s_stream,
};

static const struct v4l2_subdev_pad_ops mipi_csis_pad_ops = {
	.enum_frame_size       = mipi_csis_enum_framesizes,
	.enum_frame_interval   = mipi_csis_enum_frameintervals,
	.get_fmt               = mipi_csis_get_fmt,
	.set_fmt               = mipi_csis_set_fmt,
	.get_frame_interval    = mipi_csis_get_frame_interval,
	.set_frame_interval    = mipi_csis_set_frame_interval,
};

static struct v4l2_subdev_ops mipi_csis_subdev_ops = {
	.core = &mipi_csis_core_ops,
	.video = &mipi_csis_video_ops,
	.pad = &mipi_csis_pad_ops,
};

static irqreturn_t mipi_csis_irq_handler(int irq, void *dev_id)
{
	struct csi_state *state = dev_id;
	struct csis_pktbuf *pktbuf = &state->pkt_buf;
	unsigned long flags;
	u32 status;

	status = mipi_csis_read(state, MIPI_CSIS_INTSRC);

	spin_lock_irqsave(&state->slock, flags);
	if ((status & MIPI_CSIS_INTSRC_NON_IMAGE_DATA) && pktbuf->data) {
		u32 offset;

		if (status & MIPI_CSIS_INTSRC_EVEN)
			offset = MIPI_CSIS_PKTDATA_EVEN;
		else
			offset = MIPI_CSIS_PKTDATA_ODD;

		memcpy(pktbuf->data, state->regs + offset, pktbuf->len);
		pktbuf->data = NULL;
		rmb();
	}

	/* Update the event/error counters */
	if ((status & MIPI_CSIS_INTSRC_ERRORS) || debug) {
		int i;
		for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++) {
			if (!(status & state->events[i].mask))
				continue;
			state->events[i].counter++;
			v4l2_dbg(2, debug, &state->sd, "%s: %d\n",
				 state->events[i].name,
				 state->events[i].counter);
		}
		v4l2_dbg(2, debug, &state->sd, "status: %08x\n", status);
	}
	spin_unlock_irqrestore(&state->slock, flags);

	mipi_csis_write(state, MIPI_CSIS_INTSRC, status);
	return IRQ_HANDLED;
}

static int mipi_csis_parse_dt(struct platform_device *pdev,
			    struct csi_state *state)
{
	struct device_node *node = pdev->dev.of_node;

	state->index = of_alias_get_id(node, "csi");

	if (of_property_read_u32(node, "clock-frequency", &state->clk_frequency))
		state->clk_frequency = DEFAULT_SCLK_CSIS_FREQ;

	if (of_property_read_u32(node, "bus-width", &state->max_num_lanes))
		return -EINVAL;

	node = of_graph_get_next_endpoint(node, NULL);
	if (!node) {
		dev_err(&pdev->dev, "No port node\n");
		return -EINVAL;
	}

	/* Get MIPI CSI-2 bus configration from the endpoint node. */
	of_property_read_u32(node, "csis-hs-settle", &state->hs_settle);
	of_property_read_u32(node, "csis-clk-settle", &state->clk_settle);
	of_property_read_u32(node, "data-lanes", &state->num_lanes);

	state->wclk_ext = of_property_read_bool(node, "csis-wclk");

	of_node_put(node);
	return 0;
}

static const struct of_device_id mipi_csis_of_match[];

/* init subdev */
static int mipi_csis_subdev_init(struct v4l2_subdev *mipi_sd,
		struct platform_device *pdev,
		const struct v4l2_subdev_ops *ops)
{
	struct csi_state *state = platform_get_drvdata(pdev);
	int ret = 0;

	v4l2_subdev_init(mipi_sd, ops);
	mipi_sd->owner = THIS_MODULE;
	snprintf(mipi_sd->name, sizeof(mipi_sd->name), "%s.%d",
		 CSIS_SUBDEV_NAME, state->index);
	mipi_sd->entity.function = MEDIA_ENT_F_IO_V4L;
	mipi_sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	mipi_sd->dev = &pdev->dev;

	state->csis_fmt      = &mipi_csis_formats[0];
	state->format.code   = mipi_csis_formats[0].code;
	state->format.width  = MIPI_CSIS_DEF_PIX_WIDTH;
	state->format.height = MIPI_CSIS_DEF_PIX_HEIGHT;

	/* This allows to retrieve the platform device id by the host driver */
	v4l2_set_subdevdata(mipi_sd, state);

	return ret;
}

/*
 * IMX8MP platform data
 */
static int mipi_csis_imx8mp_parse_resets(struct csi_state *state)
{
	struct device *dev = state->dev;
	struct reset_control *reset;

	reset = devm_reset_control_get_optional(dev, "csi_rst_pclk");
	if (IS_ERR(reset)) {
		if (PTR_ERR(reset) != -EPROBE_DEFER)
			dev_err(dev, "Failed to get csi pclk reset control\n");
		return PTR_ERR(reset);
	}
	state->csi_rst_pclk = reset;

	reset = devm_reset_control_get_optional(dev, "csi_rst_aclk");
	if (IS_ERR(reset)) {
		if (PTR_ERR(reset) != -EPROBE_DEFER)
			dev_err(dev, "Failed to get csi aclk reset control\n");
		return PTR_ERR(reset);
	}
	state->csi_rst_aclk = reset;

	return 0;
}

static int mipi_csis_imx8mp_resets_assert(struct csi_state *state)
{
	struct device *dev = state->dev;
	int ret;

	if (!state->csi_rst_pclk || !state->csi_rst_aclk)
		return 0;

	ret = reset_control_assert(state->csi_rst_pclk);
	if (ret) {
		dev_err(dev, "Failed to assert csi pclk reset control\n");
		return ret;
	}

	ret = reset_control_assert(state->csi_rst_aclk);
	if (ret) {
		dev_err(dev, "Failed to assert csi aclk reset control\n");
		return ret;
	}

	return ret;
}

static int mipi_csis_imx8mp_resets_deassert(struct csi_state *state)
{
	if (!state->csi_rst_pclk || !state->csi_rst_aclk)
		return 0;

	reset_control_deassert(state->csi_rst_pclk);
	reset_control_deassert(state->csi_rst_aclk);

	return 0;
}

static struct mipi_csis_rst_ops imx8mp_rst_ops = {
	.parse  = mipi_csis_imx8mp_parse_resets,
	.assert = mipi_csis_imx8mp_resets_assert,
	.deassert = mipi_csis_imx8mp_resets_deassert,
};

static int mipi_csis_imx8mp_gclk_get(struct csi_state *state)
{
	struct device *dev = state->dev;

	state->csi_pclk = devm_clk_get_optional(dev, "media_blk_csi_pclk");
	if (IS_ERR(state->csi_pclk)) {
		if (PTR_ERR(state->csi_pclk) != -EPROBE_DEFER)
			dev_err(dev, "Failed to get media csi pclk\n");
		return PTR_ERR(state->csi_pclk);
	}

	state->csi_aclk = devm_clk_get_optional(dev, "media_blk_csi_aclk");
	if (IS_ERR(state->csi_aclk)) {
		if (PTR_ERR(state->csi_aclk) != -EPROBE_DEFER)
			dev_err(dev, "Failed to get media csi aclk\n");
		return PTR_ERR(state->csi_aclk);
	}

	return 0;
}

static int mipi_csis_imx8mp_gclk_enable(struct csi_state *state)
{
	struct device *dev = state->dev;
	int ret;

	ret = clk_prepare_enable(state->csi_pclk);
	if (ret) {
		dev_err(dev, "enable csi_pclk failed!\n");
		return ret;
	}

	ret = clk_prepare_enable(state->csi_aclk);
	if (ret) {
		dev_err(dev, "enable csi_aclk failed!\n");
		return ret;
	}

	return ret;
}

static int mipi_csis_imx8mp_gclk_disable(struct csi_state *state)
{
	clk_disable_unprepare(state->csi_pclk);
	clk_disable_unprepare(state->csi_aclk);
	return 0;
}

static struct mipi_csis_gate_clk_ops imx8mp_gclk_ops = {
	.gclk_get = mipi_csis_imx8mp_gclk_get,
	.gclk_enable  = mipi_csis_imx8mp_gclk_enable,
	.gclk_disable = mipi_csis_imx8mp_gclk_disable,
};

static void mipi_csis_imx8mp_dewarp_ctl_data_type(struct csi_state *state,
		int bus_width)
{
	switch (state->index) {
	case 0:
		state->val = ISP_DEWARP_CTRL_ISP_0_DATA_TYPE(bus_width);
		break;
	case 1:
		state->val = ISP_DEWARP_CTRL_ISP_1_DATA_TYPE(bus_width);
		break;
	default:
		v4l2_err(&state->sd, "%s :csi subdev index err!\n", __func__);
		break;
	}
}

static void mipi_csis_imx8mp_dewarp_ctl_left_just_mode(struct csi_state *state)
{
	switch (state->index) {
	case 0:
		state->val |= ISP_DEWARP_CTRL_ISP_0_LEFT_JUST_MODE;
		break;
	case 1:
		state->val |= ISP_DEWARP_CTRL_ISP_1_LEFT_JUST_MODE;
		break;
	default:
		v4l2_err(&state->sd, "%s :csi subdev index err!\n", __func__);
		break;
	}
}

static void mipi_csis_imx8mp_phy_reset(struct csi_state *state)
{
	int ret = 0;
	struct v4l2_subdev *sen_sd;
	struct reset_control *reset = state->mipi_reset;

	struct v4l2_subdev_mbus_code_enum code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	reset_control_assert(reset);
	usleep_range(10, 20);

	reset_control_deassert(reset);
	usleep_range(10, 20);

	sen_sd = csis_get_remote_subdev(state, __func__);
	if (!sen_sd)
		goto csi_phy_initial_cfg;

	ret = v4l2_subdev_call(sen_sd, pad, enum_mbus_code, NULL, &code);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		v4l2_err(&state->sd, "enum_mbus_code error !!!\n");
		return;
	} else if (ret == -ENOIOCTLCMD)
		goto csi_phy_initial_cfg;

	/* temporary place */
	if (state->mix_gpr) {
		if ((code.code == MEDIA_BUS_FMT_SRGGB8_1X8) ||
				(code.code == MEDIA_BUS_FMT_SGRBG8_1X8) ||
				(code.code == MEDIA_BUS_FMT_SGBRG8_1X8) ||
				(code.code == MEDIA_BUS_FMT_SBGGR8_1X8)) {
			mipi_csis_imx8mp_dewarp_ctl_data_type(state,
					 ISP_DEWARP_CTRL_DATA_TYPE_RAW8);
			v4l2_dbg(1, debug, &state->sd,
					"%s: bus fmt is 8 bit!\n", __func__);
		} else if ((code.code == MEDIA_BUS_FMT_SRGGB10_1X10) ||
				(code.code == MEDIA_BUS_FMT_SGRBG10_1X10) ||
				(code.code == MEDIA_BUS_FMT_SGBRG10_1X10) ||
				(code.code == MEDIA_BUS_FMT_SBGGR10_1X10)) {
			mipi_csis_imx8mp_dewarp_ctl_data_type(state,
					ISP_DEWARP_CTRL_DATA_TYPE_RAW10);
			v4l2_dbg(1, debug, &state->sd,
					"%s: bus fmt is 10 bit !\n", __func__);
		} else {
			mipi_csis_imx8mp_dewarp_ctl_data_type(state,
					ISP_DEWARP_CTRL_DATA_TYPE_RAW12);
			v4l2_dbg(1, debug, &state->sd,
					"%s: bus fmt is 12 bit !\n", __func__);
		}
		goto write_regmap;
	}

csi_phy_initial_cfg:
	mipi_csis_imx8mp_dewarp_ctl_data_type(state,
			ISP_DEWARP_CTRL_DATA_TYPE_RAW12);
	v4l2_dbg(1, debug, &state->sd,
			"%s: bus fmt is 12 bit !\n", __func__);

write_regmap:
	state->val |= ISP_DEWARP_CTRL_ID_MODE(ISP_DEWARP_CTRL_ID_MODE_012);
	mipi_csis_imx8mp_dewarp_ctl_left_just_mode(state);
	if (state->mix_gpr)
		regmap_update_bits(state->mix_gpr, ISP_DEWARP_CTRL,
			state->val, state->val);

	return;
}

static struct mipi_csis_pdata mipi_csis_imx8mp_pdata = {
	.rst_ops  = &imx8mp_rst_ops,
	.gclk_ops = &imx8mp_gclk_ops,
	.use_mix_gpr = true,
};

static int mipi_csis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *mipi_sd;
	struct resource *mem_res;
	struct csi_state *state;
	const struct of_device_id *of_id;
	int ret = -ENOMEM;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->lock);
	spin_lock_init(&state->slock);

	state->pdev = pdev;
	mipi_sd = &state->sd;
	state->dev = dev;
	state->val = 0;

	ret = mipi_csis_parse_dt(pdev, state);
	if (ret < 0)
		return ret;

	if (state->num_lanes == 0 || state->num_lanes > state->max_num_lanes) {
		dev_err(dev, "Unsupported number of data lanes: %d (max. %d)\n",
			state->num_lanes, state->max_num_lanes);
		return -EINVAL;
	}

	ret = mipi_csis_phy_init(state);
	if (ret < 0)
		return ret;

	of_id = of_match_node(mipi_csis_of_match, dev->of_node);
	if (!of_id || !of_id->data) {
		dev_err(dev, "No match data for %s\n", dev_name(dev));
		return -EINVAL;
	}
	state->pdata = of_id->data;

	state->gasket = syscon_regmap_lookup_by_phandle(dev->of_node, "csi-gpr");
	if (IS_ERR(state->gasket)) {
		dev_err(dev, "failed to get csi gasket\n");
		return PTR_ERR(state->gasket);
	}

	ret = disp_mix_sft_parse_resets(state);
	if (ret < 0)
		return ret;

	if (state->pdata->use_mix_gpr) {
		state->mix_gpr = syscon_regmap_lookup_by_phandle(dev->of_node, "gpr");
		if (IS_ERR(state->mix_gpr)) {
			dev_err(dev, "failed to get mix gpr\n");
			return PTR_ERR(state->mix_gpr);
		}
	}

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	state->regs = devm_ioremap_resource(dev, mem_res);
	if (IS_ERR(state->regs))
		return PTR_ERR(state->regs);

	state->irq = platform_get_irq(pdev, 0);
	if (state->irq < 0) {
		dev_err(dev, "Failed to get irq\n");
		return state->irq;
	}

	ret = mipi_csis_clk_get(state);
	if (ret < 0)
		return ret;

	ret = disp_mix_clks_get(state);
	if (ret < 0) {
		dev_err(dev, "Failed to get disp mix clocks");
		return ret;
	}

	ret = mipi_csis_clk_enable(state);
	if (ret < 0)
		return ret;

	disp_mix_clks_enable(state, true);
	disp_mix_sft_rstn(state, false);
	mipi_csis_imx8mp_phy_reset(state);

	disp_mix_clks_enable(state, false);
	mipi_csis_clk_disable(state);

	ret = devm_request_irq(dev, state->irq, mipi_csis_irq_handler, 0,
			       dev_name(dev), state);
	if (ret) {
		dev_err(dev, "Interrupt request failed\n");
		return ret;
	}

	platform_set_drvdata(pdev, state);
	ret = mipi_csis_subdev_init(&state->sd, pdev, &mipi_csis_subdev_ops);
	if (ret < 0) {
		dev_err(dev, "mipi csi subdev init failed\n");
		return ret;
	}

	state->pads[MIPI_CSIS_VC0_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	state->pads[MIPI_CSIS_VC1_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	state->pads[MIPI_CSIS_VC2_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	state->pads[MIPI_CSIS_VC3_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	state->pads[MIPI_CSIS_VC0_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	state->pads[MIPI_CSIS_VC1_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	state->pads[MIPI_CSIS_VC2_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	state->pads[MIPI_CSIS_VC3_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&state->sd.entity, MIPI_CSIS_VCX_PADS_NUM, state->pads);
	if (ret < 0) {
		dev_err(dev, "mipi csi entity pad init failed\n");
		return ret;
	}

	memcpy(state->events, mipi_csis_events, sizeof(state->events));
	state->sd.entity.ops = &mipi_csi2_sd_media_ops;

	pm_runtime_enable(dev);

	dev_info(&pdev->dev, "lanes: %d, hs_settle: %d, clk_settle: %d, wclk: %d, freq: %u\n",
		 state->num_lanes, state->hs_settle, state->clk_settle,
		 state->wclk_ext, state->clk_frequency);
	return 0;
}

static int mipi_csis_system_suspend(struct device *dev)
{
	return pm_runtime_force_suspend(dev);;
}

static int mipi_csis_system_resume(struct device *dev)
{
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0) {
		dev_err(dev, "force resume %s failed!\n", dev_name(dev));
		return ret;
	}

	return 0;
}

static int mipi_csis_runtime_suspend(struct device *dev)
{
	struct csi_state *state = dev_get_drvdata(dev);
	int ret;

	ret = regulator_disable(state->mipi_phy_regulator);
	if (ret < 0)
		return ret;

	disp_mix_clks_enable(state, false);
	mipi_csis_clk_disable(state);
	return 0;
}

static int mipi_csis_runtime_resume(struct device *dev)
{
	struct csi_state *state = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(state->mipi_phy_regulator);
	if (ret < 0)
		return ret;

	ret = mipi_csis_clk_enable(state);
	if (ret < 0)
		return ret;

	disp_mix_clks_enable(state, true);
	disp_mix_sft_rstn(state, false);
	mipi_csis_imx8mp_phy_reset(state);

	return 0;
}

static void mipi_csis_remove(struct platform_device *pdev)
{
	struct csi_state *state = platform_get_drvdata(pdev);

	media_entity_cleanup(&state->sd.entity);
	pm_runtime_disable(&pdev->dev);
}

static const struct dev_pm_ops mipi_csis_pm_ops = {
	SET_RUNTIME_PM_OPS(mipi_csis_runtime_suspend, mipi_csis_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(mipi_csis_system_suspend, mipi_csis_system_resume)
};

static const struct of_device_id mipi_csis_of_match[] = {
	{	.compatible = "fsl,imx8mp-mipi-csi",
		.data = (void *)&mipi_csis_imx8mp_pdata,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mipi_csis_of_match);

static struct platform_driver mipi_csis_driver = {
	.driver = {
		.name  = CSIS_DRIVER_NAME,
		.owner = THIS_MODULE,
		.pm    = &mipi_csis_pm_ops,
		.of_match_table = mipi_csis_of_match,
	},
	.probe  = mipi_csis_probe,
	.remove = mipi_csis_remove,
};
module_platform_driver(mipi_csis_driver);

MODULE_DESCRIPTION("Freescale MIPI-CSI2 receiver driver");
MODULE_LICENSE("GPL");

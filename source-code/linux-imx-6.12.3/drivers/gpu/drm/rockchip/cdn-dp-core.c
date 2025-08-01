// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Chris Zhong <zyw@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/extcon.h>
#include <linux/firmware.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <sound/hdmi-codec.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "cdn-dp-core.h"

static inline struct cdn_dp_device *connector_to_dp(struct drm_connector *connector)
{
	return container_of(connector, struct cdn_dp_device, mhdp.connector.base);
}

static inline struct cdn_dp_device *encoder_to_dp(struct drm_encoder *encoder)
{
	struct rockchip_encoder *rkencoder = to_rockchip_encoder(encoder);

	return container_of(rkencoder, struct cdn_dp_device, encoder);
}

#define GRF_SOC_CON9		0x6224
#define DP_SEL_VOP_LIT		BIT(12)
#define GRF_SOC_CON26		0x6268
#define DPTX_HPD_SEL		(3 << 12)
#define DPTX_HPD_DEL		(2 << 12)
#define DPTX_HPD_SEL_MASK	(3 << 28)

#define CDN_FW_TIMEOUT_MS	(64 * 1000)
#define CDN_DPCD_TIMEOUT_MS	5000
#define CDN_DP_FIRMWARE		"rockchip/dptx.bin"
MODULE_FIRMWARE(CDN_DP_FIRMWARE);

struct cdn_dp_data {
	u8 max_phy;
};

static struct cdn_dp_data rk3399_cdn_dp = {
	.max_phy = 2,
};

static const struct of_device_id cdn_dp_dt_ids[] = {
	{ .compatible = "rockchip,rk3399-cdn-dp",
		.data = (void *)&rk3399_cdn_dp },
	{}
};

MODULE_DEVICE_TABLE(of, cdn_dp_dt_ids);

static int cdn_dp_grf_write(struct cdn_dp_device *dp,
			    unsigned int reg, unsigned int val)
{
	struct device *dev = dp->mhdp.dev;
	int ret;

	ret = clk_prepare_enable(dp->grf_clk);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to prepare_enable grf clock\n");
		return ret;
	}

	ret = regmap_write(dp->grf, reg, val);
	if (ret) {
		DRM_DEV_ERROR(dev, "Could not write to GRF: %d\n", ret);
		clk_disable_unprepare(dp->grf_clk);
		return ret;
	}

	clk_disable_unprepare(dp->grf_clk);

	return 0;
}

static int cdn_dp_clk_enable(struct cdn_dp_device *dp)
{
	struct device *dev = dp->mhdp.dev;
	int ret;
	unsigned long rate;

	ret = clk_prepare_enable(dp->pclk);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "cannot enable dp pclk %d\n", ret);
		goto err_pclk;
	}

	ret = clk_prepare_enable(dp->core_clk);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "cannot enable core_clk %d\n", ret);
		goto err_core_clk;
	}

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "cannot get pm runtime %d\n", ret);
		goto err_pm_runtime_get;
	}

	reset_control_assert(dp->core_rst);
	reset_control_assert(dp->dptx_rst);
	reset_control_assert(dp->apb_rst);
	reset_control_deassert(dp->core_rst);
	reset_control_deassert(dp->dptx_rst);
	reset_control_deassert(dp->apb_rst);

	rate = clk_get_rate(dp->core_clk);
	if (!rate) {
		DRM_DEV_ERROR(dev, "get clk rate failed\n");
		ret = -EINVAL;
		goto err_set_rate;
	}

	cdns_mhdp_set_fw_clk(&dp->mhdp, rate);
	cdns_mhdp_clock_reset(&dp->mhdp);

	return 0;

err_set_rate:
	pm_runtime_put(dev);
err_pm_runtime_get:
	clk_disable_unprepare(dp->core_clk);
err_core_clk:
	clk_disable_unprepare(dp->pclk);
err_pclk:
	return ret;
}

static void cdn_dp_clk_disable(struct cdn_dp_device *dp)
{
	pm_runtime_put_sync(dp->mhdp.dev);
	clk_disable_unprepare(dp->pclk);
	clk_disable_unprepare(dp->core_clk);
}

static int cdn_dp_get_port_lanes(struct cdn_dp_port *port)
{
	struct extcon_dev *edev = port->extcon;
	union extcon_property_value property;
	int dptx;
	u8 lanes;

	dptx = extcon_get_state(edev, EXTCON_DISP_DP);
	if (dptx > 0) {
		extcon_get_property(edev, EXTCON_DISP_DP,
				    EXTCON_PROP_USB_SS, &property);
		if (property.intval)
			lanes = 2;
		else
			lanes = 4;
	} else {
		lanes = 0;
	}

	return lanes;
}

static int cdn_dp_get_sink_count(struct cdn_dp_device *dp, u8 *sink_count)
{
	int ret;
	u8 value;

	*sink_count = 0;
	ret = drm_dp_dpcd_read(&dp->mhdp.dp.aux, DP_SINK_COUNT, &value, 1);
	if (ret)
		return ret;

	*sink_count = DP_GET_SINK_COUNT(value);
	return 0;
}

static struct cdn_dp_port *cdn_dp_connected_port(struct cdn_dp_device *dp)
{
	struct cdn_dp_port *port;
	int i, lanes;

	for (i = 0; i < dp->ports; i++) {
		port = dp->port[i];
		lanes = cdn_dp_get_port_lanes(port);
		if (lanes)
			return port;
	}
	return NULL;
}

static bool cdn_dp_check_sink_connection(struct cdn_dp_device *dp)
{
	struct device *dev = dp->mhdp.dev;
	unsigned long timeout = jiffies + msecs_to_jiffies(CDN_DPCD_TIMEOUT_MS);
	struct cdn_dp_port *port;
	u8 sink_count = 0;

	if (dp->active_port < 0 || dp->active_port >= dp->ports) {
		DRM_DEV_ERROR(dev, "active_port is wrong!\n");
		return false;
	}

	port = dp->port[dp->active_port];

	/*
	 * Attempt to read sink count, retry in case the sink may not be ready.
	 *
	 * Sinks are *supposed* to come up within 1ms from an off state, but
	 * some docks need more time to power up.
	 */
	while (time_before(jiffies, timeout)) {
		if (!extcon_get_state(port->extcon, EXTCON_DISP_DP))
			return false;

		if (!cdn_dp_get_sink_count(dp, &sink_count))
			return sink_count ? true : false;

		usleep_range(5000, 10000);
	}

	DRM_DEV_ERROR(dev, "Get sink capability timed out\n");
	return false;
}

static enum drm_connector_status
cdn_dp_connector_detect(struct drm_connector *connector, bool force)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);
	enum drm_connector_status status = connector_status_disconnected;

	mutex_lock(&dp->lock);
	if (dp->connected)
		status = connector_status_connected;
	mutex_unlock(&dp->lock);

	return status;
}

static void cdn_dp_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs cdn_dp_atomic_connector_funcs = {
	.detect = cdn_dp_connector_detect,
	.destroy = cdn_dp_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int cdn_dp_connector_get_modes(struct drm_connector *connector)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);
	int ret = 0;

	mutex_lock(&dp->lock);

	ret = drm_edid_connector_add_modes(connector);

	mutex_unlock(&dp->lock);

	return ret;
}

static enum drm_mode_status
cdn_dp_connector_mode_valid(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	struct cdn_dp_device *dp = connector_to_dp(connector);
	struct drm_display_info *display_info =
		&dp->mhdp.connector.base.display_info;
	u32 requested, actual, rate, sink_max, source_max = 0;
	u8 lanes, bpc;

	/* If DP is disconnected, every mode is invalid */
	if (!dp->connected)
		return MODE_BAD;

	switch (display_info->bpc) {
	case 10:
		bpc = 10;
		break;
	case 6:
		bpc = 6;
		break;
	default:
		bpc = 8;
		break;
	}

	requested = mode->clock * bpc * 3 / 1000;

	source_max = dp->lanes;
	sink_max = drm_dp_max_lane_count(dp->mhdp.dp.dpcd);
	lanes = min(source_max, sink_max);

	source_max = CDNS_DP_MAX_LINK_RATE;
	sink_max = drm_dp_max_link_rate(dp->mhdp.dp.dpcd);
	rate = min(source_max, sink_max);

	actual = rate * lanes / 100;

	/* efficiency is about 0.8 */
	actual = actual * 8 / 10;

	if (requested > actual) {
		DRM_DEV_DEBUG_KMS(dp->mhdp.dev,
				  "requested=%d, actual=%d, clock=%d\n",
				  requested, actual, mode->clock);
		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

static struct drm_connector_helper_funcs cdn_dp_connector_helper_funcs = {
	.get_modes = cdn_dp_connector_get_modes,
	.mode_valid = cdn_dp_connector_mode_valid,
};

static int cdn_dp_firmware_init(struct cdn_dp_device *dp)
{
	int ret;
	const u32 *iram_data, *dram_data;
	const struct firmware *fw = dp->fw;
	const struct cdn_firmware_header *hdr;
	struct device *dev = dp->mhdp.dev;

	hdr = (struct cdn_firmware_header *)fw->data;
	if (fw->size != le32_to_cpu(hdr->size_bytes)) {
		DRM_DEV_ERROR(dev, "firmware is invalid\n");
		return -EINVAL;
	}

	iram_data = (const u32 *)(fw->data + hdr->header_size);
	dram_data = (const u32 *)(fw->data + hdr->header_size + hdr->iram_size);

	ret = cdns_mhdp_load_firmware(&dp->mhdp, iram_data, hdr->iram_size,
				      dram_data, hdr->dram_size);
	if (ret)
		return ret;

	ret = cdns_mhdp_set_firmware_active(&dp->mhdp, true);
	if (ret) {
		DRM_DEV_ERROR(dev, "active ucpu failed: %d\n", ret);
		return ret;
	}

	return cdns_mhdp_event_config(&dp->mhdp);
}

static int cdn_dp_get_sink_capability(struct cdn_dp_device *dp)
{
	const struct drm_display_info *info = &dp->mhdp.connector.base.display_info;
	struct cdns_mhdp_device *mhdp = &dp->mhdp;
	int ret;

	if (!cdn_dp_check_sink_connection(dp))
		return -ENODEV;

	ret = drm_dp_dpcd_read(&mhdp->dp.aux, DP_DPCD_REV, mhdp->dp.dpcd,
			       DP_RECEIVER_CAP_SIZE);
	if (ret) {
		DRM_DEV_ERROR(mhdp->dev, "Failed to get caps %d\n", ret);
		return ret;
	}

	drm_edid_free(dp->drm_edid);
	dp->drm_edid = drm_edid_read_custom(&mhdp->connector.base,
					    cdns_mhdp_get_edid_block, mhdp);
	drm_edid_connector_update(&mhdp->connector.base, dp->drm_edid);

	dp->sink_has_audio = info->has_audio;

	if (dp->drm_edid)
		DRM_DEV_DEBUG_KMS(mhdp->dev, "got edid: width[%d] x height[%d]\n",
				  info->width_mm / 10, info->height_mm / 10);

	return 0;
}

static int cdn_dp_enable_phy(struct cdn_dp_device *dp, struct cdn_dp_port *port)
{
	struct device *dev = dp->mhdp.dev;
	union extcon_property_value property;
	int ret;

	if (!port->phy_enabled) {
		ret = phy_power_on(port->phy);
		if (ret) {
			DRM_DEV_ERROR(dev, "phy power on failed: %d\n",
				      ret);
			goto err_phy;
		}
		port->phy_enabled = true;
	}

	ret = cdn_dp_grf_write(dp, GRF_SOC_CON26,
			       DPTX_HPD_SEL_MASK | DPTX_HPD_SEL);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to write HPD_SEL %d\n", ret);
		goto err_power_on;
	}

	ret = cdns_mhdp_read_hpd(&dp->mhdp);
	if (ret <= 0) {
		if (!ret)
			DRM_DEV_ERROR(dev, "hpd does not exist\n");
		goto err_power_on;
	}

	ret = extcon_get_property(port->extcon, EXTCON_DISP_DP,
				  EXTCON_PROP_USB_TYPEC_POLARITY, &property);
	if (ret) {
		DRM_DEV_ERROR(dev, "get property failed\n");
		goto err_power_on;
	}

	port->lanes = cdn_dp_get_port_lanes(port);

	if (property.intval)
		dp->mhdp.lane_mapping = LANE_MAPPING_FLIPPED;
	else
		dp->mhdp.lane_mapping = LANE_MAPPING_NORMAL;
	ret = cdns_mhdp_set_host_cap(&dp->mhdp);
	if (ret) {
		DRM_DEV_ERROR(dev, "set host capabilities failed: %d\n",
			      ret);
		goto err_power_on;
	}

	dp->active_port = port->id;
	return 0;

err_power_on:
	if (phy_power_off(port->phy))
		DRM_DEV_ERROR(dev, "phy power off failed: %d", ret);
	else
		port->phy_enabled = false;

err_phy:
	cdn_dp_grf_write(dp, GRF_SOC_CON26,
			 DPTX_HPD_SEL_MASK | DPTX_HPD_DEL);
	return ret;
}

static int cdn_dp_disable_phy(struct cdn_dp_device *dp,
			      struct cdn_dp_port *port)
{
	int ret;

	if (port->phy_enabled) {
		ret = phy_power_off(port->phy);
		if (ret) {
			DRM_DEV_ERROR(dp->mhdp.dev,
				      "phy power off failed: %d", ret);
			return ret;
		}
	}

	port->phy_enabled = false;
	port->lanes = 0;
	dp->active_port = -1;
	return 0;
}

static int cdn_dp_disable(struct cdn_dp_device *dp)
{
	int ret, i;

	if (!dp->active)
		return 0;

	for (i = 0; i < dp->ports; i++)
		cdn_dp_disable_phy(dp, dp->port[i]);

	ret = cdn_dp_grf_write(dp, GRF_SOC_CON26,
			       DPTX_HPD_SEL_MASK | DPTX_HPD_DEL);
	if (ret) {
		DRM_DEV_ERROR(dp->mhdp.dev, "Failed to clear hpd sel %d\n",
			      ret);
		return ret;
	}

	cdns_mhdp_set_firmware_active(&dp->mhdp, false);
	cdn_dp_clk_disable(dp);
	dp->active = false;
	dp->mhdp.dp.rate = 0;
	dp->mhdp.dp.num_lanes = 0;
	if (!dp->connected) {
		drm_edid_free(dp->drm_edid);
		dp->drm_edid = NULL;
	}

	return 0;
}

static int cdn_dp_enable(struct cdn_dp_device *dp)
{
	int ret, i, lanes;
	struct cdn_dp_port *port;
	struct device *dev = dp->mhdp.dev;

	port = cdn_dp_connected_port(dp);
	if (!port) {
		DRM_DEV_ERROR(dev, "Can't enable without connection\n");
		return -ENODEV;
	}

	if (dp->active)
		return 0;

	ret = cdn_dp_clk_enable(dp);
	if (ret)
		return ret;

	ret = cdn_dp_firmware_init(dp);
	if (ret) {
		DRM_DEV_ERROR(dp->mhdp.dev, "firmware init failed: %d", ret);
		goto err_clk_disable;
	}

	/* only enable the port that connected with downstream device */
	for (i = port->id; i < dp->ports; i++) {
		port = dp->port[i];
		lanes = cdn_dp_get_port_lanes(port);
		if (lanes) {
			ret = cdn_dp_enable_phy(dp, port);
			if (ret)
				continue;

			ret = cdn_dp_get_sink_capability(dp);
			if (ret) {
				cdn_dp_disable_phy(dp, port);
			} else {
				dp->active = true;
				dp->lanes = port->lanes;
				return 0;
			}
		}
	}

err_clk_disable:
	cdn_dp_clk_disable(dp);
	return ret;
}

static void cdn_dp_encoder_mode_set(struct drm_encoder *encoder,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted)
{
	struct cdn_dp_device *dp = encoder_to_dp(encoder);
	struct drm_display_info *display_info =
		&dp->mhdp.connector.base.display_info;
	struct video_info *video = &dp->mhdp.video_info;

	switch (display_info->bpc) {
	case 10:
		video->color_depth = 10;
		break;
	case 6:
		video->color_depth = 6;
		break;
	default:
		video->color_depth = 8;
		break;
	}

	video->color_fmt = PXL_RGB;
	video->v_sync_polarity = !!(mode->flags & DRM_MODE_FLAG_NVSYNC);
	video->h_sync_polarity = !!(mode->flags & DRM_MODE_FLAG_NHSYNC);

	drm_mode_copy(&dp->mhdp.mode, adjusted);
}

static bool cdn_dp_check_link_status(struct cdn_dp_device *dp)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	struct cdn_dp_port *port = cdn_dp_connected_port(dp);
	u8 sink_lanes = drm_dp_max_lane_count(dp->mhdp.dp.dpcd);

	if (!port || !dp->mhdp.dp.rate || !dp->mhdp.dp.num_lanes)
		return false;

	if (drm_dp_dpcd_read(&dp->mhdp.dp.aux, DP_LANE0_1_STATUS, link_status,
				DP_LINK_STATUS_SIZE)) {
		DRM_ERROR("Failed to get link status\n");
		return false;
	}

	/* if link training is requested we should perform it always */
	return drm_dp_channel_eq_ok(link_status, min(port->lanes, sink_lanes));
}

static void cdn_dp_audio_handle_plugged_change(struct cdn_dp_device *dp,
					       bool plugged)
{
	if (dp->codec_dev)
		dp->plugged_cb(dp->codec_dev, plugged);
}

static void cdn_dp_encoder_enable(struct drm_encoder *encoder)
{
	struct cdn_dp_device *dp = encoder_to_dp(encoder);
	struct device *dev = dp->mhdp.dev;
	int ret, val;

	ret = drm_of_encoder_active_endpoint_id(dev->of_node, encoder);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Could not get vop id, %d", ret);
		return;
	}

	DRM_DEV_DEBUG_KMS(dev, "vop %s output to cdn-dp\n",
			  (ret) ? "LIT" : "BIG");
	if (ret)
		val = DP_SEL_VOP_LIT | (DP_SEL_VOP_LIT << 16);
	else
		val = DP_SEL_VOP_LIT << 16;

	ret = cdn_dp_grf_write(dp, GRF_SOC_CON9, val);
	if (ret)
		return;

	mutex_lock(&dp->lock);

	ret = cdn_dp_enable(dp);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to enable encoder %d\n",
			      ret);
		goto out;
	}
	if (!cdn_dp_check_link_status(dp)) {
		ret = cdns_mhdp_train_link(&dp->mhdp);
		if (ret) {
			DRM_DEV_ERROR(dev, "Failed link train %d\n", ret);
			goto out;
		}
	}

	ret = cdns_mhdp_set_video_status(&dp->mhdp, CONTROL_VIDEO_IDLE);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to idle video %d\n", ret);
		goto out;
	}

	ret = cdns_mhdp_config_video(&dp->mhdp);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to config video %d\n", ret);
		goto out;
	}

	ret = cdns_mhdp_set_video_status(&dp->mhdp, CONTROL_VIDEO_VALID);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to valid video %d\n", ret);
		goto out;
	}

	cdn_dp_audio_handle_plugged_change(dp, true);

out:
	mutex_unlock(&dp->lock);
}

static void cdn_dp_encoder_disable(struct drm_encoder *encoder)
{
	struct cdn_dp_device *dp = encoder_to_dp(encoder);
	int ret;

	mutex_lock(&dp->lock);
	cdn_dp_audio_handle_plugged_change(dp, false);

	if (dp->active) {
		ret = cdn_dp_disable(dp);
		if (ret) {
			DRM_DEV_ERROR(dp->mhdp.dev,
				      "Failed to disable encoder %d\n",
				      ret);
		}
	}
	mutex_unlock(&dp->lock);

	/*
	 * In the following 2 cases, we need to run the event_work to re-enable
	 * the DP:
	 * 1. If there is not just one port device is connected, and remove one
	 *    device from a port, the DP will be disabled here, at this case,
	 *    run the event_work to re-open DP for the other port.
	 * 2. If re-training or re-config failed, the DP will be disabled here.
	 *    run the event_work to re-connect it.
	 */
	if (!dp->connected && cdn_dp_connected_port(dp))
		schedule_work(&dp->event_work);
}

static int cdn_dp_encoder_atomic_check(struct drm_encoder *encoder,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
	s->output_type = DRM_MODE_CONNECTOR_DisplayPort;

	return 0;
}

static const struct drm_encoder_helper_funcs cdn_dp_encoder_helper_funcs = {
	.mode_set = cdn_dp_encoder_mode_set,
	.enable = cdn_dp_encoder_enable,
	.disable = cdn_dp_encoder_disable,
	.atomic_check = cdn_dp_encoder_atomic_check,
};

static int cdn_dp_parse_dt(struct cdn_dp_device *dp)
{
	struct device *dev = dp->mhdp.dev;
	struct device_node *np = dev->of_node;
	struct platform_device *pdev = to_platform_device(dev);

	dp->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(dp->grf)) {
		DRM_DEV_ERROR(dev, "cdn-dp needs rockchip,grf property\n");
		return PTR_ERR(dp->grf);
	}

	dp->mhdp.regs_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dp->mhdp.regs_base)) {
		DRM_DEV_ERROR(dev, "ioremap reg failed\n");
		return PTR_ERR(dp->mhdp.regs_base);
	}

	dp->core_clk = devm_clk_get(dev, "core-clk");
	if (IS_ERR(dp->core_clk)) {
		DRM_DEV_ERROR(dev, "cannot get core_clk_dp\n");
		return PTR_ERR(dp->core_clk);
	}

	dp->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dp->pclk)) {
		DRM_DEV_ERROR(dev, "cannot get pclk\n");
		return PTR_ERR(dp->pclk);
	}

	dp->mhdp.spdif_clk = devm_clk_get(dev, "spdif");
	if (IS_ERR(dp->mhdp.spdif_clk)) {
		DRM_DEV_ERROR(dev, "cannot get spdif_clk\n");
		return PTR_ERR(dp->mhdp.spdif_clk);
	}

	dp->grf_clk = devm_clk_get(dev, "grf");
	if (IS_ERR(dp->grf_clk)) {
		DRM_DEV_ERROR(dev, "cannot get grf clk\n");
		return PTR_ERR(dp->grf_clk);
	}

	dp->mhdp.spdif_rst = devm_reset_control_get(dev, "spdif");
	if (IS_ERR(dp->mhdp.spdif_rst)) {
		DRM_DEV_ERROR(dev, "no spdif reset control found\n");
		return PTR_ERR(dp->mhdp.spdif_rst);
	}

	dp->dptx_rst = devm_reset_control_get(dev, "dptx");
	if (IS_ERR(dp->dptx_rst)) {
		DRM_DEV_ERROR(dev, "no uphy reset control found\n");
		return PTR_ERR(dp->dptx_rst);
	}

	dp->core_rst = devm_reset_control_get(dev, "core");
	if (IS_ERR(dp->core_rst)) {
		DRM_DEV_ERROR(dev, "no core reset control found\n");
		return PTR_ERR(dp->core_rst);
	}

	dp->apb_rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(dp->apb_rst)) {
		DRM_DEV_ERROR(dev, "no apb reset control found\n");
		return PTR_ERR(dp->apb_rst);
	}

	return 0;
}

static int cdn_dp_audio_hw_params(struct device *dev,  void *data,
				  struct hdmi_codec_daifmt *daifmt,
				  struct hdmi_codec_params *params)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	struct audio_info audio = {
		.sample_width = params->sample_width,
		.sample_rate = params->sample_rate,
		.channels = params->channels,
	};
	int ret;

	mutex_lock(&dp->lock);
	if (!dp->active) {
		ret = -ENODEV;
		goto out;
	}

	switch (daifmt->fmt) {
	case HDMI_I2S:
		audio.format = AFMT_I2S;
		break;
	case HDMI_SPDIF:
		audio.format = AFMT_SPDIF_INT;
		break;
	default:
		DRM_DEV_ERROR(dev, "Invalid format %d\n", daifmt->fmt);
		ret = -EINVAL;
		goto out;
	}

	ret = cdns_mhdp_audio_config(&dp->mhdp, &audio);
	if (!ret)
		dp->mhdp.audio_info = audio;

out:
	mutex_unlock(&dp->lock);
	return ret;
}

static void cdn_dp_audio_shutdown(struct device *dev, void *data)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&dp->lock);
	if (!dp->active)
		goto out;

	ret = cdns_mhdp_audio_stop(&dp->mhdp, &dp->mhdp.audio_info);
	if (!ret)
		dp->mhdp.audio_info.format = AFMT_UNUSED;
out:
	mutex_unlock(&dp->lock);
}

static int cdn_dp_audio_mute_stream(struct device *dev, void *data,
				    bool enable, int direction)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&dp->lock);
	if (!dp->active) {
		ret = -ENODEV;
		goto out;
	}

	ret = cdns_mhdp_audio_mute(&dp->mhdp, enable);

out:
	mutex_unlock(&dp->lock);
	return ret;
}

static int cdn_dp_audio_get_eld(struct device *dev, void *data,
				u8 *buf, size_t len)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);

	memcpy(buf, dp->mhdp.connector.base.eld,
	       min(sizeof(dp->mhdp.connector.base.eld), len));

	return 0;
}

static int cdn_dp_audio_hook_plugged_cb(struct device *dev, void *data,
					hdmi_codec_plugged_cb fn,
					struct device *codec_dev)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);

	mutex_lock(&dp->lock);
	dp->plugged_cb = fn;
	dp->codec_dev = codec_dev;
	cdn_dp_audio_handle_plugged_change(dp, dp->connected);
	mutex_unlock(&dp->lock);

	return 0;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = cdn_dp_audio_hw_params,
	.audio_shutdown = cdn_dp_audio_shutdown,
	.mute_stream = cdn_dp_audio_mute_stream,
	.get_eld = cdn_dp_audio_get_eld,
	.hook_plugged_cb = cdn_dp_audio_hook_plugged_cb,
	.no_capture_mute = 1,
};

static int cdn_dp_audio_codec_init(struct cdn_dp_device *dp,
				   struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.i2s = 1,
		.spdif = 1,
		.ops = &audio_codec_ops,
		.max_i2s_channels = 8,
	};

	dp->mhdp.audio_pdev = platform_device_register_data(
			      dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
			      &codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(dp->mhdp.audio_pdev);
}

static int cdn_dp_request_firmware(struct cdn_dp_device *dp)
{
	int ret;
	unsigned long timeout = jiffies + msecs_to_jiffies(CDN_FW_TIMEOUT_MS);
	unsigned long sleep = 1000;
	struct device *dev = dp->mhdp.dev;

	WARN_ON(!mutex_is_locked(&dp->lock));

	if (dp->fw_loaded)
		return 0;

	/* Drop the lock before getting the firmware to avoid blocking boot */
	mutex_unlock(&dp->lock);

	while (time_before(jiffies, timeout)) {
		ret = request_firmware(&dp->fw, CDN_DP_FIRMWARE, dev);
		if (ret == -ENOENT) {
			msleep(sleep);
			sleep *= 2;
			continue;
		} else if (ret) {
			DRM_DEV_ERROR(dev,
				      "failed to request firmware: %d\n", ret);
			goto out;
		}

		dp->fw_loaded = true;
		ret = 0;
		goto out;
	}

	DRM_DEV_ERROR(dev, "Timed out trying to load firmware\n");
	ret = -ETIMEDOUT;
out:
	mutex_lock(&dp->lock);
	return ret;
}

static void cdn_dp_pd_event_work(struct work_struct *work)
{
	struct cdn_dp_device *dp = container_of(work, struct cdn_dp_device,
						event_work);
	struct drm_connector *connector = &dp->mhdp.connector.base;
	enum drm_connector_status old_status;
	struct device *dev = dp->mhdp.dev;

	int ret;

	mutex_lock(&dp->lock);

	if (dp->suspended)
		goto out;

	ret = cdn_dp_request_firmware(dp);
	if (ret)
		goto out;

	dp->connected = true;

	/* Not connected, notify userspace to disable the block */
	if (!cdn_dp_connected_port(dp)) {
		DRM_DEV_INFO(dev, "Not connected; disabling cdn\n");
		dp->connected = false;

	/* Connected but not enabled, enable the block */
	} else if (!dp->active) {
		DRM_DEV_INFO(dev, "Connected, not enabled; enabling cdn\n");
		ret = cdn_dp_enable(dp);
		if (ret) {
			DRM_DEV_ERROR(dev, "Enabling dp failed: %d\n", ret);
			dp->connected = false;
		}

	/* Enabled and connected to a dongle without a sink, notify userspace */
	} else if (!cdn_dp_check_sink_connection(dp)) {
		DRM_DEV_INFO(dev, "Connected without sink; assert hpd\n");
		dp->connected = false;

	/* Enabled and connected with a sink, re-train if requested */
	} else if (!cdn_dp_check_link_status(dp)) {
		unsigned int rate = dp->mhdp.dp.rate;
		unsigned int lanes = dp->mhdp.dp.num_lanes;
		struct drm_display_mode *mode = &dp->mhdp.mode;

		DRM_DEV_INFO(dev, "Connected with sink; re-train link\n");
		ret = cdns_mhdp_train_link(&dp->mhdp);
		if (ret) {
			dp->connected = false;
			DRM_DEV_ERROR(dev, "Training link failed: %d\n", ret);
			goto out;
		}

		/* If training result is changed, update the video config */
		if (mode->clock &&
		    (rate != dp->mhdp.dp.rate ||
		     lanes != dp->mhdp.dp.num_lanes)) {
			ret = cdns_mhdp_config_video(&dp->mhdp);
			if (ret) {
				dp->connected = false;
				DRM_DEV_ERROR(dev, "Failed to configure video: %d\n", ret);
			}
		}
	}

out:
	mutex_unlock(&dp->lock);

	old_status = connector->status;
	connector->status = connector->funcs->detect(connector, false);
	if (old_status != connector->status)
		drm_kms_helper_hotplug_event(dp->drm_dev);
}

static int cdn_dp_pd_event(struct notifier_block *nb,
			   unsigned long event, void *priv)
{
	struct cdn_dp_port *port = container_of(nb, struct cdn_dp_port,
						event_nb);
	struct cdn_dp_device *dp = port->dp;

	/*
	 * It would be nice to be able to just do the work inline right here.
	 * However, we need to make a bunch of calls that might sleep in order
	 * to turn on the block/phy, so use a worker instead.
	 */
	schedule_work(&dp->event_work);

	return NOTIFY_DONE;
}

static int cdn_dp_bind(struct device *dev, struct device *master, void *data)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct cdn_dp_port *port;
	struct drm_device *drm_dev = data;
	int ret, i;

	ret = cdn_dp_parse_dt(dp);
	if (ret < 0)
		return ret;

	dp->drm_dev = drm_dev;
	dp->connected = false;
	dp->active = false;
	dp->active_port = -1;
	dp->fw_loaded = false;

	INIT_WORK(&dp->event_work, cdn_dp_pd_event_work);

	encoder = &dp->encoder.encoder;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);
	DRM_DEBUG_KMS("possible_crtcs = 0x%x\n", encoder->possible_crtcs);

	ret = drm_simple_encoder_init(drm_dev, encoder,
				      DRM_MODE_ENCODER_TMDS);
	if (ret) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &cdn_dp_encoder_helper_funcs);

	connector = &dp->mhdp.connector.base;
	connector->polled = DRM_CONNECTOR_POLL_HPD;
	connector->dpms = DRM_MODE_DPMS_OFF;

	ret = drm_connector_init(drm_dev, connector,
				 &cdn_dp_atomic_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		DRM_ERROR("failed to initialize connector with drm\n");
		goto err_free_encoder;
	}

	drm_connector_helper_add(connector, &cdn_dp_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		DRM_ERROR("failed to attach connector and encoder\n");
		goto err_free_connector;
	}

	for (i = 0; i < dp->ports; i++) {
		port = dp->port[i];

		port->event_nb.notifier_call = cdn_dp_pd_event;
		ret = devm_extcon_register_notifier(dp->mhdp.dev, port->extcon,
						    EXTCON_DISP_DP,
						    &port->event_nb);
		if (ret) {
			DRM_DEV_ERROR(dev,
				      "register EXTCON_DISP_DP notifier err\n");
			goto err_free_connector;
		}
	}

	pm_runtime_enable(dev);

	schedule_work(&dp->event_work);

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void cdn_dp_unbind(struct device *dev, struct device *master, void *data)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dp->encoder.encoder;
	struct drm_connector *connector = &dp->mhdp.connector.base;

	cancel_work_sync(&dp->event_work);
	cdn_dp_encoder_disable(encoder);
	encoder->funcs->destroy(encoder);
	connector->funcs->destroy(connector);

	pm_runtime_disable(dev);
	if (dp->fw_loaded)
		release_firmware(dp->fw);
	drm_edid_free(dp->drm_edid);
	dp->drm_edid = NULL;
}

static const struct component_ops cdn_dp_component_ops = {
	.bind = cdn_dp_bind,
	.unbind = cdn_dp_unbind,
};

static int cdn_dp_suspend(struct device *dev)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&dp->lock);
	if (dp->active)
		ret = cdn_dp_disable(dp);
	dp->suspended = true;
	mutex_unlock(&dp->lock);

	return ret;
}

static __maybe_unused int cdn_dp_resume(struct device *dev)
{
	struct cdn_dp_device *dp = dev_get_drvdata(dev);

	mutex_lock(&dp->lock);
	dp->suspended = false;
	if (dp->fw_loaded)
		schedule_work(&dp->event_work);
	mutex_unlock(&dp->lock);

	return 0;
}

static int cdn_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct cdn_dp_data *dp_data;
	struct cdn_dp_port *port;
	struct cdn_dp_device *dp;
	struct extcon_dev *extcon;
	struct phy *phy;
	int ret;
	int i;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;
	dp->mhdp.dev = dev;

	match = of_match_node(cdn_dp_dt_ids, pdev->dev.of_node);
	dp_data = (struct cdn_dp_data *)match->data;

	for (i = 0; i < dp_data->max_phy; i++) {
		extcon = extcon_get_edev_by_phandle(dev, i);
		phy = devm_of_phy_get_by_index(dev, dev->of_node, i);

		if (PTR_ERR(extcon) == -EPROBE_DEFER ||
		    PTR_ERR(phy) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		if (IS_ERR(extcon) || IS_ERR(phy))
			continue;

		port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
		if (!port)
			return -ENOMEM;

		port->extcon = extcon;
		port->phy = phy;
		port->dp = dp;
		port->id = i;
		dp->port[dp->ports++] = port;
	}

	if (!dp->ports) {
		DRM_DEV_ERROR(dev, "missing extcon or phy\n");
		return -EINVAL;
	}

	mutex_init(&dp->lock);
	dev_set_drvdata(dev, dp);

	ret = cdn_dp_audio_codec_init(dp, dev);
	if (ret)
		return ret;

	ret = component_add(dev, &cdn_dp_component_ops);
	if (ret)
		goto err_audio_deinit;

	return 0;

err_audio_deinit:
	platform_device_unregister(dp->mhdp.audio_pdev);
	return ret;
}

static void cdn_dp_remove(struct platform_device *pdev)
{
	struct cdn_dp_device *dp = platform_get_drvdata(pdev);

	platform_device_unregister(dp->mhdp.audio_pdev);
	cdn_dp_suspend(dp->mhdp.dev);
	component_del(&pdev->dev, &cdn_dp_component_ops);
}

static void cdn_dp_shutdown(struct platform_device *pdev)
{
	struct cdn_dp_device *dp = platform_get_drvdata(pdev);

	cdn_dp_suspend(dp->mhdp.dev);
}

static const struct dev_pm_ops cdn_dp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cdn_dp_suspend,
				cdn_dp_resume)
};

struct platform_driver cdn_dp_driver = {
	.probe = cdn_dp_probe,
	.remove_new = cdn_dp_remove,
	.shutdown = cdn_dp_shutdown,
	.driver = {
		   .name = "cdn-dp",
		   .of_match_table = cdn_dp_dt_ids,
		   .pm = &cdn_dp_pm_ops,
	},
};

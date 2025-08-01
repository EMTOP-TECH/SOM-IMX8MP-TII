// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave6 series multi-standard codec IP - platform driver
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/of_platform.h>
#include <linux/debugfs.h>
#include "wave6-vpu.h"
#include "wave6-regdefine.h"
#include "wave6-vpuconfig.h"
#include "wave6.h"
#include "wave6-vpu-ctrl.h"
#include "wave6-vpu-dbg.h"

#define VPU_PLATFORM_DEVICE_NAME "vpu"
#define VPU_CLK_NAME "vcodec"
#define WAVE6_VPU_DEBUGFS_DIR "wave6"

#define WAVE6_IS_ENC BIT(0)
#define WAVE6_IS_DEC BIT(1)

static unsigned int debug;
module_param(debug, uint, 0644);

struct wave6_match_data {
	int flags;
	u32 compatible_fw_version;
};

static const struct wave6_match_data wave633c_data = {
	.flags = WAVE6_IS_ENC | WAVE6_IS_DEC,
	.compatible_fw_version = 0x2010000,
};

unsigned int wave6_vpu_debug(void)
{
	return debug;
}

static void wave6_vpu_get_clk(struct vpu_device *vpu_dev)
{
	int i;

	if (!vpu_dev || !vpu_dev->num_clks || vpu_dev->clk_vpu)
		return;

	for (i = 0; i < vpu_dev->num_clks; i++) {
		if (vpu_dev->clks[i].id && !strcmp(vpu_dev->clks[i].id, "vpu")) {
			vpu_dev->clk_vpu = vpu_dev->clks[i].clk;
			return;
		}
	}

	vpu_dev->clk_vpu = vpu_dev->clks[0].clk;
}

static irqreturn_t wave6_vpu_irq(int irq, void *dev_id)
{
	struct vpu_device *dev = dev_id;
	u32 irq_status;

	if (wave6_vdi_readl(dev, W6_VPU_VPU_INT_STS)) {
		irq_status = wave6_vdi_readl(dev, W6_VPU_VINT_REASON);

		wave6_vdi_writel(dev, W6_VPU_VINT_REASON_CLR, irq_status);
		wave6_vdi_writel(dev, W6_VPU_VINT_CLEAR, 0x1);

		kfifo_in(&dev->irq_status, &irq_status, sizeof(int));

		return IRQ_WAKE_THREAD;
	}

	return IRQ_HANDLED;
}

static irqreturn_t wave6_vpu_irq_thread(int irq, void *dev_id)
{
	struct vpu_device *dev = dev_id;
	struct vpu_instance *inst;
	int irq_status, ret;

	while (kfifo_len(&dev->irq_status)) {
		ret = kfifo_out(&dev->irq_status, &irq_status, sizeof(int));
		if (!ret)
			break;

		if (irq_status & BIT(INT_WAVE6_REQ_WORK_BUF)) {
			if (!dev->ctrl)
				continue;
			/*firmware requires buffer*/
			wave6_vpu_ctrl_require_buffer(dev->ctrl, &dev->entity);
			continue;
		}

		if (irq_status & BIT(INT_WAVE6_ENC_SET_PARAM)) {
			complete(&dev->irq_done);
			continue;
		}
		if (irq_status & BIT(INT_WAVE6_INIT_SEQ)) {
			complete(&dev->irq_done);
			continue;
		}

		inst = v4l2_m2m_get_curr_priv(dev->m2m_dev);
		if (inst)
			inst->ops->finish_process(inst, irq_status);
		else
			complete(&dev->irq_done);
	}

	return IRQ_HANDLED;
}

static u32 wave6_vpu_read_reg(struct device *dev, u32 addr)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	return wave6_vdi_readl(vpu_dev, addr);
}

static void wave6_vpu_write_reg(struct device *dev, u32 addr, u32 data)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	wave6_vdi_writel(vpu_dev, addr, data);
}

static void wave6_vpu_on_boot(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	u32 product_code;
	u32 version;
	u32 revision;
	u32 hw_version;
	int ret;

	product_code = wave6_vdi_readl(vpu_dev, W6_VPU_RET_PRODUCT_VERSION);
	vpu_dev->product = wave_vpu_get_product_id(vpu_dev);

	wave6_enable_interrupt(vpu_dev);
	ret = wave6_vpu_get_version(vpu_dev, &version, &revision);
	if (ret) {
		dev_err(dev, "wave6_vpu_get_version fail\n");
		return;
	}

	hw_version = wave6_vdi_readl(vpu_dev, W6_RET_CONF_REVISION);

	if (vpu_dev->product_code != product_code ||
	    vpu_dev->fw_version != version ||
	    vpu_dev->fw_revision != revision ||
	    vpu_dev->hw_version != hw_version) {
		vpu_dev->product_code = product_code;
		vpu_dev->fw_version = version;
		vpu_dev->fw_revision = revision;
		vpu_dev->hw_version = hw_version;
		dev_info(dev,
			 "product: 0x%x, fw_version : v%d.%d.%d_g%08x(r%d), hw_version : 0x%x\n",
			 vpu_dev->product_code,
			 (version >> 24) & 0xFF,
			 (version >> 16) & 0xFF,
			 (version >> 0) & 0xFFFF,
			 wave6_vdi_readl(vpu_dev, W6_RET_SHA_ID),
			 revision,
			 vpu_dev->hw_version);
	}

	if (vpu_dev->res->compatible_fw_version > version)
		dev_err(dev,
			"compatible firmware version is v%d.%d.%d or higher, but only v%d.%d.%d\n",
			(vpu_dev->res->compatible_fw_version >> 24) & 0xFF,
			(vpu_dev->res->compatible_fw_version >> 16) & 0xFF,
			vpu_dev->res->compatible_fw_version & 0xFFFF,
			(version >> 24) & 0xFF,
			(version >> 16) & 0xFF,
			version & 0xFFFF);

	wave6_vpu_get_clk(vpu_dev);
}

void wave6_vpu_pause(struct device *dev, int resume)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	mutex_lock(&vpu_dev->pause_lock);
	if (resume) {
		vpu_dev->pause_request--;
		if (!vpu_dev->pause_request)
			v4l2_m2m_resume(vpu_dev->m2m_dev);
	} else {
		if (!vpu_dev->pause_request)
			v4l2_m2m_suspend(vpu_dev->m2m_dev);
		vpu_dev->pause_request++;
	}
	mutex_unlock(&vpu_dev->pause_lock);
}

void wave6_vpu_activate(struct vpu_device *dev)
{
	dev->active = true;
}

void wave6_vpu_wait_activated(struct vpu_device *dev)
{
	wave6_vpu_check_state(dev);
}

static int wave6_vpu_probe(struct platform_device *pdev)
{
	int ret;
	struct vpu_device *dev;
	const struct wave6_match_data *match_data;
	struct device_node *np;
	struct platform_device *pctrl;

	match_data = device_get_match_data(&pdev->dev);
	if (!match_data) {
		dev_err(&pdev->dev, "missing match_data\n");
		return -EINVAL;
	}

	/* physical addresses limited to 32 bits */
	dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->dev_lock);
	mutex_init(&dev->hw_lock);
	mutex_init(&dev->pause_lock);
	init_completion(&dev->irq_done);
	dev_set_drvdata(&pdev->dev, dev);
	dev->dev = &pdev->dev;
	dev->res = match_data;

	dev->entity.dev = dev->dev;
	dev->entity.read_reg = wave6_vpu_read_reg;
	dev->entity.write_reg = wave6_vpu_write_reg;
	dev->entity.on_boot = wave6_vpu_on_boot;
	dev->entity.pause = wave6_vpu_pause;

	dev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->reg_base))
		return PTR_ERR(dev->reg_base);

	np = of_parse_phandle(pdev->dev.of_node, "cnm,ctrl", 0);
	if (np) {
		pctrl = of_find_device_by_node(np);
		of_node_put(np);
		if (pctrl) {
			dev->ctrl = &pctrl->dev;
			if (wave6_vpu_ctrl_get_state(dev->ctrl) < 0) {
				dev_info(&pdev->dev, "vpu ctrl is not ready, defer probe\n");
				return -EPROBE_DEFER;
			}
		} else {
			dev_info(&pdev->dev, "vpu ctrl is not found\n");
			return -EINVAL;
		}
	} else {
		dev_info(&pdev->dev, "it's a follower vpu device\n");
	}

	ret = devm_clk_bulk_get_all(&pdev->dev, &dev->clks);
	if (ret < 0) {
		/* continue without clock, assume externally managed */
		dev_warn(&pdev->dev, "unable to get clocks: %d\n", ret);
		ret = 0;
	}
	dev->num_clks = ret;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "v4l2_device_register fail: %d\n", ret);
		return ret;
	}

	ret = wave6_vpu_init_m2m_dev(dev);
	if (ret)
		goto err_v4l2_unregister;

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0) {
		dev_err(&pdev->dev, "failed to get irq resource\n");
		ret = -ENXIO;
		goto err_m2m_dev_release;
	}

	if (kfifo_alloc(&dev->irq_status, 16 * sizeof(int), GFP_KERNEL)) {
		dev_err(&pdev->dev, "failed to allocate fifo\n");
		goto err_m2m_dev_release;
	}

	ret = devm_request_threaded_irq(&pdev->dev, dev->irq, wave6_vpu_irq,
					wave6_vpu_irq_thread, 0, "vpu_irq", dev);
	if (ret) {
		dev_err(&pdev->dev, "fail to register interrupt handler: %d\n", ret);
		goto err_kfifo_free;
	}

	dev->temp_vbuf.size = ALIGN(WAVE6_TEMPBUF_SIZE, 4096);
	ret = wave6_alloc_dma(dev->dev, &dev->temp_vbuf);
	if (ret) {
		dev_err(&pdev->dev, "alloc temp of size %zu failed\n", dev->temp_vbuf.size);
		goto err_kfifo_free;
	}

	dev->debugfs = debugfs_lookup(WAVE6_VPU_DEBUGFS_DIR, NULL);
	if (!dev->debugfs)
		dev->debugfs = debugfs_create_dir(WAVE6_VPU_DEBUGFS_DIR, NULL);

	pm_runtime_enable(&pdev->dev);

	if (dev->res->flags & WAVE6_IS_DEC) {
		ret = wave6_vpu_dec_register_device(dev);
		if (ret) {
			dev_err(&pdev->dev, "wave6_vpu_dec_register_device fail: %d\n", ret);
			goto err_temp_vbuf_free;
		}
	}

	if (dev->res->flags & WAVE6_IS_ENC) {
		ret = wave6_vpu_enc_register_device(dev);
		if (ret) {
			dev_err(&pdev->dev, "wave6_vpu_enc_register_device fail: %d\n", ret);
			goto err_dec_unreg;
		}
	}

	if (dev->ctrl && wave6_vpu_ctrl_support_follower(dev->ctrl)) {
		wave6_vpu_activate(dev);
		ret = pm_runtime_resume_and_get(dev->dev);
		if (ret)
			goto err_enc_unreg;
	}

	dev_dbg(&pdev->dev, "Added wave driver with caps %s %s\n",
		dev->res->flags & WAVE6_IS_ENC ? "'ENCODE'" : "",
		dev->res->flags & WAVE6_IS_DEC ? "'DECODE'" : "");

	return 0;

err_enc_unreg:
	if (dev->res->flags & WAVE6_IS_ENC)
		wave6_vpu_enc_unregister_device(dev);
err_dec_unreg:
	if (dev->res->flags & WAVE6_IS_DEC)
		wave6_vpu_dec_unregister_device(dev);
err_temp_vbuf_free:
	wave6_free_dma(&dev->temp_vbuf);
err_kfifo_free:
	kfifo_free(&dev->irq_status);
err_m2m_dev_release:
	wave6_vpu_release_m2m_dev(dev);
err_v4l2_unregister:
	v4l2_device_unregister(&dev->v4l2_dev);

	return ret;
}

static void wave6_vpu_remove(struct platform_device *pdev)
{
	struct vpu_device *dev = dev_get_drvdata(&pdev->dev);

	wave6_vpu_enc_unregister_device(dev);
	wave6_vpu_dec_unregister_device(dev);

	if (dev->ctrl && wave6_vpu_ctrl_support_follower(dev->ctrl)) {
		if (!pm_runtime_suspended(&pdev->dev))
			pm_runtime_put_sync(&pdev->dev);
	}
	pm_runtime_disable(&pdev->dev);

	wave6_free_dma(&dev->temp_vbuf);
	wave6_vpu_release_m2m_dev(dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	kfifo_free(&dev->irq_status);
}

#ifdef CONFIG_PM
static int wave6_vpu_runtime_suspend(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	if (!vpu_dev)
		return -ENODEV;

	dprintk(dev, "runtime suspend\n");
	if (vpu_dev->ctrl && vpu_dev->active)
		wave6_vpu_ctrl_put_sync(vpu_dev->ctrl, &vpu_dev->entity);
	if (vpu_dev->num_clks)
		clk_bulk_disable_unprepare(vpu_dev->num_clks, vpu_dev->clks);

	return 0;
}

static int wave6_vpu_runtime_resume(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	int ret = 0;

	if (!vpu_dev)
		return -ENODEV;

	dprintk(dev, "runtime resume\n");
	if (vpu_dev->num_clks) {
		ret = clk_bulk_prepare_enable(vpu_dev->num_clks, vpu_dev->clks);
		if (ret) {
			dev_err(dev, "failed to enable clocks: %d\n", ret);
			return ret;
		}
	}

	if (vpu_dev->ctrl && vpu_dev->active) {
		ret = wave6_vpu_ctrl_resume_and_get(vpu_dev->ctrl, &vpu_dev->entity);
		if (ret && vpu_dev->num_clks)
			clk_bulk_disable_unprepare(vpu_dev->num_clks, vpu_dev->clks);
	} else {
		wave6_vpu_check_state(vpu_dev);
	}

	return ret;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int wave6_vpu_suspend(struct device *dev)
{
	int ret;

	dprintk(dev, "suspend\n");
	wave6_vpu_pause(dev, 0);

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		wave6_vpu_pause(dev, 1);
	return ret;
}

static int wave6_vpu_resume(struct device *dev)
{
	int ret;

	dprintk(dev, "resume\n");
	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	wave6_vpu_pause(dev, 1);
	return 0;
}
#endif
static const struct dev_pm_ops wave6_vpu_pm_ops = {
	SET_RUNTIME_PM_OPS(wave6_vpu_runtime_suspend, wave6_vpu_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(wave6_vpu_suspend, wave6_vpu_resume)
};

static const struct of_device_id wave6_dt_ids[] = {
	{ .compatible = "fsl,cnm633c-vpu", .data = &wave633c_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wave6_dt_ids);

static struct platform_driver wave6_vpu_driver = {
	.driver = {
		.name = VPU_PLATFORM_DEVICE_NAME,
		.of_match_table = of_match_ptr(wave6_dt_ids),
		.pm = &wave6_vpu_pm_ops,
	},
	.probe = wave6_vpu_probe,
	.remove = wave6_vpu_remove,
	//.suspend = vpu_suspend,
	//.resume = vpu_resume,
};

module_platform_driver(wave6_vpu_driver);
MODULE_DESCRIPTION("chips&media VPU V4L2 driver");
MODULE_LICENSE("Dual BSD/GPL");

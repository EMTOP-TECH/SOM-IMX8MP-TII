// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2013-2014 Freescale Semiconductor, Inc
// Copyright (c) 2017 Sysam, Angelo Dureghello  <angelo@sysam.it>

#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/dmapool.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>

#include "fsl-edma-common.h"

#define EDMA_CR			0x00
#define EDMA_ES			0x04
#define EDMA_ERQ		0x0C
#define EDMA_EEI		0x14
#define EDMA_SERQ		0x1B
#define EDMA_CERQ		0x1A
#define EDMA_SEEI		0x19
#define EDMA_CEEI		0x18
#define EDMA_CINT		0x1F
#define EDMA_CERR		0x1E
#define EDMA_SSRT		0x1D
#define EDMA_CDNE		0x1C
#define EDMA_INTR		0x24
#define EDMA_ERR		0x2C

#define EDMA64_ERQH		0x08
#define EDMA64_EEIH		0x10
#define EDMA64_SERQ		0x18
#define EDMA64_CERQ		0x19
#define EDMA64_SEEI		0x1a
#define EDMA64_CEEI		0x1b
#define EDMA64_CINT		0x1c
#define EDMA64_CERR		0x1d
#define EDMA64_SSRT		0x1e
#define EDMA64_CDNE		0x1f
#define EDMA64_INTH		0x20
#define EDMA64_INTL		0x24
#define EDMA64_ERRH		0x28
#define EDMA64_ERRL		0x2c

void fsl_edma_tx_chan_handler(struct fsl_edma_chan *fsl_chan)
{
	spin_lock(&fsl_chan->vchan.lock);

	/* Ignore this interrupt since channel has been freeed with power off */
	if (!fsl_chan->edesc && !fsl_chan->tcd_pool) {
		spin_unlock(&fsl_chan->vchan.lock);
		return;
	}

	if (!fsl_chan->edesc) {
		/* terminate_all called before */
		spin_unlock(&fsl_chan->vchan.lock);
		return;
	}

	if (!fsl_chan->edesc->iscyclic) {
		fsl_edma_get_realcnt(fsl_chan);
		list_del(&fsl_chan->edesc->vdesc.node);
		vchan_cookie_complete(&fsl_chan->edesc->vdesc);
		fsl_chan->edesc = NULL;
		fsl_chan->status = DMA_COMPLETE;
	} else {
		vchan_cyclic_callback(&fsl_chan->edesc->vdesc);
	}

	if (!fsl_chan->edesc)
		fsl_edma_xfer_desc(fsl_chan);

	spin_unlock(&fsl_chan->vchan.lock);
}

static void fsl_edma3_enable_request(struct fsl_edma_chan *fsl_chan)
{
	u32 val, flags;

	flags = fsl_edma_drvflags(fsl_chan);
	val = edma_readl_chreg(fsl_chan, ch_sbr);
	if (fsl_chan->is_rxchan)
		val |= EDMA_V3_CH_SBR_RD;
	else
		val |= EDMA_V3_CH_SBR_WR;

	if (fsl_chan->is_remote)
		val &= ~(EDMA_V3_CH_SBR_RD | EDMA_V3_CH_SBR_WR);

	edma_writel_chreg(fsl_chan, val, ch_sbr);

	if (flags & FSL_EDMA_DRV_HAS_CHMUX) {
		/*
		 * ch_mux: With the exception of 0, attempts to write a value
		 * already in use will be forced to 0.
		 */
		if (!edma_readl(fsl_chan->edma, fsl_chan->mux_addr))
			edma_writel(fsl_chan->edma, fsl_chan->srcid, fsl_chan->mux_addr);
	}

	val = edma_readl_chreg(fsl_chan, ch_csr);
	val |= EDMA_V3_CH_CSR_ERQ | EDMA_V3_CH_CSR_EEI;
	edma_writel_chreg(fsl_chan, val, ch_csr);
}

static void fsl_edma_enable_request(struct fsl_edma_chan *fsl_chan)
{
	struct edma_regs *regs = &fsl_chan->edma->regs;
	u32 ch = fsl_chan->vchan.chan.chan_id;

	if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_SPLIT_REG)
		return fsl_edma3_enable_request(fsl_chan);

	if (fsl_chan->edma->drvdata->flags & FSL_EDMA_DRV_WRAP_IO) {
		edma_writeb(fsl_chan->edma, EDMA_SEEI_SEEI(ch), regs->seei);
		edma_writeb(fsl_chan->edma, ch, regs->serq);
	} else {
		/* ColdFire is big endian, and accesses natively
		 * big endian I/O peripherals
		 */
		iowrite8(EDMA_SEEI_SEEI(ch), regs->seei);
		iowrite8(ch, regs->serq);
	}
}

static void fsl_edma3_disable_request(struct fsl_edma_chan *fsl_chan)
{
	u32 val = edma_readl_chreg(fsl_chan, ch_csr);
	u32 flags;

	flags = fsl_edma_drvflags(fsl_chan);

	if (fsl_chan->srcid && (flags & FSL_EDMA_DRV_HAS_CHMUX))
		edma_writel(fsl_chan->edma, 0, fsl_chan->mux_addr);

	val &= ~EDMA_V3_CH_CSR_ERQ;
	edma_writel_chreg(fsl_chan, val, ch_csr);
}

void fsl_edma_disable_request(struct fsl_edma_chan *fsl_chan)
{
	struct edma_regs *regs = &fsl_chan->edma->regs;
	u32 ch = fsl_chan->vchan.chan.chan_id;

	if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_SPLIT_REG)
		return fsl_edma3_disable_request(fsl_chan);

	if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_WRAP_IO) {
		edma_writeb(fsl_chan->edma, ch, regs->cerq);
		edma_writeb(fsl_chan->edma, EDMA_CEEI_CEEI(ch), regs->ceei);
	} else {
		/* ColdFire is big endian, and accesses natively
		 * big endian I/O peripherals
		 */
		iowrite8(ch, regs->cerq);
		iowrite8(EDMA_CEEI_CEEI(ch), regs->ceei);
	}
}

static void mux_configure8(struct fsl_edma_chan *fsl_chan, void __iomem *addr,
			   u32 off, u32 slot, bool enable)
{
	u8 val8;

	if (enable)
		val8 = EDMAMUX_CHCFG_ENBL | slot;
	else
		val8 = EDMAMUX_CHCFG_DIS;

	iowrite8(val8, addr + off);
}

static void mux_configure32(struct fsl_edma_chan *fsl_chan, void __iomem *addr,
			    u32 off, u32 slot, bool enable)
{
	u32 val;

	if (enable)
		val = EDMAMUX_CHCFG_ENBL << 24 | slot;
	else
		val = EDMAMUX_CHCFG_DIS;

	iowrite32(val, addr + off * 4);
}

void fsl_edma_chan_mux(struct fsl_edma_chan *fsl_chan,
		       unsigned int slot, bool enable)
{
	u32 ch = fsl_chan->vchan.chan.chan_id;
	void __iomem *muxaddr;
	unsigned int chans_per_mux, ch_off;
	int endian_diff[4] = {3, 1, -1, -3};
	u32 dmamux_nr = fsl_chan->edma->drvdata->dmamuxs;

	if (!dmamux_nr)
		return;

	chans_per_mux = fsl_chan->edma->n_chans / dmamux_nr;
	ch_off = fsl_chan->vchan.chan.chan_id % chans_per_mux;

	if (fsl_chan->edma->drvdata->flags & FSL_EDMA_DRV_MUX_SWAP)
		ch_off += endian_diff[ch_off % 4];

	muxaddr = fsl_chan->edma->muxbase[ch / chans_per_mux];
	slot = EDMAMUX_CHCFG_SOURCE(slot);

	if (fsl_chan->edma->drvdata->flags & FSL_EDMA_DRV_CONFIG32)
		mux_configure32(fsl_chan, muxaddr, ch_off, slot, enable);
	else
		mux_configure8(fsl_chan, muxaddr, ch_off, slot, enable);
}

static unsigned int fsl_edma_get_tcd_attr(enum dma_slave_buswidth addr_width)
{
	u32 val;

	if (addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
		addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	val = ffs(addr_width) - 1;
	return val | (val << 8);
}

void fsl_edma_free_desc(struct virt_dma_desc *vdesc)
{
	struct fsl_edma_desc *fsl_desc;
	int i;

	fsl_desc = to_fsl_edma_desc(vdesc);
	for (i = 0; i < fsl_desc->n_tcds; i++)
		dma_pool_free(fsl_desc->echan->tcd_pool, fsl_desc->tcd[i].vtcd,
			      fsl_desc->tcd[i].ptcd);
	kfree(fsl_desc);
}

int fsl_edma_terminate_all(struct dma_chan *chan)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
	fsl_edma_disable_request(fsl_chan);
	fsl_chan->edesc = NULL;
	fsl_chan->status = DMA_COMPLETE;
	vchan_get_all_descriptors(&fsl_chan->vchan, &head);
	spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);
	vchan_dma_desc_free_list(&fsl_chan->vchan, &head);

	return 0;
}

int fsl_edma_pause(struct dma_chan *chan)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
	if (fsl_chan->edesc) {
		fsl_edma_disable_request(fsl_chan);
		fsl_chan->status = DMA_PAUSED;
	}
	spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);
	return 0;
}

int fsl_edma_resume(struct dma_chan *chan)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
	if (fsl_chan->edesc) {
		fsl_edma_enable_request(fsl_chan);
		fsl_chan->status = DMA_IN_PROGRESS;
	}
	spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);
	return 0;
}

static void fsl_edma_unprep_slave_dma(struct fsl_edma_chan *fsl_chan)
{
	if (fsl_chan->dma_dir != DMA_NONE)
		dma_unmap_resource(fsl_chan->vchan.chan.device->dev,
				   fsl_chan->dma_dev_addr,
				   fsl_chan->dma_dev_size,
				   fsl_chan->dma_dir, 0);
	fsl_chan->dma_dir = DMA_NONE;
}

static bool fsl_edma_prep_slave_dma(struct fsl_edma_chan *fsl_chan,
				    enum dma_transfer_direction dir)
{
	struct device *dev = fsl_chan->vchan.chan.device->dev;
	enum dma_data_direction dma_dir;
	phys_addr_t addr = 0;
	u32 size = 0;

	switch (dir) {
	case DMA_MEM_TO_DEV:
		dma_dir = DMA_FROM_DEVICE;
		addr = fsl_chan->cfg.dst_addr;
		size = fsl_chan->cfg.dst_maxburst;
		break;
	case DMA_DEV_TO_MEM:
		dma_dir = DMA_TO_DEVICE;
		addr = fsl_chan->cfg.src_addr;
		size = fsl_chan->cfg.src_maxburst;
		break;
	default:
		dma_dir = DMA_NONE;
		break;
	}

	/* Already mapped for this config? */
	if (fsl_chan->dma_dir == dma_dir)
		return true;

	fsl_edma_unprep_slave_dma(fsl_chan);

	fsl_chan->dma_dev_addr = dma_map_resource(dev, addr, size, dma_dir, 0);
	if (dma_mapping_error(dev, fsl_chan->dma_dev_addr))
		return false;
	fsl_chan->dma_dev_size = size;
	fsl_chan->dma_dir = dma_dir;

	return true;
}

int fsl_edma_slave_config(struct dma_chan *chan,
				 struct dma_slave_config *cfg)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);

	memcpy(&fsl_chan->cfg, cfg, sizeof(*cfg));
	fsl_edma_unprep_slave_dma(fsl_chan);

	return 0;
}

static size_t fsl_edma_desc_residue(struct fsl_edma_chan *fsl_chan,
		struct virt_dma_desc *vdesc, bool in_progress)
{
	struct fsl_edma_desc *edesc = fsl_chan->edesc;
	enum dma_transfer_direction dir = edesc->dirn;
	dma_addr_t cur_addr, dma_addr, old_addr;
	size_t len, size;
	u32 nbytes = 0;
	int i;

	/* calculate the total size in this desc */
	for (len = i = 0; i < fsl_chan->edesc->n_tcds; i++) {
		nbytes = fsl_edma_get_tcd_to_cpu(fsl_chan, edesc->tcd[i].vtcd, nbytes);
		if (nbytes & (EDMA_V3_TCD_NBYTES_DMLOE | EDMA_V3_TCD_NBYTES_SMLOE))
			nbytes = EDMA_V3_TCD_NBYTES_MLOFF_NBYTES(nbytes);
		len += nbytes * fsl_edma_get_tcd_to_cpu(fsl_chan, edesc->tcd[i].vtcd, biter);
	}

	if (!in_progress)
		return len;

	/* 64bit read is not atomic, need read retry when high 32bit changed */
	do {
		if (dir == DMA_MEM_TO_DEV) {
			old_addr = edma_read_tcdreg(fsl_chan, saddr);
			cur_addr = edma_read_tcdreg(fsl_chan, saddr);
		} else {
			old_addr = edma_read_tcdreg(fsl_chan, daddr);
			cur_addr = edma_read_tcdreg(fsl_chan, daddr);
		}
	} while (upper_32_bits(cur_addr) != upper_32_bits(old_addr));

	/* figure out the finished and calculate the residue */
	for (i = 0; i < fsl_chan->edesc->n_tcds; i++) {
		nbytes = fsl_edma_get_tcd_to_cpu(fsl_chan, edesc->tcd[i].vtcd, nbytes);
		if (nbytes & (EDMA_V3_TCD_NBYTES_DMLOE | EDMA_V3_TCD_NBYTES_SMLOE))
			nbytes = EDMA_V3_TCD_NBYTES_MLOFF_NBYTES(nbytes);

		size = nbytes * fsl_edma_get_tcd_to_cpu(fsl_chan, edesc->tcd[i].vtcd, biter);

		if (dir == DMA_MEM_TO_DEV)
			dma_addr = fsl_edma_get_tcd_to_cpu(fsl_chan, edesc->tcd[i].vtcd, saddr);
		else
			dma_addr = fsl_edma_get_tcd_to_cpu(fsl_chan, edesc->tcd[i].vtcd, daddr);

		len -= size;
		if (cur_addr >= dma_addr && cur_addr < dma_addr + size) {
			len += dma_addr + size - cur_addr;
			break;
		}
	}

	return len;
}

void fsl_edma_get_realcnt(struct fsl_edma_chan *fsl_chan)
{
	fsl_chan->chn_real_count = fsl_edma_desc_residue(fsl_chan, NULL, true);
}

enum dma_status fsl_edma_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	struct virt_dma_desc *vdesc;
	enum dma_status status;
	unsigned long flags;

	status = dma_cookie_status(chan, cookie, txstate);
	if (status == DMA_COMPLETE) {
		spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
		txstate->residue = fsl_chan->chn_real_count;
		spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);
		return status;
	}

	if (!txstate)
		return fsl_chan->status;

	spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
	vdesc = vchan_find_desc(&fsl_chan->vchan, cookie);
	if (fsl_chan->edesc && cookie == fsl_chan->edesc->vdesc.tx.cookie)
		txstate->residue =
			fsl_edma_desc_residue(fsl_chan, vdesc, true);
	else if (vdesc)
		txstate->residue =
			fsl_edma_desc_residue(fsl_chan, vdesc, false);
	else
		txstate->residue = 0;

	spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);

	return fsl_chan->status;
}

void fsl_edma_set_tcd_regs(struct fsl_edma_chan *fsl_chan, void *tcd)
{
	u16 csr = 0;

	/*
	 * TCD parameters are stored in struct fsl_edma_hw_tcd in little
	 * endian format. However, we need to load the TCD registers in
	 * big- or little-endian obeying the eDMA engine model endian,
	 * and this is performed from specific edma_write functions
	 */
	edma_write_tcdreg(fsl_chan, 0, csr);

	edma_cp_tcd_to_reg(fsl_chan, tcd, saddr);
	edma_cp_tcd_to_reg(fsl_chan, tcd, daddr);

	edma_cp_tcd_to_reg(fsl_chan, tcd, attr);
	edma_cp_tcd_to_reg(fsl_chan, tcd, soff);

	edma_cp_tcd_to_reg(fsl_chan, tcd, nbytes);
	edma_cp_tcd_to_reg(fsl_chan, tcd, slast);

	edma_cp_tcd_to_reg(fsl_chan, tcd, citer);
	edma_cp_tcd_to_reg(fsl_chan, tcd, biter);
	edma_cp_tcd_to_reg(fsl_chan, tcd, doff);

	edma_cp_tcd_to_reg(fsl_chan, tcd, dlast_sga);

	csr = fsl_edma_get_tcd_to_cpu(fsl_chan, tcd, csr);

	if (fsl_chan->is_sw) {
		csr |= EDMA_TCD_CSR_START;
		fsl_edma_set_tcd_to_le(fsl_chan, tcd, csr, csr);
	}

	/*
	 * Must clear CHn_CSR[DONE] bit before enable TCDn_CSR[ESG] at EDMAv3
	 * eDMAv4 have not such requirement.
	 * Change MLINK need clear CHn_CSR[DONE] for both eDMAv3 and eDMAv4.
	 */
	if (((fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_CLEAR_DONE_E_SG) &&
		(csr & EDMA_TCD_CSR_E_SG)) ||
	    ((fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_CLEAR_DONE_E_LINK) &&
		(csr & EDMA_TCD_CSR_E_LINK)))
		edma_writel_chreg(fsl_chan, edma_readl_chreg(fsl_chan, ch_csr), ch_csr);


	edma_cp_tcd_to_reg(fsl_chan, tcd, csr);
}

static inline
void fsl_edma_fill_tcd(struct fsl_edma_chan *fsl_chan,
		       struct fsl_edma_hw_tcd *tcd, dma_addr_t src, dma_addr_t dst,
		       u16 attr, u16 soff, u32 nbytes, dma_addr_t slast, u16 citer,
		       u16 biter, u16 doff, dma_addr_t dlast_sga, bool major_int,
		       bool disable_req, bool enable_sg)
{
	struct dma_slave_config *cfg = &fsl_chan->cfg;
	u16 csr = 0;
	u32 burst;

	/*
	 * eDMA hardware SGs require the TCDs to be stored in little
	 * endian format irrespective of the register endian model.
	 * So we put the value in little endian in memory, waiting
	 * for fsl_edma_set_tcd_regs doing the swap.
	 */
	fsl_edma_set_tcd_to_le(fsl_chan, tcd, src, saddr);
	fsl_edma_set_tcd_to_le(fsl_chan, tcd, dst, daddr);

	fsl_edma_set_tcd_to_le(fsl_chan, tcd, attr, attr);

	fsl_edma_set_tcd_to_le(fsl_chan, tcd, soff, soff);

	if (fsl_chan->is_multi_fifo) {
		/* set mloff to support multiple fifo */
		burst = cfg->direction == DMA_DEV_TO_MEM ?
				cfg->src_maxburst : cfg->dst_maxburst;
		nbytes |= EDMA_V3_TCD_NBYTES_MLOFF(-(burst * 4));
		/* enable DMLOE/SMLOE */
		if (cfg->direction == DMA_MEM_TO_DEV) {
			nbytes |= EDMA_V3_TCD_NBYTES_DMLOE;
			nbytes &= ~EDMA_V3_TCD_NBYTES_SMLOE;
		} else {
			nbytes |= EDMA_V3_TCD_NBYTES_SMLOE;
			nbytes &= ~EDMA_V3_TCD_NBYTES_DMLOE;
		}
	}

	fsl_edma_set_tcd_to_le(fsl_chan, tcd, nbytes, nbytes);
	fsl_edma_set_tcd_to_le(fsl_chan, tcd, slast, slast);

	fsl_edma_set_tcd_to_le(fsl_chan, tcd, EDMA_TCD_CITER_CITER(citer), citer);
	fsl_edma_set_tcd_to_le(fsl_chan, tcd, doff, doff);

	fsl_edma_set_tcd_to_le(fsl_chan, tcd, dlast_sga, dlast_sga);

	fsl_edma_set_tcd_to_le(fsl_chan, tcd, EDMA_TCD_BITER_BITER(biter), biter);

	if (major_int)
		csr |= EDMA_TCD_CSR_INT_MAJOR;

	if (disable_req)
		csr |= EDMA_TCD_CSR_D_REQ;

	if (enable_sg)
		csr |= EDMA_TCD_CSR_E_SG;

	if (fsl_chan->is_rxchan)
		csr |= EDMA_TCD_CSR_ACTIVE;

	if (fsl_chan->is_sw)
		csr |= EDMA_TCD_CSR_START;

	fsl_edma_set_tcd_to_le(fsl_chan, tcd, csr, csr);

	trace_edma_fill_tcd(fsl_chan, tcd);
}

static struct fsl_edma_desc *fsl_edma_alloc_desc(struct fsl_edma_chan *fsl_chan,
		int sg_len)
{
	struct fsl_edma_desc *fsl_desc;
	int i;

	fsl_desc = kzalloc(struct_size(fsl_desc, tcd, sg_len), GFP_NOWAIT);
	if (!fsl_desc)
		return NULL;

	fsl_desc->echan = fsl_chan;
	fsl_desc->n_tcds = sg_len;
	for (i = 0; i < sg_len; i++) {
		fsl_desc->tcd[i].vtcd = dma_pool_alloc(fsl_chan->tcd_pool,
					GFP_NOWAIT, &fsl_desc->tcd[i].ptcd);
		if (!fsl_desc->tcd[i].vtcd)
			goto err;
	}
	return fsl_desc;

err:
	while (--i >= 0)
		dma_pool_free(fsl_chan->tcd_pool, fsl_desc->tcd[i].vtcd,
				fsl_desc->tcd[i].ptcd);
	kfree(fsl_desc);
	return NULL;
}

struct dma_async_tx_descriptor *fsl_edma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t dma_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	struct fsl_edma_desc *fsl_desc;
	dma_addr_t dma_buf_next;
	bool major_int = true;
	int sg_len, i;
	dma_addr_t src_addr, dst_addr, last_sg;
	u16 soff, doff, iter;
	u32 nbytes;

	if (!is_slave_direction(direction))
		return NULL;

	if (!fsl_edma_prep_slave_dma(fsl_chan, direction))
		return NULL;

	sg_len = buf_len / period_len;
	fsl_desc = fsl_edma_alloc_desc(fsl_chan, sg_len);
	if (!fsl_desc)
		return NULL;
	fsl_desc->iscyclic = true;
	fsl_desc->dirn = direction;

	dma_buf_next = dma_addr;
	if (direction == DMA_MEM_TO_DEV) {
		fsl_chan->attr =
			fsl_edma_get_tcd_attr(fsl_chan->cfg.dst_addr_width);
		nbytes = fsl_chan->cfg.dst_addr_width *
			fsl_chan->cfg.dst_maxburst;
	} else {
		fsl_chan->attr =
			fsl_edma_get_tcd_attr(fsl_chan->cfg.src_addr_width);
		nbytes = fsl_chan->cfg.src_addr_width *
			fsl_chan->cfg.src_maxburst;
	}

	iter = period_len / nbytes;

	for (i = 0; i < sg_len; i++) {
		if (dma_buf_next >= dma_addr + buf_len)
			dma_buf_next = dma_addr;

		/* get next sg's physical address */
		last_sg = fsl_desc->tcd[(i + 1) % sg_len].ptcd;

		if (direction == DMA_MEM_TO_DEV) {
			src_addr = dma_buf_next;
			dst_addr = fsl_chan->dma_dev_addr;
			soff = fsl_chan->cfg.dst_addr_width;
			doff = fsl_chan->is_multi_fifo ? 4 : 0;
		} else if (direction == DMA_DEV_TO_MEM) {
			src_addr = fsl_chan->dma_dev_addr;
			dst_addr = dma_buf_next;
			soff = fsl_chan->is_multi_fifo ? 4 : 0;
			doff = fsl_chan->cfg.src_addr_width;
		} else {
			/* DMA_DEV_TO_DEV */
			src_addr = fsl_chan->cfg.src_addr;
			dst_addr = fsl_chan->cfg.dst_addr;
			soff = doff = 0;
			major_int = false;
		}

		fsl_edma_fill_tcd(fsl_chan, fsl_desc->tcd[i].vtcd, src_addr, dst_addr,
				  fsl_chan->attr, soff, nbytes, 0, iter,
				  iter, doff, last_sg, major_int, false, true);
		dma_buf_next += period_len;
	}

	return vchan_tx_prep(&fsl_chan->vchan, &fsl_desc->vdesc, flags);
}

struct dma_async_tx_descriptor *fsl_edma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	struct fsl_edma_desc *fsl_desc;
	struct scatterlist *sg;
	dma_addr_t src_addr, dst_addr, last_sg;
	u16 soff, doff, iter;
	u32 nbytes;
	int i;

	if (!is_slave_direction(direction))
		return NULL;

	if (!fsl_edma_prep_slave_dma(fsl_chan, direction))
		return NULL;

	fsl_desc = fsl_edma_alloc_desc(fsl_chan, sg_len);
	if (!fsl_desc)
		return NULL;
	fsl_desc->iscyclic = false;
	fsl_desc->dirn = direction;

	if (direction == DMA_MEM_TO_DEV) {
		fsl_chan->attr =
			fsl_edma_get_tcd_attr(fsl_chan->cfg.dst_addr_width);
		nbytes = fsl_chan->cfg.dst_addr_width *
			fsl_chan->cfg.dst_maxburst;
	} else {
		fsl_chan->attr =
			fsl_edma_get_tcd_attr(fsl_chan->cfg.src_addr_width);
		nbytes = fsl_chan->cfg.src_addr_width *
			fsl_chan->cfg.src_maxburst;
	}

	for_each_sg(sgl, sg, sg_len, i) {
		if (direction == DMA_MEM_TO_DEV) {
			src_addr = sg_dma_address(sg);
			dst_addr = fsl_chan->dma_dev_addr;
			soff = fsl_chan->cfg.dst_addr_width;
			doff = 0;
		} else if (direction == DMA_DEV_TO_MEM) {
			src_addr = fsl_chan->dma_dev_addr;
			dst_addr = sg_dma_address(sg);
			soff = 0;
			doff = fsl_chan->cfg.src_addr_width;
		} else {
			/* DMA_DEV_TO_DEV */
			src_addr = fsl_chan->cfg.src_addr;
			dst_addr = fsl_chan->cfg.dst_addr;
			soff = 0;
			doff = 0;
		}

		/*
		 * Choose the suitable burst length if sg_dma_len is not
		 * multiple of burst length so that the whole transfer length is
		 * multiple of minor loop(burst length).
		 */
		if (sg_dma_len(sg) % nbytes) {
			u32 width = (direction == DMA_DEV_TO_MEM) ? doff : soff;
			u32 burst = (direction == DMA_DEV_TO_MEM) ?
						fsl_chan->cfg.src_maxburst :
						fsl_chan->cfg.dst_maxburst;
			int j;

			for (j = burst; j > 1; j--) {
				if (!(sg_dma_len(sg) % (j * width))) {
					nbytes = j * width;
					break;
				}
			}
			/* Set burst size as 1 if there's no suitable one */
			if (j == 1)
				nbytes = width;
		}
		iter = sg_dma_len(sg) / nbytes;
		if (i < sg_len - 1) {
			last_sg = fsl_desc->tcd[(i + 1)].ptcd;
			fsl_edma_fill_tcd(fsl_chan, fsl_desc->tcd[i].vtcd, src_addr,
					  dst_addr, fsl_chan->attr, soff,
					  nbytes, 0, iter, iter, doff, last_sg,
					  false, false, true);
		} else {
			last_sg = 0;
			fsl_edma_fill_tcd(fsl_chan, fsl_desc->tcd[i].vtcd, src_addr,
					  dst_addr, fsl_chan->attr, soff,
					  nbytes, 0, iter, iter, doff, last_sg,
					  true, true, false);
		}
	}

	return vchan_tx_prep(&fsl_chan->vchan, &fsl_desc->vdesc, flags);
}

struct dma_async_tx_descriptor *fsl_edma_prep_memcpy(struct dma_chan *chan,
						     dma_addr_t dma_dst, dma_addr_t dma_src,
						     size_t len, unsigned long flags)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	struct fsl_edma_desc *fsl_desc;

	fsl_desc = fsl_edma_alloc_desc(fsl_chan, 1);
	if (!fsl_desc)
		return NULL;
	fsl_desc->iscyclic = false;

	fsl_chan->is_sw = true;
	if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_MEM_REMOTE)
		fsl_chan->is_remote = true;

	/* To match with copy_align and max_seg_size so 1 tcd is enough */
	fsl_edma_fill_tcd(fsl_chan, fsl_desc->tcd[0].vtcd, dma_src, dma_dst,
			fsl_edma_get_tcd_attr(DMA_SLAVE_BUSWIDTH_32_BYTES),
			32, len, 0, 1, 1, 32, 0, true, true, false);

	return vchan_tx_prep(&fsl_chan->vchan, &fsl_desc->vdesc, flags);
}

void fsl_edma_xfer_desc(struct fsl_edma_chan *fsl_chan)
{
	struct virt_dma_desc *vdesc;

	lockdep_assert_held(&fsl_chan->vchan.lock);

	vdesc = vchan_next_desc(&fsl_chan->vchan);
	if (!vdesc)
		return;
	fsl_chan->edesc = to_fsl_edma_desc(vdesc);
	fsl_edma_set_tcd_regs(fsl_chan, fsl_chan->edesc->tcd[0].vtcd);
	fsl_edma_enable_request(fsl_chan);
	fsl_chan->status = DMA_IN_PROGRESS;
}

void fsl_edma_issue_pending(struct dma_chan *chan)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&fsl_chan->vchan.lock, flags);

	if (unlikely(fsl_chan->pm_state != RUNNING)) {
		spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);
		/* cannot submit due to suspend */
		return;
	}

	if (vchan_issue_pending(&fsl_chan->vchan) && !fsl_chan->edesc)
		fsl_edma_xfer_desc(fsl_chan);

	spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);

}

int fsl_edma_alloc_chan_resources(struct dma_chan *chan)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	int ret;

	if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_HAS_CHCLK)
		clk_prepare_enable(fsl_chan->clk);

	if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_HAS_PD)
		pm_runtime_get_sync(fsl_chan->pd_dev);

	fsl_chan->tcd_pool = dma_pool_create("tcd_pool", chan->device->dev,
				fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_TCD64 ?
				sizeof(struct fsl_edma_hw_tcd64) : sizeof(struct fsl_edma_hw_tcd),
				32, 0);

	if (fsl_chan->txirq) {
		ret = request_irq(fsl_chan->txirq, fsl_chan->irq_handler, IRQF_SHARED,
				 fsl_chan->chan_name, fsl_chan);

		if (ret) {
			dma_pool_destroy(fsl_chan->tcd_pool);
			if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_HAS_PD)
				pm_runtime_put_sync_suspend(fsl_chan->pd_dev);

			return ret;
		}

		if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_EDMA3) {
			if (!(fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_ERRIRQ_SHARE)) {
				ret = request_irq(fsl_chan->errirq, fsl_chan->errirq_handler,
						  IRQF_SHARED, fsl_chan->errirq_name, fsl_chan);

				if (ret) {
					dma_pool_destroy(fsl_chan->tcd_pool);
					if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_HAS_PD)
						pm_runtime_put_sync_suspend(fsl_chan->pd_dev);

					return ret;
				}
			}
		}
	}

	return 0;
}

void fsl_edma_free_chan_resources(struct dma_chan *chan)
{
	struct fsl_edma_chan *fsl_chan = to_fsl_edma_chan(chan);
	struct fsl_edma_engine *edma = fsl_chan->edma;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
	fsl_edma_disable_request(fsl_chan);
	if (edma->drvdata->dmamuxs)
		fsl_edma_chan_mux(fsl_chan, 0, false);
	fsl_chan->edesc = NULL;
	vchan_get_all_descriptors(&fsl_chan->vchan, &head);
	fsl_edma_unprep_slave_dma(fsl_chan);
	spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);

	if (fsl_chan->txirq)
		free_irq(fsl_chan->txirq, fsl_chan);
	if (fsl_chan->errirq)
		free_irq(fsl_chan->errirq, fsl_chan);

	vchan_dma_desc_free_list(&fsl_chan->vchan, &head);
	dma_pool_destroy(fsl_chan->tcd_pool);
	fsl_chan->tcd_pool = NULL;
	fsl_chan->is_sw = false;
	fsl_chan->srcid = 0;
	fsl_chan->is_remote = false;
	if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_HAS_CHCLK)
		clk_disable_unprepare(fsl_chan->clk);

	if (fsl_edma_drvflags(fsl_chan) & FSL_EDMA_DRV_HAS_PD)
		pm_runtime_put_sync_suspend(fsl_chan->pd_dev);
}

void fsl_edma_cleanup_vchan(struct dma_device *dmadev)
{
	struct fsl_edma_chan *chan, *_chan;

	list_for_each_entry_safe(chan, _chan,
				&dmadev->channels, vchan.chan.device_node) {
		list_del(&chan->vchan.chan.device_node);
		tasklet_kill(&chan->vchan.task);
	}
}

/*
 * On the 32 channels Vybrid/mpc577x edma version, register offsets are
 * different compared to ColdFire mcf5441x 64 channels edma.
 *
 * This function sets up register offsets as per proper declared version
 * so must be called in xxx_edma_probe() just after setting the
 * edma "version" and "membase" appropriately.
 */
void fsl_edma_setup_regs(struct fsl_edma_engine *edma)
{
	bool is64 = !!(edma->drvdata->flags & FSL_EDMA_DRV_EDMA64);

	edma->regs.cr = edma->membase + EDMA_CR;
	edma->regs.es = edma->membase + EDMA_ES;
	edma->regs.erql = edma->membase + EDMA_ERQ;
	edma->regs.eeil = edma->membase + EDMA_EEI;

	edma->regs.serq = edma->membase + (is64 ? EDMA64_SERQ : EDMA_SERQ);
	edma->regs.cerq = edma->membase + (is64 ? EDMA64_CERQ : EDMA_CERQ);
	edma->regs.seei = edma->membase + (is64 ? EDMA64_SEEI : EDMA_SEEI);
	edma->regs.ceei = edma->membase + (is64 ? EDMA64_CEEI : EDMA_CEEI);
	edma->regs.cint = edma->membase + (is64 ? EDMA64_CINT : EDMA_CINT);
	edma->regs.cerr = edma->membase + (is64 ? EDMA64_CERR : EDMA_CERR);
	edma->regs.ssrt = edma->membase + (is64 ? EDMA64_SSRT : EDMA_SSRT);
	edma->regs.cdne = edma->membase + (is64 ? EDMA64_CDNE : EDMA_CDNE);
	edma->regs.intl = edma->membase + (is64 ? EDMA64_INTL : EDMA_INTR);
	edma->regs.errl = edma->membase + (is64 ? EDMA64_ERRL : EDMA_ERR);

	if (is64) {
		edma->regs.erqh = edma->membase + EDMA64_ERQH;
		edma->regs.eeih = edma->membase + EDMA64_EEIH;
		edma->regs.errh = edma->membase + EDMA64_ERRH;
		edma->regs.inth = edma->membase + EDMA64_INTH;
	}
}

MODULE_LICENSE("GPL v2");

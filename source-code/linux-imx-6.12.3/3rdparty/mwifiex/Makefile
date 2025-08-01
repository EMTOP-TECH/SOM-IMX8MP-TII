#  File: Makefile
#
#  Copyright 2008-2024 NXP
#
#  This software file (the File) is distributed by NXP
#  under the terms of the GNU General Public License Version 2, June 1991
#  (the License).  You may use, redistribute and/or modify the File in
#  accordance with the terms and conditions of the License, a copy of which
#  is available by writing to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
#  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
#
#  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
#  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
#  this warranty disclaimer.
#

CONFIG_COMPATDIR=n
ifeq ($(CONFIG_COMPATDIR), y)
COMPATDIR=/lib/modules/$(KERNELVERSION_X86)/build/compat-wireless-3.2-rc1-1/include
CC ?=		$(CROSS_COMPILE)gcc -I$(COMPATDIR)
endif

LD ?=		$(CROSS_COMPILE)ld
BACKUP=		/root/backup
YMD=		`date +%Y%m%d%H%M`

#############################################################################
# Configuration Options
#############################################################################
# Multi-chipsets
CONFIG_SD8887=n
CONFIG_SD8897=n
CONFIG_USB8897=n
CONFIG_PCIE8897=n
CONFIG_SD8977=n
CONFIG_SD8978=y
CONFIG_USB8978=n
CONFIG_SD8997=y
CONFIG_USB8997=n
CONFIG_PCIE8997=y
CONFIG_SD8987=y
CONFIG_SD9097=n
CONFIG_SD9177=y
CONFIG_SD8801=y
CONFIG_USB8801=n
CONFIG_USB9097=n
CONFIG_PCIE9097=n
CONFIG_SD9098=y
CONFIG_USB9098=n
CONFIG_PCIE9098=y
CONFIG_SDIW610=y
CONFIG_USBIW610=n
CONFIG_SDIW624=n
CONFIG_SDAW693=n
CONFIG_PCIEIW624=n
CONFIG_USBIW624=n
CONFIG_PCIEAW693=n


# Debug Option
# DEBUG LEVEL n/1/2:
# n: NO DEBUG
# 1: Only PRINTM(MMSG,...), PRINTM(MFATAL,...), ...
# 2: All PRINTM()
CONFIG_DEBUG=1

# Enable STA mode support
CONFIG_STA_SUPPORT=y

# Enable uAP mode support
CONFIG_UAP_SUPPORT=y

# Enable WIFIDIRECT support
CONFIG_WIFI_DIRECT_SUPPORT=y


# Re-association in driver
CONFIG_REASSOCIATION=y


# Manufacturing firmware support
CONFIG_MFG_CMD_SUPPORT=y

# OpenWrt support
CONFIG_OPENWRT_SUPPORT=n

# Big-endian platform
CONFIG_BIG_ENDIAN=n




# SDIO suspend/resume
CONFIG_SDIO_SUSPEND_RESUME=y

# DFS testing support
CONFIG_DFS_TESTING_SUPPORT=y

# Multi-channel support
CONFIG_MULTI_CHAN_SUPPORT=y



CONFIG_DUMP_TO_PROC=y

CONFIG_TASKLET_SUPPORT=n



#32bit app over 64bit kernel support
CONFIG_USERSPACE_32BIT_OVER_KERNEL_64BIT=n

GCC_VERSION := $(shell echo `gcc -dumpversion | cut -f1-2 -d.` \>= 4.4 | sed -e 's/\./*100+/g' | bc )
ifeq ($(GCC_VERSION),1)
        ccflags-y += -Wno-packed-bitfield-compat
endif
WimpGCC_VERSION := $(shell echo `gcc -dumpversion | cut -f1 -d.`| bc )
ifeq ($(shell test $(WimpGCC_VERSION) -ge 7; echo $$?),0)
ccflags-y += -Wimplicit-fallthrough=3
endif
#ccflags-y += -Wunused-but-set-variable
#ccflags-y += -Wmissing-prototypes
#ccflags-y += -Wold-style-definition
#ccflags-y += -Wtype-limits
#ccflags-y += -Wsuggest-attribute=format
#ccflags-y += -Wmissing-include-dirs
#ccflags-y += -Wshadow
#ccflags-y += -Wsign-compare
#ccflags-y += -Wunused-macros
#ccflags-y += -Wmissing-field-initializers
#ccflags-y += -Wstringop-truncation
#ccflags-y += -Wmisleading-indentation
#ccflags-y += -Wunused-const-variable

#############################################################################
# Select Platform Tools
#############################################################################

ifeq ($(ANDROID_BUILD), 1)
    KERNEL_CFLAGS += -DANDROID
    PWD := $(shell pwd)
    KERNELDIR ?= $(KERNEL_SRC)
    ccflags-y += -DANDROID_SDK_VERSION=$(ANDROID_SDK_VERSION)
endif

MODEXT = ko
ccflags-y += -I$(PWD)/mlan
ccflags-y += -DLINUX






ARCH ?= arm64
CONFIG_IMX_SUPPORT=y
ifeq ($(CONFIG_IMX_SUPPORT),y)
ccflags-y += -DIMX_SUPPORT
ifneq ($(ANDROID_PRODUCT_OUT),)
ccflags-y += -DIMX_ANDROID
ccflags-y += -Wno-implicit-fallthrough
CONFIG_ANDROID_KERNEL=y
endif
endif

LD += -S

BINDIR = bin_wlan
APPDIR= $(shell if test -d "mapp"; then echo mapp; fi)

#############################################################################
# Compiler Flags
#############################################################################

	ccflags-y += -I$(KERNELDIR)/include
	ccflags-y += -DMLAN_RELEASE_VERSION='"505.p14"'

	ccflags-y += -DFPNUM='"92"'

ifeq ($(CONFIG_DEBUG),1)
	ccflags-y += -DDEBUG_LEVEL1
endif

ifeq ($(CONFIG_DEBUG),2)
	ccflags-y += -DDEBUG_LEVEL1
	ccflags-y += -DDEBUG_LEVEL2
	DBG=	-dbg
endif

ifeq ($(CONFIG_64BIT), y)
	ccflags-y += -DMLAN_64BIT
endif

ifeq ($(CONFIG_STA_SUPPORT),y)
	ccflags-y += -DSTA_SUPPORT
ifeq ($(CONFIG_REASSOCIATION),y)
	ccflags-y += -DREASSOCIATION
endif
else
CONFIG_WIFI_DIRECT_SUPPORT=n
CONFIG_STA_WEXT=n
CONFIG_STA_CFG80211=n
endif

ifeq ($(CONFIG_UAP_SUPPORT),y)
	ccflags-y += -DUAP_SUPPORT
else
CONFIG_WIFI_DIRECT_SUPPORT=n
CONFIG_UAP_WEXT=n
CONFIG_UAP_CFG80211=n
endif

ifeq ($(CONFIG_WIFI_DIRECT_SUPPORT),y)
	ccflags-y += -DWIFI_DIRECT_SUPPORT
endif

ifeq ($(CONFIG_MFG_CMD_SUPPORT),y)
	ccflags-y += -DMFG_CMD_SUPPORT
endif

ifeq ($(CONFIG_BIG_ENDIAN),y)
	ccflags-y += -DBIG_ENDIAN_SUPPORT
endif

ifeq ($(CONFIG_USERSPACE_32BIT_OVER_KERNEL_64BIT),y)
	ccflags-y += -DUSERSPACE_32BIT_OVER_KERNEL_64BIT
endif

ifeq ($(CONFIG_SDIO_SUSPEND_RESUME),y)
	ccflags-y += -DSDIO_SUSPEND_RESUME
endif

ifeq ($(CONFIG_MULTI_CHAN_SUPPORT),y)
	ccflags-y += -DMULTI_CHAN_SUPPORT
endif

ifeq ($(CONFIG_DFS_TESTING_SUPPORT),y)
	ccflags-y += -DDFS_TESTING_SUPPORT
endif


ifeq ($(CONFIG_ANDROID_KERNEL), y)
	ccflags-y += -DANDROID_KERNEL
	CONFIG_DUMP_TO_PROC=y
endif

ifeq ($(CONFIG_DUMP_TO_PROC), y)
	ccflags-y += -DDUMP_TO_PROC
endif

ifeq ($(CONFIG_TASKLET_SUPPORT), y)
	ccflags-y += -DTASKLET_SUPPORT
endif

ifeq ($(CONFIG_OPENWRT_SUPPORT), y)
	ccflags-y += -DOPENWRT
endif

ifeq ($(CONFIG_T50), y)
	ccflags-y += -DT50
	ccflags-y += -DT40
	ccflags-y += -DT3T
endif

ifeq ($(CONFIG_SD8887),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD8887
endif
ifeq ($(CONFIG_SD8897),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD8897
endif
ifeq ($(CONFIG_SD8977),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD8977
endif
ifeq ($(CONFIG_SD8978),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD8978
endif
ifeq ($(CONFIG_SD8997),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD8997
endif
ifeq ($(CONFIG_SD8987),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD8987
endif
ifeq ($(CONFIG_SD9097),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD9097
endif
ifeq ($(CONFIG_SDIW610),y)
	CONFIG_SDIO=y
	ccflags-y += -DSDIW610
endif
ifeq ($(CONFIG_SDIW624),y)
	CONFIG_SDIO=y
	ccflags-y += -DSDIW624
endif
ifeq ($(CONFIG_SDAW693),y)
	CONFIG_SDIO=y
	ccflags-y += -DSDAW693
endif
ifeq ($(CONFIG_SD9177),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD9177
endif
ifeq ($(CONFIG_SD8801),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD8801
endif
ifeq ($(CONFIG_SD9098),y)
	CONFIG_SDIO=y
	ccflags-y += -DSD9098
endif
ifeq ($(CONFIG_USB8801),y)
	CONFIG_MUSB=y
	ccflags-y += -DUSB8801
endif
ifeq ($(CONFIG_USB8897),y)
	CONFIG_MUSB=y
	ccflags-y += -DUSB8897
endif
ifeq ($(CONFIG_USB8997),y)
	CONFIG_MUSB=y
	ccflags-y += -DUSB8997
endif
ifeq ($(CONFIG_USB8978),y)
	CONFIG_MUSB=y
	ccflags-y += -DUSB8978
endif
ifeq ($(CONFIG_USB9097),y)
	CONFIG_MUSB=y
	ccflags-y += -DUSB9097
endif
ifeq ($(CONFIG_USBIW610),y)
	CONFIG_MUSB=y
	ccflags-y += -DUSBIW610
endif
ifeq ($(CONFIG_USBIW624),y)
	CONFIG_MUSB=y
	ccflags-y += -DUSBIW624
endif
ifeq ($(CONFIG_USB9098),y)
	CONFIG_MUSB=y
	ccflags-y += -DUSB9098
endif
ifeq ($(CONFIG_PCIE8897),y)
	CONFIG_PCIE=y
	ccflags-y += -DPCIE8897
endif
ifeq ($(CONFIG_PCIE8997),y)
	CONFIG_PCIE=y
	ccflags-y += -DPCIE8997
endif
ifeq ($(CONFIG_PCIE9097),y)
	CONFIG_PCIE=y
	ccflags-y += -DPCIE9097
endif
ifeq ($(CONFIG_PCIE9098),y)
	CONFIG_PCIE=y
	ccflags-y += -DPCIE9098
endif
ifeq ($(CONFIG_PCIEIW624),y)
	CONFIG_PCIE=y
	ccflags-y += -DPCIEIW624
endif
ifeq ($(CONFIG_PCIEAW693),y)
	CONFIG_PCIE=y
	ccflags-y += -DPCIEAW693
endif
ifeq ($(CONFIG_SDIO),y)
	ccflags-y += -DSDIO
	ccflags-y += -DSDIO_MMC
endif
ifeq ($(CONFIG_MUSB),y)
	ccflags-y += -DUSB
endif
ifeq ($(CONFIG_PCIE),y)
	ccflags-y += -DPCIE
endif

ifeq ($(CONFIG_MAC80211_SUPPORT),y)
	ccflags-y += -DMAC80211_SUPPORT
endif
ifeq ($(CONFIG_MAC80211_SUPPORT_UAP),y)
	ccflags-y += -DMAC80211_SUPPORT_UAP
endif
ifeq ($(CONFIG_MAC80211_SUPPORT_MESH),y)
	ccflags-y += -DMAC80211_SUPPORT_MESH
endif

#############################################################################
# Make Targets
#############################################################################

ifneq ($(KERNELRELEASE),)

ifeq ($(CONFIG_WIRELESS_EXT),y)
ifeq ($(CONFIG_WEXT_PRIV),y)
	# Enable WEXT for STA
	CONFIG_STA_WEXT=y
	# Enable WEXT for uAP
	CONFIG_UAP_WEXT=y
else
# Disable WEXT for STA
	CONFIG_STA_WEXT=n
# Disable WEXT for uAP
	CONFIG_UAP_WEXT=n
endif
endif

# Enable CFG80211 for STA
ifeq ($(CONFIG_CFG80211),y)
	CONFIG_STA_CFG80211=y
else
ifeq ($(CONFIG_CFG80211),m)
	CONFIG_STA_CFG80211=y
else
	CONFIG_STA_CFG80211=n
endif
endif

# OpenWrt
ifeq ($(CONFIG_OPENWRT_SUPPORT), y)
ifeq ($(CPTCFG_CFG80211),y)
	CONFIG_STA_CFG80211=y
else
ifeq ($(CPTCFG_CFG80211),m)
	CONFIG_STA_CFG80211=y
else
	CONFIG_STA_CFG80211=n
endif
endif
endif

# Enable CFG80211 for uAP
ifeq ($(CONFIG_CFG80211),y)
	CONFIG_UAP_CFG80211=y
else
ifeq ($(CONFIG_CFG80211),m)
	CONFIG_UAP_CFG80211=y
else
	CONFIG_UAP_CFG80211=n
endif
endif

# OpenWrt
ifeq ($(CONFIG_OPENWRT_SUPPORT), y)
ifeq ($(CPTCFG_CFG80211),y)
	CONFIG_STA_CFG80211=y
else
ifeq ($(CPTCFG_CFG80211),m)
	CONFIG_STA_CFG80211=y
else
	CONFIG_STA_CFG80211=n
endif
endif
endif

ifneq ($(CONFIG_STA_SUPPORT),y)
	CONFIG_WIFI_DIRECT_SUPPORT=n
	CONFIG_STA_WEXT=n
	CONFIG_STA_CFG80211=n
endif

ifneq ($(CONFIG_UAP_SUPPORT),y)
	CONFIG_WIFI_DIRECT_SUPPORT=n
	CONFIG_UAP_WEXT=n
	CONFIG_UAP_CFG80211=n
endif

ifeq ($(CONFIG_STA_SUPPORT),y)
ifeq ($(CONFIG_STA_WEXT),y)
	ccflags-y += -DSTA_WEXT
endif
ifeq ($(CONFIG_STA_CFG80211),y)
	ccflags-y += -DSTA_CFG80211
endif
endif
ifeq ($(CONFIG_UAP_SUPPORT),y)
ifeq ($(CONFIG_UAP_WEXT),y)
	ccflags-y += -DUAP_WEXT
endif
ifeq ($(CONFIG_UAP_CFG80211),y)
	ccflags-y += -DUAP_CFG80211
endif
endif

print:
ifeq ($(CONFIG_STA_SUPPORT),y)
ifeq ($(CONFIG_STA_WEXT),n)
ifeq ($(CONFIG_STA_CFG80211),n)
	@echo "Can not build STA without WEXT or CFG80211"
	exit 2
endif
endif
endif
ifeq ($(CONFIG_UAP_SUPPORT),y)
ifeq ($(CONFIG_UAP_WEXT),n)
ifeq ($(CONFIG_UAP_CFG80211),n)
	@echo "Can not build UAP without WEXT or CFG80211"
	exit 2
endif
endif
endif





MOALOBJS =	mlinux/moal_main.o \
		mlinux/moal_ioctl.o \
		mlinux/moal_shim.o \
		mlinux/moal_eth_ioctl.o \
		mlinux/moal_init.o

MLANOBJS =	mlan/mlan_shim.o mlan/mlan_init.o \
		mlan/mlan_txrx.o \
		mlan/mlan_cmdevt.o mlan/mlan_misc.o \
		mlan/mlan_cfp.o \
		mlan/mlan_module.o

MLANOBJS += mlan/mlan_wmm.o
ifeq ($(CONFIG_MUSB),y)
MLANOBJS += mlan/mlan_usb.o
endif
ifeq ($(CONFIG_SDIO),y)
MLANOBJS += mlan/mlan_sdio.o
endif
ifeq ($(CONFIG_PCIE),y)
MLANOBJS += mlan/mlan_pcie.o
endif
MLANOBJS += mlan/mlan_11n_aggr.o
MLANOBJS += mlan/mlan_11n_rxreorder.o
MLANOBJS += mlan/mlan_11n.o
MLANOBJS += mlan/mlan_11ac.o
MLANOBJS += mlan/mlan_11ax.o
MLANOBJS += mlan/mlan_11d.o
MLANOBJS += mlan/mlan_11h.o
ifeq ($(CONFIG_STA_SUPPORT),y)
MLANOBJS += mlan/mlan_meas.o
MLANOBJS += mlan/mlan_scan.o \
			mlan/mlan_sta_ioctl.o \
			mlan/mlan_sta_rx.o \
			mlan/mlan_sta_tx.o \
			mlan/mlan_sta_event.o \
			mlan/mlan_sta_cmd.o \
			mlan/mlan_sta_cmdresp.o \
			mlan/mlan_join.o
ifeq ($(CONFIG_STA_WEXT),y)
MOALOBJS += mlinux/moal_priv.o \
            mlinux/moal_wext.o
endif
endif
ifeq ($(CONFIG_UAP_SUPPORT),y)
MLANOBJS += mlan/mlan_uap_ioctl.o
MLANOBJS += mlan/mlan_uap_cmdevent.o
MLANOBJS += mlan/mlan_uap_txrx.o
MOALOBJS += mlinux/moal_uap.o
ifeq ($(CONFIG_UAP_WEXT),y)
MOALOBJS += mlinux/moal_uap_priv.o
MOALOBJS += mlinux/moal_uap_wext.o
endif
endif
ifeq ($(CONFIG_STA_CFG80211),y)
MOALOBJS += mlinux/moal_cfg80211.o
MOALOBJS += mlinux/moal_cfg80211_util.o
MOALOBJS += mlinux/moal_sta_cfg80211.o
endif
ifeq ($(CONFIG_UAP_CFG80211),y)
MOALOBJS += mlinux/moal_cfg80211.o
MOALOBJS += mlinux/moal_cfg80211_util.o
MOALOBJS += mlinux/moal_uap_cfg80211.o
endif

ifdef CONFIG_PROC_FS
MOALOBJS += mlinux/moal_proc.o
MOALOBJS += mlinux/moal_debug.o
endif

ifeq ($(CONFIG_MAC80211_SUPPORT),y)
MOALOBJS += mlinux/moal_mac80211.o
MLANOBJS += mlan/mlan_mac80211.o
endif







obj-m := mlan.o
mlan-objs := $(MLANOBJS)

ifeq ($(CONFIG_MUSB),y)
MOALOBJS += mlinux/moal_usb.o
endif
ifeq ($(CONFIG_SDIO),y)
MOALOBJS += mlinux/moal_sdio_mmc.o
endif
ifeq ($(CONFIG_PCIE),y)
MOALOBJS += mlinux/moal_pcie.o
endif
obj-m += moal.o
moal-objs := $(MOALOBJS)

# Otherwise we were called directly from the command line; invoke the kernel build system.
else

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

endif

###############################################################

export		CC LD ccflags-y KERNELDIR

.PHONY: mapp/mlanutl clean distclean
	@echo "Finished Making NXP Wlan Linux Driver"

mapp/mlanutl:
	$(MAKE) -C $@

echo:

appsbuild:

	@if [ ! -d $(BINDIR) ]; then \
		mkdir $(BINDIR); \
	fi

	cp -f README $(BINDIR)

ifneq ($(APPDIR),)
	cp -rf mapp/mlanconfig/config $(BINDIR)
	$(MAKE) -C mapp/mlanutl $@ INSTALLDIR=$(BINDIR)
endif

build:		echo default

	@if [ ! -d $(BINDIR) ]; then \
		mkdir $(BINDIR); \
	fi

	cp -f mlan.$(MODEXT) $(BINDIR)/mlan$(DBG).$(MODEXT)

	cp -f moal.$(MODEXT) $(BINDIR)/moal$(DBG).$(MODEXT)

	cp -f README $(BINDIR)

ifneq ($(APPDIR),)
	cp -rf mapp/mlanconfig/config $(BINDIR)
	$(MAKE) -C mapp/mlanutl $@ INSTALLDIR=$(BINDIR)
endif

clean:
	-find . -name "*.o" -exec rm {} \;
	-find . -name "*.ko" -exec rm {} \;
	-find . -name ".*.cmd" -exec rm {} \;
	-find . -name "*.mod.c" -exec rm {} \;
	-find . -name "Module.symvers" -exec rm {} \;
	-find . -name "Module.markers" -exec rm {} \;
	-find . -name "modules.order" -exec rm {} \;
	-find . -name ".*.dwo" -exec rm {} \;
	-find . -name "*dwo" -exec rm {} \;
	-rm -rf .tmp_versions
ifneq ($(APPDIR),)
	$(MAKE) -C mapp/mlanutl $@
endif
#ifdef SDIO
#endif // SDIO

install: default

	cp -f mlan.$(MODEXT) $(INSTALLDIR)/mlan$(DBG).$(MODEXT)
	cp -f moal.$(MODEXT) $(INSTALLDIR)/moal$(DBG).$(MODEXT)
	echo $(INSTALLDIR)
	echo "MX Driver Installed"

distclean:
	-find . -name "*.o" -exec rm {} \;
	-find . -name "*.orig" -exec rm {} \;
	-find . -name "*.swp" -exec rm {} \;
	-find . -name "*.*~" -exec rm {} \;
	-find . -name "*~" -exec rm {} \;
	-find . -name "*.d" -exec rm {} \;
	-find . -name "*.a" -exec rm {} \;
	-find . -name "tags" -exec rm {} \;
	-find . -name ".*" -exec rm -rf 2> /dev/null \;
	-find . -name "*.ko" -exec rm {} \;
	-find . -name ".*.cmd" -exec rm {} \;
	-find . -name "*.mod.c" -exec rm {} \;
	-find . -name ".*.dwo" -exec rm {} \;
	-find . -name "*dwo" -exec rm {} \;
	-rm -rf .tmp_versions
ifneq ($(APPDIR),)
	$(MAKE) -C mapp/mlanutl $@
endif

# End of file

#!/bin/bash

# AUTHOR: Shenzhen EMTOP Tech. @ 20250618

export PATH=/opt/bin/arm/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin:$PATH
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

DESTDIR="/dev/shm/"

corenum=`grep -c processor /proc/cpuinfo`
to_build_dtbs=false
to_build_modules=false
kernelver=""
cur_dir=$(cd "$(dirname "$0")"; pwd)

case "$2" in
    "dtbs" )
        to_build_dtbs=true;;
    "modules")
        to_build_modules=true;;
esac

build_cryptodev_drv() {
    export KERNEL_DIR="$cur_dir"
    dir="3rdparty/cryptodev-linux"

    cd $cur_dir/$dir
    [ $? != 0 ] && exit 1
    make -j$corenum
    [ $? != 0 ] && exit 1

    for d in $DESTDIR; do
        dir="$d/lib/modules/$kernelver"
        ! [ -d "$dir" ] && continue
        dir="$d/lib/modules/$kernelver/extra/cryptodev"
        ! [ -d "$dir" ] && mkdir -p $dir
        ko_list=`find . -name "*.ko"`
        for ko in $ko_list; do
            dirnm=`dirname $ko`
            ! [ -d "$dir/$dirnm" ] && mkdir -p $dir/$dirnm
            echo "Info: COPY $ko -> $dir/$ko"
            rsync -qavH $ko $dir/$ko
            [ $? != 0 ] && exit 1
        done
    done
    make clean >/dev/null 2>&1
}

build_vvcam_drv() {
    export ARCH_TYPE=arm64
    export KERNEL_SRC="$cur_dir"
    dir="3rdparty/isp-vvcam/vvcam/v4l2"

    cd $cur_dir/$dir
    [ $? != 0 ] && exit 1
    make -j$corenum
    [ $? != 0 ] && exit 1

    for d in $DESTDIR; do
        dir="$d/lib/modules/$kernelver"
        ! [ -d "$dir" ] && continue
        dir="$d/lib/modules/$kernelver/extra/vvcam"
        ! [ -d "$dir" ] && mkdir -p $dir
        ko_list=`find . -name "*.ko"`
        for ko in $ko_list; do
            dirnm=`dirname $ko`
            ! [ -d "$dir/$dirnm" ] && mkdir -p $dir/$dirnm
            echo "Info: COPY $ko -> $dir/$ko"
            rsync -qavH $ko $dir/$ko
            [ $? != 0 ] && exit 1
        done
    done
    make clean >/dev/null 2>&1
}

build_wifi_drv() {
    export KERNELDIR="$cur_dir"
    dir="3rdparty/mwifiex"

    cd $cur_dir/$dir
    [ $? != 0 ] && exit 1
    make -j$corenum
    [ $? != 0 ] && exit 1

    for d in $DESTDIR; do
        dir="$d/lib/modules/$kernelver"
        ! [ -d "$dir" ] && continue
        dir="$d/lib/modules/$kernelver/extra/mwifiex"
        ! [ -d "$dir" ] && mkdir -p $dir
        ko_list=`find . -name "*.ko"`
        for ko in $ko_list; do
            dirnm=`dirname $ko`
            ! [ -d "$dir/$dirnm" ] && mkdir -p $dir/$dirnm
            echo "Info: COPY $ko -> $dir/$ko"
            rsync -qavH $ko $dir/$ko
            [ $? != 0 ] && exit 1
        done
    done
    make distclean >/dev/null 2>&1
}

build_3rdparty_drv() {
    build_wifi_drv
    build_vvcam_drv
    build_cryptodev_drv

    for d in $DESTDIR; do
        dir="$d/lib/modules/$kernelver/extra"
        if [ -d "$dir" ]; then
            depmod -b $d $kernelver
            [ $? != 0 ] && exit 1
        fi
    done
}

build_imx8mp_tii() {
    SRC0DTB="arch/arm64/boot/dts/freescale/imx8mp-emtop-tii.dtb"
    DST0DTB="imx8mp-emtop-tii.dtb"
    SRCKER="arch/arm64/boot/Image"
    DSTKER="Image"

    SRCDTBS=($SRC0DTB $SRC1DTB $SRC2DTB)
    DSTDTBS=($DST0DTB $DST1DTB $DST2DTB)

    unset BUILDDTBS
    for i in ${SRCDTBS[@]};do BUILDDTBS=`echo $BUILDDTBS`" freescale/"$(basename $i); done

    if ! [ -f ".config" ]; then
        make emtop_imx8m_defconfig
        [ $? != 0 ] && exit 1
    fi

    kernelver=`make kernelversion`

    if [ "$to_build_dtbs" = true ]; then
        make $BUILDDTBS -j$corenum
        [ $? != 0 ] && exit 1
    elif [ "$to_build_modules" = true ]; then
        make $BUILDDTBS Image modules -j$corenum
        [ $? != 0 ] && exit 1
        make INSTALL_MOD_PATH=/dev/shm/ modules_install
        [ $? != 0 ] && exit 1

        build_3rdparty_drv
    else
        make $BUILDDTBS Image -j$corenum
        [ $? != 0 ] && exit 1
    fi
    for d in $DESTDIR; do
        ! [ -d "$d" ] && continue
        for ((i = 0; i < ${#SRCDTBS[@]}; i++)) {
            echo "Info: COPY ${SRCDTBS[$i]} -> ${d}/${DSTDTBS[$i]}"
            cp -f $cur_dir/${SRCDTBS[$i]} ${d}/${DSTDTBS[$i]}
        }
        echo "Info: COPY ${SRCKER} ->  ${d}/${DSTKER}"
        cp -f $cur_dir/${SRCKER} ${d}/${DSTKER}
    done
}

#-------------------------------------------------------
create_yocto_patch() {
    VER=`make -C $cur_dir kernelversion`
    DATE=`/usr/bin/date +%Y%m%d`
    PATCH="linux-imx-$VER-emtop-$DATE.patch"

    cp -f arch/arm64/configs/emtop_imx8m_defconfig arch/arm64/configs/imx_v8_defconfig
    [ $? != 0 ] && exit 1

    for d in $DESTDIR; do
        ! [ -d "$d" ] && continue
        echo "Info: Generate Upstream-Status..."
        echo -e "Upstream-Status: Submitted\n" > ${d}/$PATCH
        echo "Info: Generate ${d}/${PATCH}"
        git diff 33b36b7da -- . ':!3rdparty' ':!make.sh' >> ${d}/$PATCH
        [ $? != 0 ] && exit 1
    done

    # restore
    git checkout arch/arm64/configs/imx_v8_defconfig
}

#-------------------------------------------------------
print_usage() {
    echo "$1 <supported_board> [dtbs | modules]"
    echo
    echo "[supported_board]:"
    echo "  imx8mp-tii      - IMX8MP-TII"
    echo
    echo "  create-patch    - Create patch for Yocto L6.12.3"
    echo
}

# main entry
case "$1" in
    "imx8mp-tii")
        build_imx8mp_tii;;

    "create-patch")
        create_yocto_patch;;
    *)
        print_usage $0;;
esac

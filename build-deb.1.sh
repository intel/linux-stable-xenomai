#SPDX-License-Identifier: MIT

D_KC=meta_kconfigs
D_OUT=.out/

### parse parameters
if [ "$1" == "--help" ]; then
	echo "bash $0                  ### build - without clean previous output"
	echo "bash $0 --clean-build    ### build - clean previous output before build"
	echo "bash $0 --clean-all      ### clean - clean previous output then exit"
	echo "bash $0 --help           ### help"
	exit
fi

if [ "$1" == "--clean-all" ]; then
	rm *.deb
	rm *.buildinfo
	rm *.changes
	make mrproper O=
	make mrproper
	rm -rf ${D_OUT}
	exit
fi

### https://www.kernel.org/doc/html/latest/kbuild/kconfig.html,kbuild.html
if [ "$1" == "--clean-build" ]; then
	echo "will clean previous output before build."
	make mrproper
	rm -rf ${D_OUT}
fi

mkdir -p ${D_OUT}
export KBUILD_OUTPUT=$D_OUT && printf "KBUILD_OUTPUT=$KBUILD_OUTPUT\n"

if [[ $KCONFIG_CONFIG != "" ]]; then
	echo "rename KCONFIG_CONFIG is not support."
	exit
fi

#make mrproper

./scripts/kconfig/merge_config.sh -m -r -O ${D_KC}\
	${D_KC}/x86_64_base_defconfig \
	${D_KC}/realtime.1.cfg ${D_KC}/tcc.cfg ${D_KC}/misc.cfg \
	${D_KC}/xenomai.cfg

cp ${D_KC}/.config $KBUILD_OUTPUT && sleep 1

make olddefconfig && sleep 1

###make bindeb-pkg LOCALVERSION=xenomai_local-test-1 KDEB_PKGVERSION=1 -j$(nproc --all) 2>&1 | tee .build.log
make bindeb-pkg KDEB_PKGVERSION=1 -j$(nproc --all) 2>&1 | tee .build.log


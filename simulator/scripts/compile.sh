#!/bin/bash
ZSIMPATH=$(pwd)
PINPATH="$ZSIMPATH/pin"
LIBCONFIGPATH="$ZSIMPATH/libconfig"
#DRAMSIMPATH="$ZSIMPATH/DRAMSim2"
DRAMSIMPATH="/home/basel/PIMSimulator/src"
RAMULATORPATH="$ZSIMPATH/ramulator"
NUMCPUS=$(grep -c ^processor /proc/cpuinfo)
HDF5PATH="/home/basel/Downloads/hdf5-1.8.4-patch1-linux-x86_64-shared"


if [ "$1" = "z" ]
then
	echo "Compiling only ZSim ..."
        export PINPATH
        export RAMULATORPATH
        export DRAMSIMPATH
        export LIBCONFIGPATH
	    export HDF5PATH
        scons -j$NUMCPUS

elif [ "$1" = "r" ]
then
	echo "Compiling only Ramulator ..."
        export RAMULATORPATH
        cd $RAMULATORPATH
        make libramulator.so
        cd ..

elif [ "$1" = "c" ]
then
    echo "Cleaning compilation ..."
         export PINPATH
         export DRAMSIMPATH
         export LIBCONFIGPATH
         export HDF5PATH
         cd $DRAMSIMPATH/..
         scons -c
         cd -
         scons -c
elif [ "$1" = "p" ]
 then
     echo "Compiling with PIMSim ..."
         export PINPATH
         export RAMULATORPATH
         export DRAMSIMPATH
         export LIBCONFIGPATH
         export HDF5PATH
         cd $DRAMSIMPATH/..
         scons NO_EMUL=1
         cd -
         scons -j$NUMCPUS
else
	echo "Compiling all ..."
	export LIBCONFIGPATH
	export HDF5PATH
	cd $LIBCONFIGPATH
	./configure --prefix=$LIBCONFIGPATH && make install
	cd ..


	export RAMULATORPATH
	cd $RAMULATORPATH
	make libramulator.so
	cd ..

	export PINPATH
	scons -j$NUMCPUS
fi

#These are the steps to compile the included software
#make sure to have 'build-essential' software already 
#installed. Debian/*Buntu: sudo apt update && install build-essential
#feel free to customize this script to your liking or cherry pick
#if you can't get this script to run, make sure to run:
# chmod +x build.sh rebuild.sh start.sh detector.sh
#then run: ./build.sh to start script.

gcc -o dot-detector dot-detector.c
gcc -o ledacs-esk ledacs-esk.c
gcc -o ledacs-esk-analyzer ledacs-esk-analyzer.c
tar -xvf rtl-sdr-master-udp.tar.xz
cd rtl-sdr-master-udp/rtl-sdr-master
mkdir build
cd build
cmake ..
make
cd ..
cd ..
cd ..


rtl-sdr-master-udp/rtl-sdr-master/build/src/rtl_udp -d 1 -f 851.8M -s 28.8k -p 0.5 -g 49 | tee >(sox -t raw -b 16 -e signed-integer -r 28800 -c 1 - -t raw - vol 2 sinc 0.2k-4.5k -a 110 rate 48000 | aplay -t raw -f S16_LE -r 48000 -c 1) | ./dot-detector

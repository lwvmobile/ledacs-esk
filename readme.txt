Forked from https://github.com/sp5wwp/ledacs

LEDACS-ESK is a command line system written and modified to trunk track
EDACS ESK systems. This software was forked from aforementioned LEDACS
found at the link provided above, but having diverged too far from the source
it is most likely not going to be incorporated into the original project.
LEDACS-ESK can be run for legacy EDACS systems, but its current working
status for other variants of EDACS is still unknown.

LEDACS-ESK uses standard rtl_fm for the trunk tracking ledacs-esk program
which only decifers the EDACS ESK control channel and provides controls to
the second program, DOT-DETECTOR

DOT-DETECTOR is used in conjunction with rtl_udp, a fork of RTL_SDR which
allows for remote control input from another software via UDP ports. Port 6020
is the default port this software uses. In future releases, it is hopeful to
move away from RTL_UDP in favor of another solution, as RTL_UDP seems to be
an outdated and perhaps *dead* project in its own right, but still works 
well enough for the current scope of this project.

LEDACS-ESK requires two dongles if you wish to track the control channel
and tune the voice channels. 

LEDACS-ESK has currently been testing and known to run on Ubuntu 18.04 based
desktop distros (Ubuntu, LM19, Zorin 15, etc) as well as Raspberri Pi
3b+ hardware running Raspian Buster. Many more are likely to be added as soon
as they can be properly tested. 

Installation:

Make sure to have the prerequisite sox, aplay, cmake and build-essential installed.
You will also need rtl-sdr already installed as well.
Raspberry Pi Raspian users will also need libusb-1.0.0-dev

sudo apt update
sudo apt install sox aplay build-essential cmake
sudo apt install rtl-sdr

#Additional For Raspberry Pi users# 
sudo apt install libusb-1.0.0-dev

If you haven't already, please extract the tarball or zip file into
whichever directory you wish. For the tarball:

tar -xvf LEDACS-ESK.tar.xz
cd LEDACS-ESK

After installing the prerequisite software and extracting 
the archive into your directory of choice, please run:

chmod +x build.sh rebuild.sh start.sh detector.sh standalone.sh

This command will give the necessary execution permissions to our scripts for
building and quickly starting up the software without needing to remember
long strings of code.

Next, run the included build.sh script to compile all the code necesary.


./build.sh

This process will compile ledacs-esk and dot-detector, extract, cmake and make
the rtl-sdr-udp required. Make install is not run, and is not recommended to
be installed as it may cause issues, overwrite, or conflict with RTL_SDR.
RTL_UDP will be called and executed in its own folder where it was made.

Usage:

First, you will need to make a simple text file with LCN channel numbers entered
in Hz. For example, in the site243 file included you will find:

851375000
851800000
855987500
858487500

Listed out seperated by line with no decimals or engineering notation. This format is
required for now. The LCN channels will need to be in order from 1,2,3, until the end.
This list, and LEDACS-ESK currently has a limit of 32 LCN Channels per file. 

Next, we will view the start.sh file, this is where the commands required for running
LEDACS-ESK will be, and we will simply execute the start.sh file to start the software.

The contents of a typical file will be:

rtl_fm -d 0 -f 851375000 -s 28.8k -p 0.5 -g 49 | ./ledacs-esk site243 1 1 0 allow deny

First, we will specify 
-d 0 as our RTL of choice, 
-f 851375000, tune it to our control channel frequency
-s 28.88k to specify the sample rate of 28.8k THIS VALUE MUST REMAIN 28.8k, DO NOT
        CHANGE THIS VALUE AS IT DIRECTLY CORRELATES TO THE bitrate of EDACS systems.
        THE SOFTWARE WILL NOT WORK IF THIS VALUE IS CHANGED. DON'T TOUCH IT!!
 
-p 0.5 will specify our PPM correction, adjust as required for your device.
-g 49 specifies a gain value of 49 (the max) you may adjust this as neccesary,
      or omit gain to have it set automatically. 

Please see man rtl_fm for more information on fine tuning options available.
REPEAT, DO NOT CHANGE THE SAMPLE RATE. LEAVE AS -s 28.8k.

On the other side, we see this is piped into our software. Currently, you need
to specify the file name with the LCN channels, the first (1) specifies LCN 1
as control, the second (1) specifies EDACS-ESK, which can be changed to 2 for legacy,
and the (0) specifies debug verbosity levels. allow and deny refer to files which can
be populated with group numbers in decimal format for the purpose of allow or deny of
voice channel assignment. Currently, allow overrides everything in deny, having information
in both will default to allow.

Currently, all of these arguments are required, otherwise a segmentation fault will occur.
Sorry :( 

See ./ledacs-esk -h for more info.

Then, when you have all values set appropriately in the start.sh file, simply execute it
with the following command:

./start.sh

Example Output:

If you should get this software running properly on an EDACS ESK system(Fingers Crossed)
you will find typical output can include the following.

Sockets successfully initialized
Found 2 device(s):
  0:  Realtek, RTL2838UHIDIR, SN: 00000001
  1:  Realtek, RTL2838UHIDIR, SN: 00000002

Using device 0: Generic RTL2832U OEM
Found Rafael Micro R820T tuner
Tuner gain set to 49.60 dB.
Tuned to 851627000 Hz.
Oversampling input by: 35x.
Oversampling output by: 1x.
Buffer size: 8.13ms
Exact sample rate is: 1008000.009613 Hz
Sampling at 1008000 S/s.
Output at 28800 Hz.
LCN[1]=851375000Hz
LCN[2]=851800000Hz
LCN[3]=855987500Hz
LCN[4]=858487500Hz
CC=LCN[1]
Time: 15:44:10  AFC=1786 	IDLE 	Status=[0xF] 	Site ID=[243]
Time: 15:44:20  AFC=1860 	IDLE 	Status=[0xF] 	Site ID=[243]
Time: 15:44:30  AFC=1933 	IDLE 	Status=[0xF] 	Site ID=[243]

First, we see that RTL_FM initializes, sets its setting as appropriate, then
LEDACS-ESK lists our LCN channels in our example site243 file, and we are greeted
with a rolling IDLE status, AFC (number of samples processed), Status bits in hex,
and Site ID in decimal form.

For EDACS 9600, The closer the AFC value comes to 2400, the better/cleaner the signal.
Typical AFC numbers can vary, anything around 1800 or higher should be good enough.

When the software receives VOICE commands from the EDACS signal, we will begin to see

Time: 15:46:47  AFC=1994	VOICE	Status=[0xF] 	LCN=4
Sender=[ 199401i]
Group=[  1155g]
Time: 15:46:51  AFC=2043	VOICE	Status=[0xF] 	LCN=2
Sender=[ 197868i]
Time: 15:47:05  AFC=2009	VOICE	Status=[0xF] 	LCN=4	PVT 
Sender=[ 197868i]
Group=[  1155g]

First, we see the current time, the VOICE status, status bits, LCN channel number, and on the next
line we see the SENDER and GROUP ID.

Note: You may also wish to use ./standalone as opposed to ./start, using the same parameters.
This uses the ledacs-esk-standalone version which only tracks the control channel, but does
not attempt to tune frequencies. This may be preferred if you only have or want to use one dongle.
Raspberry Pi repo version of rtl-sdr includes UDP functionality, which using the ./start method
may cause the program to attempt to tune its own instance of rtl_fm if ./detector hasn't launched yet.
Please be sure to run ./detector first, or use the ./standalone if you only have one dongle.

Dot Detector:

After we have verified we have an active control channel, we can look at the script to 
start up Dot-Detector in conjuction with rtl_udp, this software will listen to the output
of LEDACS-ESK and tune to the appropriate voice channel.

Below is our example ./detector.sh script:

rtl-sdr-master-upd/rtl-sdr-master/build/src/rtl_udp -d 1 -f 851.375M -s 28.8k -p 0.5 -g 49 | tee >(sox -t raw -b 16 -e signed-integer -r 28800 -c 1 - -t raw - vol 2 sinc 0.2k-4.5k -a 110 rate 14400 | aplay -t raw -f S16_LE -r 14400 -c 1) | ./dot-detector

First, we see we call rtl_upd much like we did above with rtl_fm, the values for frequency don't matter as the 
software will change to the appropriate channel, but in this example we tune to the Control Channel to verify audio
output listening to the control channel's machine gun like tone. You may wish to add the argument '-l 5000' to set the squelch to a really high number to not listen.  All of these values can be tweaked, however,

DO NOT CHANGE THE SAMPLE RATE. LEAVE AS -s 28.8k.

The software requires this precise number for proper function. In fact, past RTL_UDP, it is not advised to change any
of the values as it may hamper the proper functionality of the dot-detector program and subsequently, properly 
tuning in voice channels. ANY FURTHER MODIFICATION IS AT YOUR OWN DISCRETION AND CANNOT BE HELPED.

Alternatively, on Rapberry Pi hardware, you may be able to use the repo version of rtl_fm as it incorporates
UDP threads and seems to some degree to function properly, but you may need to specify a manual squelch level that
encompasses all of your LCNs. However, it cannot be officially supported now, and it is recommended to stick with
rtl_udp for now. Any feedback of working setups with rtl-sdr+rpt UDP functionality greatly appreciated.

Note: On Raspberry Pi, if ./detector and rtl_udp come up with a USB claimed error message, please run:
sudo modprobe -r dvb_usb_rtl28xxu

or add to blacklist if you desire. 

What Dot Detector does:

You may wonder what Dot Detector does exactly. It is an software, that besides tuning or aiding rtl_udp tune to voice
channels, can determine when the ending sequence of EDACS voice transmissions occur. If you have ever noticed by listening, EDACS has a distinct bleeping sequence at the end of its VOICE assignment that hampers scanners from 
scanning, as the voice has changed to another channel, but the "DOTTING" at the end makes scanners stay until the end.
This in turn, makes it so that you are always late to the next LCN. Dot-detector can detect this sequence and squelch it, freeing up the RTL stick so it can be changed to the next LCN in time for its transmission to start. 

Go ahead now and start up the detector with ./detector.sh

When it is used with LEDACS-ESK, you will typically see this output:

Sockets successfully initialized
Found 2 device(s):
  0:  Realtek, RTL2838UHIDIR, SN: 00000001
  1:  Realtek, RTL2838UHIDIR, SN: 00000002

Using device 1: ezcap USB 2.0 DVB-T/DAB/FM dongle
Found Rafael Micro R820T tuner
Oversampling input by: 35x.
Oversampling output by: 1x.
Buffer size: 8.13ms
Tuned to 851627000 Hz.
Sampling at 1008000 Hz.
Output at 28800 Hz.
Exact sample rate is: 1008000.009613 Hz
Tuner gain set to 49.60 dB.
Main socket started! :-) Tuning enabled on UDP/6020 
Playing raw data 'stdin' : Signed 16 bit Little Endian, Rate 14400 Hz, Mono

First, as we are using an OLD FORK of RTL_SDR, do not be surprised if your dongle is misnamed,
in most cases, everything should work just as well otherwise. When the software receives the 
tuning commands from LEDACS-ESK, you will begin to see (and hopefully hear) the voice channel
audio. In most cases, you will likely want to pipe this audio using pavucontrol through a 
virtual audio sink into Provoice decoding software, much like DSD, DSD+, or others. Those
are currently outside the scope of this readme, but may be included in a tutorial/wiki.

Typical output when tuning channels will include:

Tuning to: 851800000 [Hz] (central freq: 852052000 [Hz])
Changing squelch to 0 
Changing squelch to 5000 
AFC=1917
Changing squelch to 0 
Changing squelch to 5000 
AFC=2046
Changing squelch to 5000 
Tuning to: 855987500 [Hz] (central freq: 856239500 [Hz])
Changing squelch to 0 
Changing squelch to 5000 
AFC=1820
Changing squelch to 5000 
Tuning to: 858487500 [Hz] (central freq: 858739500 [Hz])
Changing squelch to 0 
Changing squelch to 5000 

You can see the frequency (in the LCN channel file) being tuned to and 
squelch being set to 0 (listen) for the duration of that channel assignment,
then squelch to 5000 to "turn off" that channel, then the next LCN tuned, etc etc.


I hope you all find this readme useful, please feel free to send any questions to the
radioreference.com forums or on github. Thank you. Also, I hope you find usefulness from 
this software and in the future, I hope to add new features, re-write much of the forked code
and clean up lots of it, maybe convert it to python, and hopefully, get rid of RTL_UDP.
For now, it can successfully track EDACS ESK, display senders and groups, and tune VOICE
channel assignments and has a pretty good success rate for me. Hopefully you will all have
success with it as well. Best of all, it does all of these things in LINUX, no Windows required.

lwvmobile

















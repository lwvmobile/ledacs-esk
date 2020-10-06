nc -l -u 7355 | sox -t raw -b 16 -e signed-integer -r 48000 -c 1 - -t raw - rate 28800 | ./ledacs-esk-analyzer 1 2



# ShepherdBMS2
Application and drivers for Shepherd BMS 2

## Setting up Docker Environment
For initial installation, visit here: https://nerdocs.atlassian.net/wiki/spaces/NER/pages/108888108/Setting+Up+STM32+Dev+Env

## Start Container on MacOS/Linux
In any terminal that is in the directory:

    # if need to rebuild image
    sudo docker build -f ./Dockerfile -t ner-gcc-arm .
    sudo docker run --rm -it --privileged -v "$PWD:/home/app" ner-gcc-arm:latest bash
    
## Start Container on Windows
In any terminal that is in the directory:

    # if need to rebuild image
    # docker build -f ./Dockerfile -t ner-gcc-arm .
    docker run --rm -it --privileged -v "$(PWD):/home/app" ner-gcc-arm:latest bash

    # mounting probe
    # in another terminal run wsl -d ubuntu
    usbipd wsl list
    usbipd wsl attach --distribution=ubuntu --busid=<BUSID>
    # close the other wsl window, the device should be mounted to any wsl instance
    
## Tools / Utils

    # to build project
    make all

    # to open a serial port (make sure /dev/tty0/ACM0 exists first)
    minicom -b 115200 -o -D /dev/ttyACM0

    # to flash STM board with Raspberry Pi Probe (WIP)
    openocd -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg -c "adapter speed 5000" -c "program ./build/cerberus.elf verify reset exit"
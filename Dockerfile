FROM ubuntu:latest

# Set up container and time zones
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive TZ="America/New_York" \
    apt-get -y install tzdata

# Download Linux support tools
RUN apt-get install -y \
    build-essential \
    wget \
    curl \
    openocd \
    git \
    gdb-multiarch \
    minicom \
    vim

RUN wget https://builds.renode.io/renode-1.13.3+20230712gitedfc975b.linux-portable.tar.gz
RUN mkdir renode_portable && tar -xvf renode-*.linux-portable.tar.gz -C renode_portable --strip-components=1
ENV PATH $PATH:/renode_portable

# Set up a development tools directory
WORKDIR /home/dev
ADD . /home/dev

RUN echo 'if [ $n -e /home/app/shepherd2.ioc ]; then echo \
" ______     __  __     ______     ______   __  __     ______     ______     _____\n\
/\  ___\   /\ \_\ \   /\  ___\   /\  == \ /\ \_\ \   /\  ___\   /\  == \   /\  __-.\n\
\ \___  \  \ \  __ \  \ \  __\   \ \  _-/ \ \  __ \  \ \  __\   \ \  __<   \ \ \/\ \ \n\
 \/\_____\  \ \_\ \_\  \ \_____\  \ \_\    \ \_\ \_\  \ \_____\  \ \_\ \_\  \ \____- \n\ 
  \/_____/   \/_/\/_/   \/_____/   \/_/     \/_/\/_/   \/_____/   \/_/ /_/   \/____/"; fi;' >> ~/.bashrc && \
echo 'if [ $n -e /home/app/cerberus.ioc ]; then echo \
"_________             ___.               \n\
\_   ___ \  __________\_ |__   ___________ __ __  ______\n\
/    \  \/_/ __ \_  __ \ __ \_/ __ \_  __ \  |  \/  ___/\n\
\     \___\  ___/|  | \/ \_\ \  ___/|  | \/  |  /\___ \ \n\
 \______  /\___  >__|  |___  /\___  >__|  |____//____  >\n\
        \/     \/          \/     \/                 \/ "; fi;' >> ~/.bashrc \
&& echo 'alias serial="minicom -b 115200 -o -D /dev/ttyACM0"' >> ~/.bashrc \
&& echo 'alias flash="openocd -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg -c \"adapter speed 5000\" -c \"program /home/app/build/cerberus.elf verify reset exit\""' >> ~/.bashrc \
&& echo 'alias emulate="renode /home/app/*.resc"' >> ~/.bashrc

# Install cross compiler
RUN wget -qO- https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 | tar -xvj

ENV PATH $PATH:/home/dev/gcc-arm-none-eabi-10.3-2021.10/bin

WORKDIR /home/app

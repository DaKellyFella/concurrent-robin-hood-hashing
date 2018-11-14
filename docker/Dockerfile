FROM ubuntu:14.04
RUN rm /bin/sh && ln -s /bin/bash /bin/sh

RUN rm /etc/apt/apt.conf.d/docker-clean

RUN apt-get update -y
RUN apt-get dist-upgrade -y

# Libraries
RUN apt-get update -y && \
    apt-get install -y build-essential git cmake-qt-gui sudo g++ libtbb-dev libpapi-dev

# Create a user
ENV USERNAME lockfree
RUN useradd $USERNAME -m
RUN export uid=$(id $USERNAME -u) gid=$(id $USERNAME -g) && \
    echo "$USERNAME:x:${uid}:${gid}:$USERNAME,,,:/home/$USERNAME:/bin/bash" >> /etc/passwd && \
    echo "$USERNAME:x:${uid}:" >> /etc/group && \
    echo "$USERNAME ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers && \
    chown ${uid}:${gid} -R /home/$USERNAME

USER $USERNAME
WORKDIR /home/$USERNAME

# Auto complete
RUN sudo apt-get install -y bash-completion

ADD cpuinfo cpuinfo
RUN sudo chown -R $USERNAME cpuinfo
RUN cd cpuinfo && \
    mkdir -p build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j `grep -c '^processor' /proc/cpuinfo` && \
    sudo make install

RUN sudo apt-get install -y autoconf xsltproc

ADD jemalloc jemalloc 
RUN sudo chown -R $USERNAME jemalloc
RUN cd jemalloc && \
    ./autogen.sh && \
    ./configure LIBS='-ldl' --with-jemalloc-prefix=je_ --with-mangling && \
    make -j `grep -c '^processor' /proc/cpuinfo` && \
    sudo make install_bin install_include install_lib

CMD ["/bin/bash"]

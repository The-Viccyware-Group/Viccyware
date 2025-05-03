FROM debian:bookworm

ARG UID
ARG GID
ARG USER_NAME
ARG DIR_PATH

RUN apt-get update && apt-get install -y sudo
RUN apt-get install -y \
    build-essential chrpath cpio debianutils diffstat expect file gawk gcc git iputils-ping libacl1 liblz4-tool locales python3 python3-git python3-jinja2 python3-pexpect python3-subunit socat texinfo unzip wget xz-utils zstd
RUN apt-get install -y \
    git-core gnupg flex bison gperf build-essential zip curl zlib1g-dev gcc-multilib g++-multilib libc6-dev-i386 lib32ncurses5-dev x11proto-core-dev libx11-dev lib32z-dev libxml-simple-perl libc6-dev libgl1-mesa-dev tofrodos libxml2-utils xsltproc genisoimage
RUN apt-get install -y \
    gawk chrpath texinfo p7zip-full android-sdk-libsparse-utils mime-support
RUN apt-get install -y \
    ruby subversion libssl-dev
RUN apt-get install -y \
    pkg-config
RUN apt-get install -y \
    nano libtinfo5 ninja-build clang ccache libc++-dev rsync

RUN ln -sf /bin/bash /bin/sh
RUN useradd -ms /bin/bash ${USER_NAME} && echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
RUN echo "en_US.UTF-8 UTF-8" > /etc/locale.gen && locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8
RUN usermod -u ${UID} ${USER_NAME} && groupmod -g ${GID} ${USER_NAME}

RUN wget http://ftp.debian.org/debian/pool/main/libf/libffi/libffi7_3.3-6_amd64.deb
RUN wget http://ftp.debian.org/debian/pool/main/o/openssl/libssl1.1_1.1.1w-0+deb11u1_amd64.deb
RUN wget http://ftp.debian.org/debian/pool/main/p/python2.7/libpython2.7-minimal_2.7.18-8+deb11u1_amd64.deb
RUN wget http://ftp.debian.org/debian/pool/main/p/python2.7/python2.7-minimal_2.7.18-8+deb11u1_amd64.deb
RUN wget http://ftp.debian.org/debian/pool/main/p/python2.7/libpython2.7-stdlib_2.7.18-8+deb11u1_amd64.deb
RUN wget http://ftp.debian.org/debian/pool/main/p/python2.7/python2.7_2.7.18-8+deb11u1_amd64.deb

RUN dpkg -i libffi7_3.3-6_amd64.deb
RUN dpkg -i libssl1.1_1.1.1w-0+deb11u1_amd64.deb
RUN dpkg -i libpython2.7-minimal_2.7.18-8+deb11u1_amd64.deb
RUN dpkg -i python2.7-minimal_2.7.18-8+deb11u1_amd64.deb
RUN dpkg -i libpython2.7-stdlib_2.7.18-8+deb11u1_amd64.deb
RUN dpkg -i python2.7_2.7.18-8+deb11u1_amd64.deb

RUN ln -sf /usr/bin/python2.7 /usr/bin/python2
RUN ln -sf /usr/bin/python2.7 /usr/bin/python
RUN chmod 0755 /usr/local/bin
RUN ln -sf /usr/bin/ninja /usr/local/bin/ninja

USER ${USER_NAME}
WORKDIR ${DIR_PATH}
CMD ["/bin/bash"]

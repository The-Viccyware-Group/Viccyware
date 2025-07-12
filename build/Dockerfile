FROM debian:bookworm-20250630-slim

ARG UID
ARG GID
ARG USER_NAME
ARG DIR_PATH

# this is too much, but it's probably nice to lean on the "more" than "less" side here
# in the case i want to compile something which might need one of these,
# so i don't have to switch out the container
RUN apt-get update && \ 
    apt-get install -y sudo build-essential chrpath cpio debianutils \
    diffstat expect file gcc git iputils-ping libacl1 liblz4-tool \
    locales python3 python3-git python3-jinja2 python3-pexpect \
    python3-subunit socat texinfo unzip wget xz-utils zstd git-core \
    gnupg flex bison gperf build-essential zip curl zlib1g-dev \
    libncurses5-dev x11proto-core-dev libx11-dev libz-dev \
    libxml-simple-perl libc6-dev libgl1-mesa-dev tofrodos libxml2-utils \
    xsltproc genisoimage gawk chrpath texinfo p7zip-full \
    android-sdk-libsparse-utils mime-support ruby subversion libssl-dev \
    protobuf-compiler pkg-config nano libtinfo5 ninja-build clang ccache \
    libc++-dev rsync cmake

RUN ln -sf /bin/bash /bin/sh
RUN useradd -ms /bin/bash ${USER_NAME} && echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
RUN echo "en_US.UTF-8 UTF-8" > /etc/locale.gen && locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8
RUN usermod -u ${UID} ${USER_NAME} && groupmod -g ${GID} ${USER_NAME}

RUN chmod 0755 /usr/local/bin
RUN ln -sf /usr/bin/python3 /usr/bin/python
RUN ln -sf /usr/bin/ninja /usr/local/bin/ninja

USER ${USER_NAME}
WORKDIR ${DIR_PATH}
CMD ["/bin/bash"]

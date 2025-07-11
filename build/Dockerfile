FROM debian:bookworm-20250630-slim

ARG UID
ARG GID
ARG USER_NAME
ARG DIR_PATH

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

RUN set -e; \
    arch="$(dpkg --print-architecture)"; \
    for url in \
        "http://ftp.debian.org/debian/pool/main/libf/libffi/libffi7_3.3-6_${arch}.deb" \
        "http://ftp.debian.org/debian/pool/main/o/openssl/libssl1.1_1.1.1w-0+deb11u1_${arch}.deb" \
        "http://ftp.debian.org/debian/pool/main/p/python2.7/libpython2.7-minimal_2.7.18-8+deb11u1_${arch}.deb" \
        "http://ftp.debian.org/debian/pool/main/p/python2.7/python2.7-minimal_2.7.18-8+deb11u1_${arch}.deb" \
        "http://ftp.debian.org/debian/pool/main/p/python2.7/libpython2.7-stdlib_2.7.18-8+deb11u1_${arch}.deb" \
        "http://ftp.debian.org/debian/pool/main/p/python2.7/python2.7_2.7.18-8+deb11u1_${arch}.deb" \
	    "https://github.com/upx/upx/releases/download/v5.0.0/upx-5.0.0-${arch}_linux.tar.xz" \
	    "https://go.dev/dl/go1.24.4.linux-${arch}.tar.gz"; \
    do \
        wget -q "$url"; \
    done && \
    dpkg -i *.deb && \
    tar -zxf go1.24.4.linux-${arch}.tar.gz && \
    tar -xvf upx-5.0.0-${arch}_linux.tar.xz && \
    mv upx-5.0.0-${arch}_linux/upx /usr/bin/ && \
    rm -f *.deb *.tar.gz *.tar.xz

RUN mv go /usr/local/
RUN ln -sf /usr/local/go/bin/go /usr/bin/go
RUN mkdir /root/go
RUN go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.6
RUN go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.5.1
RUN go install github.com/grpc-ecosystem/grpc-gateway/v2/protoc-gen-grpc-gateway@v2.27.1
RUN mv $(go env GOPATH)/bin/* /usr/bin/

RUN chmod 0755 /usr/bin/upx

RUN ln -sf /usr/bin/python2.7 /usr/bin/python2
RUN ln -sf /usr/bin/python2.7 /usr/bin/python
RUN chmod 0755 /usr/local/bin
RUN ln -sf /usr/bin/ninja /usr/local/bin/ninja

USER ${USER_NAME}
WORKDIR ${DIR_PATH}
CMD ["/bin/bash"]

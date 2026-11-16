# Using ubuntu:22.04 because 25.10 includes a older compiler that we need to build PICO correctly.
# We need to wait until it's fixed
# Note to selve, the issue is that some headers ar enot included standaerd anymore in GCC15
# FROM ubuntu:25.10
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PICO_SDK_PATH=/opt/pico-sdk
ENV FREERTOS_KERNEL_PATH=/opt/FreeRTOS-Kernel

# Install everything needed
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        git python3 ninja-build cmake gcc-arm-none-eabi \
        libnewlib-arm-none-eabi build-essential \
        libstdc++-arm-none-eabi-newlib libusb-dev gnutls-bin \
        python3-pycryptodome libatomic1 && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /opt

# Git config and clone dependencies
RUN git config --global http.version HTTP/1.1 && \
    git config --global http.postBuffer 1048576000 && \
    git config --global https.postBuffer 1048576000 && \
    git clone --branch 2.3.1 --depth 1 --recurse-submodules --shallow-submodules \
        https://github.com/raspberrypi/pico-sdk.git && \
    git clone --branch V11.3.3 --depth 1 --recurse-submodules --shallow-submodules \
        https://github.com/FreeRTOS/FreeRTOS-Kernel.git

#RUN mkdir -p /opt/src/

# Copy the entrypoints 
COPY ./build-entrypoint.sh /build-entrypoint.sh
COPY ./test-entrypoint.sh /test-entrypoint.sh

RUN chmod +x /build-entrypoint.sh /test-entrypoint.sh

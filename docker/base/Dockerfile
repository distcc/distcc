FROM ubuntu:xenial-20210804

LABEL maintainer=""

ENV DEBIAN_FRONTEND=noninteractive

COPY apt-conf /etc/apt/apt.conf.d/

RUN apt-get update && \
    apt-get install -y autoconf \
                       python3-dev \
                       python-dev \
                       libiberty-dev \
                       build-essential \
                       make \
                   && \
    rm -rf /var/lib/apt/lists/*

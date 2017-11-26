# Building distcc with different compilers

## Requirements:

Docker 1.9.1

## Build
The following command will create three images based on Ubuntu 16.04 using gcc 4.8, 5.4 and clang 3.8 and
build distcc inside the container.

```
$ cd docker
$ ./build.sh
```

In order to build only one variant use the following command:

```
$ cd docker
$ ./build.sh clang-3.8|gcc-4.8|gcc-5
```

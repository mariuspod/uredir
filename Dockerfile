FROM busybox:glibc
EXPOSE 3333
ADD uredir /
CMD ["/uredir", "-l", "debug", "-n", "-p", "127.0.0.1:3333"]

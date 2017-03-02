FROM busybox:glibc
EXPOSE 3333
ADD uredir /
CMD ["/uredir", "-l", "debug", "127.0.0.1:3333"]

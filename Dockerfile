FROM busybox:glibc
EXPOSE 3333
ADD uredir /
CMD ["/uredir", "-l", "debug", "-r", "3333"]

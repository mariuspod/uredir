FROM busybox:glibc
EXPOSE 3333
ADD uredir /
CMD ["/uredir", "-l", "debug", "3333"]

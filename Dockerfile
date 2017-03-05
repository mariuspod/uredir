FROM busybox:glibc
EXPOSE 3333
ADD uredir /
CMD ["/uredir", "-r", "3333"]

UDP port redirector
===================

`uredir` is a small [zlib][] licensed tool to redirect UDP connections.
It can be used to forward connections on select external interfaces to multiple destinations.

`uredir` forwards packets to a given destination based on a prefix that is matched against the received payload.
That way, it's possible to dispatch UDP packets to various recipients based on the specified patterns.

## Deployment
The binary is packaged into a docker container that listens on port `3333` by default.
### Starting the container
`docker run -it -p 3333:3333/udp -e PATTERNS="foo.=127.0.0.1:1111,bar.=127.0.0.1:2222" mpod/uredir`

## Forwarding modes
### Single destination
By sending a payload with the matching pattern it will forward the payload to the destination.

`echo foo.hello | nc -w 1 -u localhost 3333`

The above command will forward all UDP traffic to the local service on port `1111`.

### All destinations
Sending a payload with the fixed prefix `all.` will forward the payload to *all* destinations which are configured via patterns.

`echo all.hello | nc -w 1 -u localhost 3333`

The above command will forward all UDP traffic to all services with a valid pattern, so in our example to local services with ports `1111` and `2222`.

## Usage

    uredir [-hrv] [-l LEVEL] [PORT]
    
      -h      Show this help text
      -l LVL  Set log level: none, err, info, notice (default), debug
      -r      Remove the pattern prefix from the payload before sending it to the destination(s)
      -v      Show program version


## Origin & References

`uredir` is based on [udp_redirect.c][] by Ivan Tikhonov and Joachim Nilsson.

[zlib]:            https://en.wikipedia.org/wiki/Zlib_License
[udp_redirect.c]:  http://brokestream.com/udp_redirect.html

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->

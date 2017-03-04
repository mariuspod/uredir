UDP port redirector
===================

`uredir` is a small [zlib][] licensed tool to redirect UDP connections.
It can be used to forward connections on select external interfaces to multiple destinations.

`uredir` forwards packets to a given destination based on a prefix that is matched against the received payload.
That way, it's possible to dispatch UDP packets to various recipients based on the specified patterns.

## Usage

    uredir [-hrv] [-l LEVEL] [PORT]
    
      -h      Show this help text
      -l LVL  Set log level: none, err, info, notice (default), debug
      -r      Remove the found pattern prefix from the payload before forwarding
      -v      Show program version

## Forwarding modes
Currently uredir can either forward to one destination or to all configured destinations.
By setting the following ENV variable there will be at most two possible destinations that will be used for forwarding.

`export PATTERNS="foo.=127.0.0.1:1111,bar.=127.0.0.1:2222"`

### Single destination
By sending a payload with the matching pattern it will forward the payload to the single matching destination only.

`echo foo.hello | nc -w 1 -u localhost 3333`

The above command will forward all UDP traffic to the local service running on port `1111`.

### All destinations
Sending a payload with the fixed prefix `all.` will forward the payload to *all* destinations which are configured via patterns.

`echo all.hello | nc -w 1 -u localhost 3333`

The above command will forward all UDP traffic to all services with a valid pattern, so in the above example to local services running on ports `1111` and `2222`.

## Docker
There's also the [mpod/uredir][] docker container that has the binary already packed. The listen port is set to `3333` by default but can be easily changed by specifying another command at runtime.

### Starting the container
`docker run -it -p 3333:3333/udp -e PATTERNS="foo.=127.0.0.1:1111,bar.=127.0.0.1:2222" mpod/uredir`


## Origin & References

`uredir` is based on [udp_redirect.c][] by Ivan Tikhonov and Joachim Nilsson.

[zlib]:            https://en.wikipedia.org/wiki/Zlib_License
[udp_redirect.c]:  http://brokestream.com/udp_redirect.html
[mpod/uredir]:     https://hub.docker.com/r/mpod/uredir/

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->

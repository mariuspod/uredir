/*
 * Copyright (C) 2007-2008  Ivan Tikhonov <kefeer@brokestream.com>
 * Copyright (C) 2016  Joachim Nilsson <troglobit@gmail.com>
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 */

#include "config.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define SYSLOG_NAMES
#include <syslog.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <limits.h>
#define NUM_PATTERNS 4
#define PREFIX_ALL "all"

static char *prognm     = PACKAGE_NAME;

int parse_patterns();

struct Pattern {
  char prefix[64];
  struct sockaddr_in destination;
  char hostname[64];
  unsigned short port;
};

struct Pattern *patterns[NUM_PATTERNS];


static int loglvl(char *level)
{
  int i;

  for (i = 0; prioritynames[i].c_name; i++) {
    if (!strcmp(prioritynames[i].c_name, level))
      return prioritynames[i].c_val;
  }

  return atoi(level);
}

static int version(void)
{
  printf("%s\n", PACKAGE_VERSION);
  return 0;
}

#define USAGE "Usage: PATTERNS=my_pattern_1=127.0.0.1:1111,my_pattern_2=127.0.0.1:2222%s [-hv] [-l LEVEL] [PORT]"

static int usage(int code)
{

  printf("\n" USAGE "\n\n", prognm);
  printf("  -h      Show this help text\n");
  printf("  -l LVL  Set log level: none, err, info, notice (default), debug\n");
  printf("  -v      Show program version\n\n");

  return code;
}

static void exit_cb(int signo)
{
  syslog(LOG_DEBUG, "Got signal %d, exiting.", signo);
  exit(0);
}

int starts_with(const char *a, const char *b)
{
  if(strncmp(a, b, strlen(b)) == 0) return 1;
  return 0;
}

static struct Pattern* get_pattern(char *buf) {
  for (int i = 0; i < NUM_PATTERNS; i++) {
    struct Pattern *p = patterns[i];
    if(p && strlen(p->prefix) > 0 && starts_with(buf, p->prefix))
      return p;
  }
  return NULL;
}

static char *progname(char *arg0)
{
  char *nm;

  nm = strrchr(arg0, '/');
  if (nm)
    nm++;
  else
    nm = arg0;

  return nm;
}

int forward(int sd);

int main(int argc, char *argv[])
{
  int c, sd, src_port, dst_port;
  int opt = 0;
  int log_opts = LOG_CONS | LOG_PID;
  int loglevel = LOG_NOTICE;
  char *ident;
  char src[20], dst[20];
  struct sockaddr_in sa;
  struct sockaddr_in da;

  ident = prognm = progname(argv[0]);
  while ((c = getopt(argc, argv, "h:l:v")) != EOF) {
    switch (c) {
      case 'h':
        return usage(0);

      case 'l':
        loglevel = loglvl(optarg);
        if (-1 == loglevel)
          return usage(1);
        break;

      case 'v':
        return version();

      default:
        return usage(-1);
    }
  }

  /*if (!background && do_syslog < 1)
    log_opts |= LOG_PERROR;
    */
  openlog(ident, log_opts, LOG_DAEMON);
  setlogmask(LOG_UPTO(loglevel));

  if (optind >= argc)
    return usage(-2);

  signal(SIGALRM, exit_cb);
  signal(SIGHUP,  exit_cb);
  signal(SIGINT,  exit_cb);
  signal(SIGQUIT, exit_cb);
  signal(SIGTERM, exit_cb);

  if (parse_patterns() < 0)
    return usage(-3);


  /* By default we need at least src:port */
  src_port = (unsigned short) atoi(argv[optind++]);
  if (-1 == src_port)
    return usage(-3);

  /* At least on Linux the obnoxious IP_MULTICAST_ALL flag is set by default */
  sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sd < 0) {
    syslog(LOG_ERR, "Failed opening UDP socket: %m");
    return 1;
  }
  setsockopt(sd, IPPROTO_IP, IP_MULTICAST_ALL, &opt, sizeof(opt));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_ANY);
  sa.sin_port = htons(src_port);
  if (bind(sd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    syslog(LOG_ERR, "Failed binding our address (%s:%d): %m", "0.0.0.0", src_port);
    return 1;
  }
  syslog(LOG_DEBUG, "Successfully bound to address (%s:%d)", "0.0.0.0", src_port);

  while (1) {
    int forwarded = forward(sd);
    if (forwarded != 0)
      return forwarded;
  }
  return 0;
}

int reply(int sd, char *buf, int n, struct Pattern *p) {
  syslog(LOG_DEBUG, "Found forwarding pattern for prefix: '%s': %s:%d\n", p->prefix, p->hostname, p->port);
  syslog(LOG_DEBUG, "Forwarding %d bytes data to %s:%d", n, p->hostname, p->port);
  n = sendto(sd, buf, n, 0, (struct sockaddr *)&p->destination, sizeof(p->destination));
  if (n <= 0) {
    if (n < 0)
      syslog(LOG_ERR, "Failed forwarding data: %m");
    return 1;
  }
  return 0;
}

int forward(int sd) {
  int n;
  char buf[USHRT_MAX], addr[50];
  struct sockaddr_in sa;
  socklen_t sn = sizeof(sa);

  syslog(LOG_DEBUG, "Reading socket ...");
  n = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr *)&sa, &sn);
  if (n <= 0) {
    if (n < 0)
      syslog(LOG_ERR, "Failed receiving data: %m");
    return 1;
  }

  if (starts_with(buf, PREFIX_ALL)) {
    for(int i = 0; i < NUM_PATTERNS; i++) {
      if(patterns[i]) {
        int reply_ret = reply(sd, buf, n, patterns[i]);
        if(reply_ret != 0)
          return reply_ret;;
      }

    }
    return 0;
  } else {
    struct Pattern *p = get_pattern(buf);
    if (p == NULL) {
      return 0;
    } else {
      return reply(sd, buf, n, p);
    }
  }
}

int get_destination(const char *hostname, unsigned short port, struct sockaddr_in *dst) {
  dst->sin_family = AF_INET;
  dst->sin_port = htons(port);
  dst->sin_addr.s_addr = inet_addr(hostname);
  if (dst->sin_addr.s_addr == INADDR_NONE) {
    struct hostent *h = gethostbyname(hostname);
    if (!h)
      return -1;

    dst->sin_addr = *((struct in_addr *) h->h_addr);
  }
  return 0;
}


int parse_patterns()
{
  /* check that we have patterns */
  char *patterns_env = getenv("PATTERNS");
  if (patterns_env == NULL) {
    syslog(LOG_ERR, "No forwarding patterns provided, please pass it as ENV variable $PATTERNS.\n");
    return -1;
  }
  syslog(LOG_NOTICE, "Forwarding patterns:\n");

  /* populate the patterns */
  int i = 0;
  struct Pattern *p;
  char *prefix;
  char *hostname;
  int port;
  char *pattern_str;
  char *end_str;
  pattern_str = strtok_r(patterns_env, ",", &end_str);
  while (pattern_str != NULL) {

    char *end_token;
    hostname = "";
    port = 0;
    prefix = strtok_r(pattern_str, "=", &end_token);
    if (prefix) {
      char *dst = strtok_r(NULL, "=", &end_token);
      if (dst) {
        char *port_token;
        hostname = strtok_r(dst, ":", &port_token);
        if (hostname) {
          char *port_str = strtok_r(NULL, ":", &port_token);
          if (port_str) {
            if (i >= NUM_PATTERNS)
              return -1;
            struct sockaddr_in destination;
            int has_error = get_destination(hostname, (unsigned short) atoi(port_str), &destination); 
            if(has_error != 0)
              return -1;
            p = (struct Pattern*) malloc(sizeof(struct Pattern));
            //TODO check nullterminator \0 of strncpy 
            strncpy(p->prefix, prefix, sizeof(p->prefix));
            p->destination = destination;
            strncpy(p->hostname, inet_ntoa(p->destination.sin_addr), sizeof(p->hostname));
            p->port = ntohs(destination.sin_port);
            patterns[i] = p;
          }
        }
      }
    }
    pattern_str = strtok_r(NULL, ",", &end_str);
    i++;
  }
  for (int i = 0; i < NUM_PATTERNS; i++) {
    if(patterns[i] && strlen(patterns[i]->prefix) > 0)
      syslog(LOG_NOTICE, "'%s' -> %s:%d\n", patterns[i]->prefix, patterns[i]->hostname, patterns[i]->port);
  }
  return 0;
}


/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */

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
#define NUM_PATTERNS 4

static int echo         = 0;
static int inetd        = 0;
static int has_patterns = 0;
static int background   = 1;
static int do_syslog    = 1;
static char *prognm     = PACKAGE_NAME;

int parse_patterns();

struct Pattern {
	char *prefix;
	char *hostname;
	int port;
};

struct Pattern *newPattern() {
	struct Pattern *retVal = malloc(sizeof(struct Pattern));
	if (retVal == NULL)
		return EXIT_FAILURE;
	retVal->prefix = malloc(64); // prefixes can be up to 64 chars long which should fit.
	if (retVal->prefix == NULL) {
		free (retVal);
		return EXIT_FAILURE;
	}
	retVal->hostname = malloc(64); // hostnames can be up to 64 chars long which should fit.
	if (retVal->hostname == NULL) {
		free (retVal);
		return EXIT_FAILURE;
	}
	retVal->port = malloc(sizeof(int));
	if (retVal->port == NULL) {
		free (retVal);
		return EXIT_FAILURE;
	}
	return retVal;
}

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

#define USAGE "Usage: %s [-hinspv] [-I NAME] [-l LEVEL] [SRC:PORT] [DST:PORT]"

static int usage(int code)
{
	if (inetd) {
		syslog(LOG_ERR, USAGE, prognm);
		return code;
	}

	printf("\n" USAGE "\n\n", prognm);
	printf("  -h      Show this help text\n");
	printf("  -i      Run in inetd mode, get SRC:PORT from stdin\n");
	printf("  -I NAME Identity, tag syslog messages with NAME, default: process name\n");
	printf("  -l LVL  Set log level: none, err, info, notice (default), debug\n");
	printf("  -n      Run in foreground, do not detach from controlling terminal\n");
	printf("  -s      Use syslog, even if running in foreground, default w/o -n\n");
	printf("  -p      Pattern-based forwarding based on the actual payload. The patterns are defined by setting an ENV variable $PATTERNS in the format: 'my_pattern_1=10.10.0.1,my_pattern_2=10.10.0.2'\n");
	printf("  -v      Show program version\n\n");
	printf("If DST:PORT is left out the program operates in echo mode.\n"
	       "Bug report address: %-40s\n\n", PACKAGE_BUGREPORT);

	return code;
}

static int parse_ipport(char *arg, char *buf, size_t len)
{
	char *ptr;

	if (!arg || !buf || !len)
		return -1;

	ptr = strchr(arg, ':');
	if (!ptr)
		return -1;

	*ptr++ = 0;
	snprintf(buf, len, "%s", arg);

	return atoi(ptr);
}

static void exit_cb(int signo)
{
	syslog(LOG_DEBUG, "Got signal %d, exiting.", signo);
	exit(0);
}

static struct Pattern* get_pattern(char *buf) {
	for (int i = 0; i < NUM_PATTERNS; i++) {
		struct Pattern *p = patterns[i];
		if(p && strlen(p->prefix) > 0 && starts_with(buf, p->prefix))
			return p;
	}
	return NULL;
}

int starts_with(const char *a, const char *b)
{
	if(strncmp(a, b, strlen(b)) == 0) return 1;
	return 0;
}

/*
 * read from in, forward to out, creating a socket pipe ... or tube
 *
 * If no @dst is given then we're in echo mode, send everything back
 * If no @src is given then we should forward the reply
 */
static int tuby(int sd, struct sockaddr_in *src, struct sockaddr_in *dst)
{
	int n;
	char buf[BUFSIZ], addr[50];
	struct sockaddr_in sa;
	static struct sockaddr_in da;
	socklen_t sn = sizeof(sa);

	syslog(LOG_DEBUG, "Reading %s socket ...", src ? "client" : "proxy");
	n = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr *)&sa, &sn);
	if (n <= 0) {
		if (n < 0)
			syslog(LOG_ERR, "Failed receiving data: %m");
		return 0;
	}

	syslog(LOG_DEBUG, "Received %d bytes data from %s:%d", n, inet_ntop(AF_INET, &sa.sin_addr, addr, sn), ntohs(sa.sin_port));
	if (has_patterns) {
		struct Pattern *p = get_pattern(buf);
		if (p == NULL)
			return -1;
		syslog(LOG_DEBUG, "Found forwarding pattern for prefix: '%s': %s:%d\n", p->prefix, p->hostname, p->port);
		dst->sin_addr.s_addr = inet_addr(p->hostname);
		dst->sin_port = htons(p->port);
	}

	/* Echo mode, return everything to sender */
	if (!dst)
		return sendto(sd, buf, n, 0, (struct sockaddr *)&sa, sn);

	/* Verify the received packet is the actual reply before we forward it */
	if (!src) {
		if (sa.sin_addr.s_addr != da.sin_addr.s_addr || sa.sin_port != da.sin_port)
			return 0;
	} else {
		*src = sa;	/* Tell callee who called */
		da   = *dst;	/* Remember for forward of reply. */
	}

	syslog(LOG_DEBUG, "Forwarding %d bytes data to %s:%d", n, inet_ntop(AF_INET, &dst->sin_addr, addr, sizeof(*dst)), ntohs(dst->sin_port));
	n = sendto(sd, buf, n, 0, (struct sockaddr *)dst, sizeof(*dst));
	if (n <= 0) {
		if (n < 0)
			syslog(LOG_ERR, "Failed forwarding data: %m");
		return 0;
	}

	return n;
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
	while ((c = getopt(argc, argv, "hiI:l:nspv")) != EOF) {
		switch (c) {
			case 'h':
				return usage(0);

			case 'i':
				inetd = 1;
				break;

			case 'I':
				ident = strdup(optarg);
				break;

			case 'l':
				loglevel = loglvl(optarg);
				if (-1 == loglevel)
					return usage(1);
				break;

			case 'n':
				background = 0;
				do_syslog--;
				break;

			case 's':
				do_syslog++;
				break;
			case 'p':
				has_patterns = 1;
				break;

			case 'v':
				return version();

			default:
				return usage(-1);
		}
	}

	if (!background && do_syslog < 1)
		log_opts |= LOG_PERROR;
	openlog(ident, log_opts, LOG_DAEMON);
	setlogmask(LOG_UPTO(loglevel));

	if (optind >= argc)
		return usage(-2);

	signal(SIGALRM, exit_cb);
	signal(SIGHUP,  exit_cb);
	signal(SIGINT,  exit_cb);
	signal(SIGQUIT, exit_cb);
	signal(SIGTERM, exit_cb);

	if (has_patterns) {
		if (parse_patterns() < 0)
			return usage(-3);


		/* By default we need at least src:port */
		src_port = parse_ipport(argv[optind++], src, sizeof(src));
		if (-1 == src_port)
			return usage(-3);

	} else if (inetd) {
		/* In inetd mode we redirect from src=stdin to dst:port */
		dst_port = parse_ipport(argv[optind], dst, sizeof(dst));
	} else {
		/* By default we need at least src:port */
		src_port = parse_ipport(argv[optind++], src, sizeof(src));
		if (-1 == src_port)
			return usage(-3);

		dst_port = parse_ipport(argv[optind], dst, sizeof(dst));
	}

	/* If no dst, then user wants to echo everything back to src */
	if (-1 == dst_port) {
		echo = 1;
	} else {
		da.sin_family = AF_INET;
		da.sin_addr.s_addr = inet_addr(dst);
		da.sin_port = htons(dst_port);
	}

	if (inetd) {
		sd = STDIN_FILENO;
	} else {
		sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
		if (sd < 0) {
			syslog(LOG_ERR, "Failed opening UDP socket: %m");
			return 1;
		}

		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = inet_addr(src);
		sa.sin_port = htons(src_port);
		if (bind(sd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
			syslog(LOG_ERR, "Failed binding our address (%s:%d): %m", src, src_port);
			return 1;
		}

		if (background) {
			if (-1 == daemon(0, 0)) {
				syslog(LOG_ERR, "Failed daemonizing: %m");
				return 2;
			}
		}
	}

	/* At least on Linux the obnoxious IP_MULTICAST_ALL flag is set by default */
	setsockopt(sd, IPPROTO_IP, IP_MULTICAST_ALL, &opt, sizeof(opt));

	while (1) {
		if (inetd)
			alarm(3);

		if (echo) {
			tuby(sd, NULL, NULL);
		} else if (tuby(sd, &sa, &da))
			tuby(sd, NULL, &sa);
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
	syslog(LOG_INFO, "Forwarding patterns:\n");

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
		p = newPattern();
		char *end_token;
		hostname = "";
		port = 0;
		prefix = strtok_r(pattern_str, "=", &end_token);
		if (prefix != NULL) {
			char *dst = strtok_r(NULL, "=", &end_token);
			if (dst == NULL) {
				freePattern(p);
			} else {
				char *port_token;
				hostname = strtok_r(dst, ":", &port_token);
				if (hostname == NULL) {
					freePattern(p);
				} else {
					char *port_str = strtok_r(NULL, ":", &port_token);
					if (port_str == NULL) {
						freePattern(p);
					} else {
						if (i >= NUM_PATTERNS)
							return -1;

						p->prefix = strdup(prefix);
						p->hostname = strdup(hostname);
						p->port = atoi(port_str);
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
			syslog(LOG_INFO, "'%s' -> %s:%d\n", patterns[i]->prefix, patterns[i]->hostname, patterns[i]->port);
	}
	return 0;
}

void freePattern(struct Pattern *p) {
	free(p->port);
	free(p->hostname);
	free(p->prefix);
	free(p);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */

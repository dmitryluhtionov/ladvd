/*
 $Id$
*/

#include "main.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_NETPACKET_PACKET_H
#include <netpacket/packet.h>
#endif /* HAVE_NETPACKET_PACKET_H */
#ifdef HAVE_NET_BPF_H
#include <net/bpf.h>
#endif /* HAVE_NET_BPF_H */

unsigned int loglevel = CRIT;
extern int do_fork;
extern int do_debug;

void my_log(int prio, const char *fmt, ...) {

    va_list ap;
    va_start(ap, fmt);

    if (prio > loglevel)
	return;

    if (do_fork == 1) {
	vsyslog(LOG_INFO, fmt, ap);
    } else {
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
    }
}

void * my_malloc(size_t size) {
    void *ptr;

    if ((ptr = malloc(size)) == NULL) {
	my_log(CRIT, "malloc failed: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }
    memset(ptr, 0, size);
    return(ptr);
}

char * my_strdup(const char *str) {
    char *cstr;

    if ((cstr = strdup(str)) == NULL) {
	my_log(CRIT, "strdup failed: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }
    return(cstr);
}

int my_socket(int af, int type, int proto) {
    int s;

    if ((s = socket(af, type, proto)) < 0) {
	my_log(CRIT, "opening socket failed: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }
    return(s);
}

int my_rsocket() {

    int socket = -1;

    // return stdout on debug
    if (do_debug == 1)
	return(1);

#ifdef HAVE_NETPACKET_PACKET_H
    socket = my_socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
#elif HAVE_NET_BPF_H
    int n = 0;
    char *dev;

    do {
	if (asprintf(&dev, "/dev/bpf%d", n++) == -1) {
	    my_log(CRIT, "failed to allocate buffer");
	    return(-1);
	}
	socket = open(dev, O_WRONLY);
    } while (socket < 0 && errno == EBUSY);
#endif

    return(socket);
}

int my_rsend(int s, struct netif *netif, const void *msg, size_t len) {

    size_t count = 0;

    // debug
    if (do_debug == 1)
	return(write(s, msg, len));

#ifdef HAVE_NETPACKET_PACKET_H
    struct sockaddr_ll sa;
    memset(&sa, 0, sizeof (sa));

    sa.sll_family = AF_PACKET;
    sa.sll_ifindex = netif->index;
    sa.sll_protocol = htons(ETH_P_ALL);

    count = sendto(s, msg, len, 0, (struct sockaddr *)&sa, sizeof (sa));
#elif HAVE_NET_BPF_H
    struct ifreq ifr;

    // prepare ifr struct
    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, netif->name, IFNAMSIZ);

    if (ioctl(s, BIOCSETIF, (caddr_t)&ifr) < 0) {
	my_log(CRIT, "ioctl failed: %s", strerror(errno));
	return(-1);
    }
    count = write(s, msg, len);
#endif

    if (count != len)
	my_log(WARN, "only %d bytes written: %s", count, strerror(errno));
    
    return(count);
}

struct netif *netif_byindex(struct netif *netifs, uint8_t index) {
    struct netif *netif;

    for (netif = netifs; netif != NULL; netif = netif->next) {
	if (netif->index == index)
	    break;
    }
    return(netif);
}

struct netif *netif_byname(struct netif *netifs, char *name) {
    struct netif *netif;

    for (netif = netifs; netif != NULL; netif = netif->next) {
	if (strcmp(netif->name, name) == 0)
	    break;
    }
    return(netif);
}

int read_line(char *path, char *line, uint16_t len) {
    FILE *file;

    if ((file = fopen(path, "r")) == NULL)
	return(-1);

    if (fgets(line, len, file) == NULL) {
	fclose(file);
	return(-1);
    }
    fclose(file);

    // remove newline
    *strchr(line, '\n') = '\0';

    return(strlen(line));
}

#ifndef HAVE_STRLCPY
/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}

#endif

/*
 $Id$
*/

#include "main.h"
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_NETPACKET_PACKET_H
#include <netpacket/packet.h>
#endif /* HAVE_NETPACKET_PACKET_H */
#ifdef HAVE_NET_BPF_H
#include <net/bpf.h>
#endif /* HAVE_NET_BPF_H */

unsigned int loglevel = 0;
extern int do_fork;

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
	my_log(0, "malloc failed: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }
    bzero(ptr, size);
    return(ptr);
}

char * my_strdup(const char *str) {
    char *cstr;

    if ((cstr = strdup(str)) == NULL) {
	my_log(0, "strdup failed: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }
    return(cstr);
}

int my_ioctl(int fd, int request, void *arg) {
    int n;

    if ((n = ioctl(fd, request, arg)) == -1) {
	my_log(0, "ioctl error: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }
    return(n);      /* streamio of I_LIST returns value */
}

int my_socket(int af, int type, int proto) {
    int s;

    if ((s = socket(af, type, proto)) < 0) {
	my_log(0, "opening socket failed: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }
    return(s);
}

int my_rsocket(const char *if_name) {

    int socket = -1;

#ifdef HAVE_NETPACKET_PACKET_H
    socket = my_socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
#elif HAVE_NET_BPF_H
    socket = open("/dev/bpf", O_WRONLY);
#endif

    return(socket);
}

int my_rsend(struct session *session, const void *msg, size_t len) {

    size_t count = 0;

#ifdef HAVE_NETPACKET_PACKET_H
    struct sockaddr_ll sa;
    bzero(&sa, sizeof (sa));

    sa.sll_family = AF_PACKET;
    sa.sll_ifindex = session->if_index;
    sa.sll_protocol = htons(ETH_P_ALL);

    count = sendto(session->sockfd, msg, len, 0,
		   (struct sockaddr *)&sa, sizeof (sa));
#elif HAVE_NET_BPF_H
    count = write(session->sockfd, msg, len);
#endif

    if (count != len)
	my_log(0, "only %d bytes written: %s", count, strerror(errno));
    
    return(count);
}

struct session *session_byname(struct session *sessions, char *name) {
    struct session *session;

    for (session = sessions; session != NULL; session = session->next) {
	if (strcmp(session->if_name, name) == 0)
	    break;
    }
    return(session);
}

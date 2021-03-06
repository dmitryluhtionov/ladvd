/*
 * $Id$
 *
 * Copyright (c) 2008, 2009, 2010
 *      Sten Spans <sten@blinkenlights.nl>
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

#include "common.h"
#include "util.h"
#include "proto/protos.h"
#include "main.h"
#include <sys/file.h>
#include <ctype.h>
#include <syslog.h>
#include <fcntl.h>

uint32_t options = OPT_DAEMON | OPT_SEND;
extern struct my_sysinfo sysinfo;
extern struct ehead exclifs;
extern char *__progname;

static void usage() __noreturn;

int main(int argc, char *argv[]) {

    int ch, i;
    char *username = PACKAGE_USER;
#ifndef __APPLE__
    char pidstr[16];
    int fd = -1;
#endif /* __APPLE__ */
    struct passwd *pwd = NULL;

    // save argc/argv
    int sargc;
    char **sargv;

    // sockets
    int cpair[2], mpair[2];

    // pids
    extern pid_t pid;
#ifndef __APPLE__
    struct flock lock = { .l_type = F_WRLCK };
#endif /* __APPLE__ */

    // exclude interface support
    TAILQ_INIT(&exclifs);
    struct exclif *exclif = NULL;

    // clear sysinfo
    memset(&sysinfo, 0, sizeof(struct my_sysinfo));
    sysinfo.lldpmed_devtype = -1;

    // cli
    if (strcmp(__progname, PACKAGE_CLI) == 0)
	cli_main(argc, argv);

    // Save argv. Duplicate so setproctitle emulation doesn't clobber it
    sargc = argc;
    sargv = my_calloc(argc + 1, sizeof(*sargv));
    for (i = 0; i < argc; i++)
	sargv[i] = my_strdup(argv[i]);
    sargv[i] = NULL;

#ifndef HAVE_SETPROCTITLE
    /* Prepare for later setproctitle emulation */
    compat_init_setproctitle(argc, argv);
    argv = sargv;
#endif

    while ((ch = getopt(argc, argv, "ade:fhm:noqrstu:vwyzc:l:LCEFN")) != -1) {
	switch(ch) {
	    case 'a':
		options |= OPT_AUTO | OPT_RECV;
		break;
	    case 'd':
		options |= OPT_DEBUG;
		options &= ~OPT_DAEMON;
		break;
	    case 'e':
		// excellent we got an ifindex
		if (if_nametoindex(optarg)) {
		    exclif = my_malloc(sizeof(struct exclif));
		    TAILQ_INSERT_TAIL(&exclifs, exclif, entries);
		    strlcpy(exclif->name, optarg, IFNAMSIZ);
		} else {
		    my_log(CRIT, "invalid exclude interface %s", optarg);
		    usage();
		}
		break;
	    case 'f':
		options &= ~OPT_DAEMON;
		break;
	    case 'm':
		options |= OPT_MNETIF;
		// excellent we got an ifindex
		if (if_nametoindex(optarg)) {
		    sysinfo.mifname = my_strdup(optarg);
		    break;
		} 
		// obsolete
		if ( (inet_pton(AF_INET, optarg, &sysinfo.maddr4) != 1) &&
		     (inet_pton(AF_INET6, optarg, &sysinfo.maddr6) != 1) ) {
		    my_log(CRIT, "invalid management address %s", optarg);
		    usage();
		}
		my_log(WARN, "please specify a management interface, "
			     "not an addr");
		break;
	    case 'n':
		options |= OPT_MADDR;
		break;
	    case 'o':
		options |= OPT_ONCE;
		break;
	    case 'q':
		options |= OPT_CHASSIS_IF;
		break;
	    case 'r':
		options |= OPT_RECV;
		break;
	    case 's':
		options &= ~OPT_SEND;
		break;
	    case 't':
		options |= OPT_TAP;
		break;
	    case 'u':
		username = optarg;
		break;
	    case 'v':
		loglevel++;
		break;
	    case 'w':
		options |= OPT_WIRELESS;
		break;
#if defined(SIOCSIFDESCR) || defined(HAVE_SYSFS)
	    case 'y':
		options |= OPT_USEDESCR;
	    case 'z':
		options |= OPT_RECV | OPT_IFDESCR;
		break;
#else
		my_log(CRIT, "ifdescr support not available");
		usage();
#endif /* SIOCSIFDESCR */
	    case 'c':
		// two-letter ISO 3166 country code
		if (strlen(optarg) != 2)
		    usage();
		// in capital ASCII letters
		sysinfo.country[0] = toupper(optarg[0]);
		sysinfo.country[1] = toupper(optarg[1]);
		break;
	    case 'l':
		if (strlcpy(sysinfo.location, optarg, 
			sizeof(sysinfo.location)) == 0)
		    usage();
		break;
	    case 'L':
		protos[PROTO_LLDP].enabled = 1;
		break;
	    case 'C':
		protos[PROTO_CDP].enabled = 1;
		break;
	    case 'E':
		protos[PROTO_EDP].enabled = 1;
		break;
	    case 'F':
		protos[PROTO_FDP].enabled = 1;
		break;
	    case 'N':
		protos[PROTO_NDP].enabled = 1;
		break;
	    default:
		usage();
	}
    }

    sargc -= optind;
    sargv += optind;

    // set argv option
    if (sargc)
	options |= OPT_ARGV;

    // validate protocols
    if (!(options & OPT_AUTO)) {
	int enabled = 0;
	for (int p = 0; protos[p].name != NULL; p++)
	    enabled |= protos[p].enabled;

	if (enabled == 0) {
	    my_log(CRIT, "no protocols enabled");
	    usage();
	}
    }

    // validate username
    if (!(options & OPT_DEBUG) && (pwd = getpwnam(username)) == NULL)
	my_fatal("user %s does not exist", username);

    // fetch system details
    sysinfo_fetch(&sysinfo);

    if (options & OPT_DAEMON) {
#ifndef __APPLE__
	// run in the background
	if (daemon(0,0) == -1)
	    my_fatale("backgrounding failed");

	// create pidfile
	fd = open(PACKAGE_PID_FILE, O_WRONLY|O_CREAT, 0666);
	if (fd == -1)
	    my_fatale("failed to open pidfile " PACKAGE_PID_FILE);
	if (fcntl(fd, F_SETLK, &lock) == -1)
	    my_fatal(PACKAGE_NAME " already running ("
			PACKAGE_PID_FILE " locked)");

	if ((snprintf(pidstr, sizeof(pidstr), "%d\n", (int)getpid()) <= 0) ||
	    (write(fd, pidstr, strlen(pidstr)) <= 0))
	    my_fatale("failed to write pidfile " PACKAGE_PID_FILE);
#endif /* __APPLE__ */
    
	// init syslog before chrooting (including tz)
	tzset();
	openlog(PACKAGE_NAME, LOG_NDELAY, LOG_DAEMON);
    }

    // init cmd/msg socketpair
    my_socketpair(cpair);
    my_socketpair(mpair);

    // create privsep parent / child
    pid = fork();

    // Instead, only try to realize the truth: there is no fork
    if (pid == -1)
	my_fatale("privsep fork failed");

    // this is the parent
    if (pid != 0) {

	// cleanup
	close(cpair[0]);
	close(mpair[0]);

	// enter the parent loop
	parent_init(cpair[1], mpair[1], pid);

	// not reached
	my_fatal("parent process failed");

    } else {

	// cleanup
	close(cpair[1]);
	close(mpair[1]);

	// enter the child loop
	child_init(cpair[0], mpair[0], sargc, sargv, pwd);

	// not reached
	my_fatal("child process failed");
    }

    // not reached
    return (EXIT_FAILURE);
}


__noreturn
static void usage() {

    fprintf(stderr, PACKAGE_NAME " version " PACKAGE_VERSION "\n" 
	"Usage: %s [-a] [INTERFACE] [INTERFACE]\n"
	    "\t-a = Auto-enable protocols based on received packets\n"
	    "\t-d = Dump pcap-compatible packets to stdout\n"
	    "\t-e <interface> = Exclude this interface\n"
	    "\t-f = Run in the foreground\n"
	    "\t-h = Print this message\n"
	    "\t-m <interface> = Management interface\n"
	    "\t-n = Use addresses of mgmt interface for all interfaces\n"
	    "\t-o = Run Once\n"
	    "\t-q = Generate per-interface chassis-id values\n"
	    "\t-r = Receive Packets\n"
	    "\t-s = Silent, don't transmit packets\n"
	    "\t-t = Use Tun/Tap interfaces\n"
	    "\t-u <user> = Setuid User (defaults to " PACKAGE_USER ")\n"
	    "\t-v = Increase logging verbosity\n"
	    "\t-w = Use wireless interfaces\n"
#if defined(SIOCSIFDESCR) || defined(HAVE_SYSFS)
	    /* everything is cooler with a z */
	    "\t-z = Save received info in interface description / alias\n"
#endif /* SIOCSIFDESCR */
	    "\t-c <CC> = System Country Code\n"
	    "\t-l <location> = System Location\n"
	    "\t-L = Enable LLDP\n"
	    "\t-C = Enable CDP\n"
	    "\t-E = Enable EDP\n"
	    "\t-F = Enable FDP\n"
	    "\t-N = Enable NDP\n",
	    __progname);

    exit(EXIT_FAILURE);
}


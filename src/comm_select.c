
/*
 * $Id: comm_select.c,v 1.54 2001/12/24 15:33:42 adrian Exp $
 *
 * DEBUG: section 5     Socket Functions
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "squid.h"

#ifdef USE_SELECT

static int MAX_POLL_TIME = 1000;	/* see also comm_quick_poll_required() */

#ifndef        howmany
#define howmany(x, y)   (((x)+((y)-1))/(y))
#endif
#ifndef        NBBY
#define        NBBY    8
#endif
#define FD_MASK_BYTES sizeof(fd_mask)
#define FD_MASK_BITS (FD_MASK_BYTES*NBBY)

/* STATIC */
static int examine_select(fd_set *, fd_set *);
static int fdIsHttp(int fd);
static int fdIsIcp(int fd);
static int fdIsDns(int fd);
static OBJH commIncomingStats;
static int comm_check_incoming_select_handlers(int nfds, int *fds);
static void comm_select_dns_incoming(void);
static void commUpdateReadBits(int fd, PF * handler);
static void commUpdateWriteBits(int fd, PF * handler);


static struct timeval zero_tv;
static fd_set global_readfds;
static fd_set global_writefds;
static int nreadfds;
static int nwritefds;

/*
 * Automatic tuning for incoming requests:
 *
 * INCOMING sockets are the ICP and HTTP ports.  We need to check these
 * fairly regularly, but how often?  When the load increases, we
 * want to check the incoming sockets more often.  If we have a lot
 * of incoming ICP, then we need to check these sockets more than
 * if we just have HTTP.
 *
 * The variables 'incoming_icp_interval' and 'incoming_http_interval' 
 * determine how many normal I/O events to process before checking
 * incoming sockets again.  Note we store the incoming_interval
 * multipled by a factor of (2^INCOMING_FACTOR) to have some
 * pseudo-floating point precision.
 *
 * The variable 'icp_io_events' and 'http_io_events' counts how many normal
 * I/O events have been processed since the last check on the incoming
 * sockets.  When io_events > incoming_interval, its time to check incoming
 * sockets.
 *
 * Every time we check incoming sockets, we count how many new messages
 * or connections were processed.  This is used to adjust the
 * incoming_interval for the next iteration.  The new incoming_interval
 * is calculated as the current incoming_interval plus what we would
 * like to see as an average number of events minus the number of
 * events just processed.
 *
 *  incoming_interval = incoming_interval + target_average - number_of_events_processed
 *
 * There are separate incoming_interval counters for both HTTP and ICP events
 * 
 * You can see the current values of the incoming_interval's, as well as
 * a histogram of 'incoming_events' by asking the cache manager
 * for 'comm_incoming', e.g.:
 *
 *      % ./client mgr:comm_incoming
 *
 * Caveats:
 *
 *      - We have MAX_INCOMING_INTEGER as a magic upper limit on
 *        incoming_interval for both types of sockets.  At the
 *        largest value the cache will effectively be idling.
 *
 *      - The higher the INCOMING_FACTOR, the slower the algorithm will
 *        respond to load spikes/increases/decreases in demand. A value
 *        between 3 and 8 is recommended.
 */

#define MAX_INCOMING_INTEGER 256
#define INCOMING_FACTOR 5
#define MAX_INCOMING_INTERVAL (MAX_INCOMING_INTEGER << INCOMING_FACTOR)
static int icp_io_events = 0;
static int dns_io_events = 0;
static int http_io_events = 0;
static int incoming_icp_interval = 16 << INCOMING_FACTOR;
static int incoming_dns_interval = 16 << INCOMING_FACTOR;
static int incoming_http_interval = 16 << INCOMING_FACTOR;
#define commCheckICPIncoming (++icp_io_events > (incoming_icp_interval>> INCOMING_FACTOR))
#define commCheckDNSIncoming (++dns_io_events > (incoming_dns_interval>> INCOMING_FACTOR))
#define commCheckHTTPIncoming (++http_io_events > (incoming_http_interval>> INCOMING_FACTOR))

void
commSetSelect(int fd, unsigned int type, PF * handler, void *client_data,
  time_t timeout)
{
    fde *F = &fd_table[fd];
    assert(fd >= 0);
    assert(F->flags.open);
    debug(5, 5) ("commSetSelect: FD %d type %d\n", fd, type);
    if (type & COMM_SELECT_READ) {
        F->read_handler = handler;
        F->read_data = client_data;
        commUpdateReadBits(fd, handler);
    }
    if (type & COMM_SELECT_WRITE) {
        F->write_handler = handler;
        F->write_data = client_data;
        commUpdateWriteBits(fd, handler);
    }
    if (timeout)
        F->timeout = squid_curtime + timeout;
}


static int
fdIsIcp(int fd)
{
    if (fd == theInIcpConnection)
	return 1;
    if (fd == theOutIcpConnection)
	return 1;
    return 0;
}

static int
fdIsDns(int fd)
{
    if (fd == DnsSocket)
	return 1;
    return 0;
}

static int
fdIsHttp(int fd)
{
    int j;
    for (j = 0; j < NHttpSockets; j++) {
	if (fd == HttpSockets[j])
	    return 1;
    }
    return 0;
}

#if DELAY_POOLS
static int slowfdcnt = 0;
static int slowfdarr[SQUID_MAXFD];

static void
commAddSlowFd(int fd)
{
    assert(slowfdcnt < SQUID_MAXFD);
    slowfdarr[slowfdcnt++] = fd;
}

static int
commGetSlowFd(void)
{
    int whichfd, retfd;

    if (!slowfdcnt)
	return -1;
    whichfd = squid_random() % slowfdcnt;
    retfd = slowfdarr[whichfd];
    slowfdarr[whichfd] = slowfdarr[--slowfdcnt];
    return retfd;
}
#endif


static int
comm_check_incoming_select_handlers(int nfds, int *fds)
{
    int i;
    int fd;
    int maxfd = 0;
    PF *hdl = NULL;
    fd_set read_mask;
    fd_set write_mask;
    FD_ZERO(&read_mask);
    FD_ZERO(&write_mask);
    incoming_sockets_accepted = 0;
    for (i = 0; i < nfds; i++) {
	fd = fds[i];
	if (fd_table[fd].read_handler) {
	    FD_SET(fd, &read_mask);
	    if (fd > maxfd)
		maxfd = fd;
	}
	if (fd_table[fd].write_handler) {
	    FD_SET(fd, &write_mask);
	    if (fd > maxfd)
		maxfd = fd;
	}
    }
    if (maxfd++ == 0)
	return -1;
#if !ALARM_UPDATES_TIME
    getCurrentTime();
#endif
    statCounter.syscalls.selects++;
    if (select(maxfd, &read_mask, &write_mask, NULL, &zero_tv) < 1)
	return incoming_sockets_accepted;
    for (i = 0; i < nfds; i++) {
	fd = fds[i];
	if (FD_ISSET(fd, &read_mask)) {
	    if ((hdl = fd_table[fd].read_handler) != NULL) {
		fd_table[fd].read_handler = NULL;
		commUpdateReadBits(fd, NULL);
		hdl(fd, fd_table[fd].read_data);
	    } else {
		debug(5, 1) ("comm_select_incoming: FD %d NULL read handler\n",
		    fd);
	    }
	}
	if (FD_ISSET(fd, &write_mask)) {
	    if ((hdl = fd_table[fd].write_handler) != NULL) {
		fd_table[fd].write_handler = NULL;
		commUpdateWriteBits(fd, NULL);
		hdl(fd, fd_table[fd].write_data);
	    } else {
		debug(5, 1) ("comm_select_incoming: FD %d NULL write handler\n",
		    fd);
	    }
	}
    }
    return incoming_sockets_accepted;
}

static void
comm_select_icp_incoming(void)
{
    int nfds = 0;
    int fds[2];
    int nevents;
    icp_io_events = 0;
    if (theInIcpConnection >= 0)
	fds[nfds++] = theInIcpConnection;
    if (theInIcpConnection != theOutIcpConnection)
	if (theOutIcpConnection >= 0)
	    fds[nfds++] = theOutIcpConnection;
    if (nfds == 0)
	return;
    nevents = comm_check_incoming_select_handlers(nfds, fds);
    incoming_icp_interval += Config.comm_incoming.icp_average - nevents;
    if (incoming_icp_interval < 0)
	incoming_icp_interval = 0;
    if (incoming_icp_interval > MAX_INCOMING_INTERVAL)
	incoming_icp_interval = MAX_INCOMING_INTERVAL;
    if (nevents > INCOMING_ICP_MAX)
	nevents = INCOMING_ICP_MAX;
    statHistCount(&statCounter.comm_icp_incoming, nevents);
}

static void
comm_select_http_incoming(void)
{
    int nfds = 0;
    int fds[MAXHTTPPORTS];
    int j;
    int nevents;
    http_io_events = 0;
    for (j = 0; j < NHttpSockets; j++) {
	if (HttpSockets[j] < 0)
	    continue;
	if (commDeferRead(HttpSockets[j]))
	    continue;
	fds[nfds++] = HttpSockets[j];
    }
    nevents = comm_check_incoming_select_handlers(nfds, fds);
    incoming_http_interval += Config.comm_incoming.http_average - nevents;
    if (incoming_http_interval < 0)
	incoming_http_interval = 0;
    if (incoming_http_interval > MAX_INCOMING_INTERVAL)
	incoming_http_interval = MAX_INCOMING_INTERVAL;
    if (nevents > INCOMING_HTTP_MAX)
	nevents = INCOMING_HTTP_MAX;
    statHistCount(&statCounter.comm_http_incoming, nevents);
}

#define DEBUG_FDBITS 0
/* Select on all sockets; call handlers for those that are ready. */
int
comm_select(int msec)
{
    fd_set readfds;
    fd_set pendingfds;
    fd_set writefds;
#if DELAY_POOLS
    fd_set slowfds;
#endif
    PF *hdl = NULL;
    int fd;
    int maxfd;
    int num;
    int pending;
    int callicp = 0, callhttp = 0;
    int calldns = 0;
    int maxindex;
    int k;
    int j;
#if DEBUG_FDBITS
    int i;
#endif
    fd_mask *fdsp;
    fd_mask *pfdsp;
    fd_mask tmask;
    static time_t last_timeout = 0;
    struct timeval poll_time;
    double timeout = current_dtime + (msec / 1000.0);
    fde *F;
    do {
#if !ALARM_UPDATES_TIME
	getCurrentTime();
#endif
#if DELAY_POOLS
	FD_ZERO(&slowfds);
#endif
	/* Handle any fs callbacks that need doing */
	storeDirCallback();
	if (commCheckICPIncoming)
	    comm_select_icp_incoming();
	if (commCheckDNSIncoming)
	    comm_select_dns_incoming();
	if (commCheckHTTPIncoming)
	    comm_select_http_incoming();
	callicp = calldns = callhttp = 0;
	maxfd = Biggest_FD + 1;
	xmemcpy(&readfds, &global_readfds,
	    howmany(maxfd, FD_MASK_BITS) * FD_MASK_BYTES);
	xmemcpy(&writefds, &global_writefds,
	    howmany(maxfd, FD_MASK_BITS) * FD_MASK_BYTES);
	/* remove stalled FDs, and deal with pending descriptors */
	pending = 0;
	maxindex = howmany(maxfd, FD_MASK_BITS);
	fdsp = (fd_mask *) & readfds;
	for (j = 0; j < maxindex; j++) {
	    if ((tmask = fdsp[j]) == 0)
		continue;	/* no bits here */
	    for (k = 0; k < FD_MASK_BITS; k++) {
		if (!EBIT_TEST(tmask, k))
		    continue;
		/* Found a set bit */
		fd = (j * FD_MASK_BITS) + k;
		switch (commDeferRead(fd)) {
		case 0:
		    break;
		case 1:
		    FD_CLR(fd, &readfds);
		    break;
#if DELAY_POOLS
		case -1:
		    FD_SET(fd, &slowfds);
		    break;
#endif
		default:
		    fatalf("bad return value from commDeferRead(FD %d)\n", fd);
		}
		if (FD_ISSET(fd, &readfds) && fd_table[fd].flags.read_pending) {
		    FD_SET(fd, &pendingfds);
		    pending++;
		}
	    }
	}
#if DEBUG_FDBITS
	for (i = 0; i < maxfd; i++) {
	    /* Check each open socket for a handler. */
#if DELAY_POOLS
	    if (fd_table[i].read_handler && commDeferRead(i) != 1) {
#else
	    if (fd_table[i].read_handler && !commDeferRead(i)) {
#endif
		assert(FD_ISSET(i, &readfds));
	    }
	    if (fd_table[i].write_handler) {
		assert(FD_ISSET(i, &writefds));
	    }
	}
#endif
	if (nreadfds + nwritefds == 0) {
	    assert(shutting_down);
	    return COMM_SHUTDOWN;
	}
	if (msec > MAX_POLL_TIME)
	    msec = MAX_POLL_TIME;
#ifdef _SQUID_OS2_
	if (msec < 0)
	    msec = MAX_POLL_TIME;
#endif
	if (pending)
	    msec = 0;
	for (;;) {
	    poll_time.tv_sec = msec / 1000;
	    poll_time.tv_usec = (msec % 1000) * 1000;
	    statCounter.syscalls.selects++;
	    num = select(maxfd, &readfds, &writefds, NULL, &poll_time);
	    statCounter.select_loops++;
	    if (num >= 0 || pending > 0)
		break;
	    if (ignoreErrno(errno))
		break;
	    debug(50, 0) ("comm_select: select failure: %s\n",
		xstrerror());
	    examine_select(&readfds, &writefds);
	    return COMM_ERROR;
	    /* NOTREACHED */
	}
	if (num < 0 && !pending)
	    continue;
	debug(5, num ? 5 : 8) ("comm_select: %d+%d FDs ready at %d\n",
	    num, pending, (int) squid_curtime);
	statHistCount(&statCounter.select_fds_hist, num);
	/* Check lifetime and timeout handlers ONCE each second.
	 * Replaces brain-dead check every time through the loop! */
	if (squid_curtime > last_timeout) {
	    last_timeout = squid_curtime;
	    checkTimeouts();
	}
	if (num == 0 && pending == 0)
	    continue;
	/* Scan return fd masks for ready descriptors */
	fdsp = (fd_mask *) & readfds;
	pfdsp = (fd_mask *) & pendingfds;
	maxindex = howmany(maxfd, FD_MASK_BITS);
	for (j = 0; j < maxindex; j++) {
	    if ((tmask = (fdsp[j] | pfdsp[j])) == 0)
		continue;	/* no bits here */
	    for (k = 0; k < FD_MASK_BITS; k++) {
		if (tmask == 0)
		    break;	/* no more bits left */
		if (!EBIT_TEST(tmask, k))
		    continue;
		/* Found a set bit */
		fd = (j * FD_MASK_BITS) + k;
		EBIT_CLR(tmask, k);	/* this will be done */
#if DEBUG_FDBITS
		debug(5, 9) ("FD %d bit set for reading\n", fd);
		assert(FD_ISSET(fd, &readfds));
#endif
		if (fdIsIcp(fd)) {
		    callicp = 1;
		    continue;
		}
		if (fdIsDns(fd)) {
		    calldns = 1;
		    continue;
		}
		if (fdIsHttp(fd)) {
		    callhttp = 1;
		    continue;
		}
		F = &fd_table[fd];
		debug(5, 6) ("comm_select: FD %d ready for reading\n", fd);
		if (NULL == (hdl = F->read_handler))
		    (void) 0;
#if DELAY_POOLS
		else if (FD_ISSET(fd, &slowfds))
		    commAddSlowFd(fd);
#endif
		else {
		    F->read_handler = NULL;
		    commUpdateReadBits(fd, NULL);
		    hdl(fd, F->read_data);
		    statCounter.select_fds++;
		    if (commCheckICPIncoming)
			comm_select_icp_incoming();
		    if (commCheckDNSIncoming)
			comm_select_dns_incoming();
		    if (commCheckHTTPIncoming)
			comm_select_http_incoming();
		}
	    }
	}
	fdsp = (fd_mask *) & writefds;
	for (j = 0; j < maxindex; j++) {
	    if ((tmask = fdsp[j]) == 0)
		continue;	/* no bits here */
	    for (k = 0; k < FD_MASK_BITS; k++) {
		if (tmask == 0)
		    break;	/* no more bits left */
		if (!EBIT_TEST(tmask, k))
		    continue;
		/* Found a set bit */
		fd = (j * FD_MASK_BITS) + k;
		EBIT_CLR(tmask, k);	/* this will be done */
#if DEBUG_FDBITS
		debug(5, 9) ("FD %d bit set for writing\n", fd);
		assert(FD_ISSET(fd, &writefds));
#endif
		if (fdIsIcp(fd)) {
		    callicp = 1;
		    continue;
		}
		if (fdIsDns(fd)) {
		    calldns = 1;
		    continue;
		}
		if (fdIsHttp(fd)) {
		    callhttp = 1;
		    continue;
		}
		F = &fd_table[fd];
		debug(5, 5) ("comm_select: FD %d ready for writing\n", fd);
		if ((hdl = F->write_handler)) {
		    F->write_handler = NULL;
		    commUpdateWriteBits(fd, NULL);
		    hdl(fd, F->write_data);
		    statCounter.select_fds++;
		    if (commCheckICPIncoming)
			comm_select_icp_incoming();
		    if (commCheckDNSIncoming)
			comm_select_dns_incoming();
		    if (commCheckHTTPIncoming)
			comm_select_http_incoming();
		}
	    }
	}
	if (callicp)
	    comm_select_icp_incoming();
	if (calldns)
	    comm_select_dns_incoming();
	if (callhttp)
	    comm_select_http_incoming();
#if DELAY_POOLS
	while ((fd = commGetSlowFd()) != -1) {
	    F = &fd_table[fd];
	    debug(5, 6) ("comm_select: slow FD %d selected for reading\n", fd);
	    if ((hdl = F->read_handler)) {
		F->read_handler = NULL;
		commUpdateReadBits(fd, NULL);
		hdl(fd, F->read_data);
		statCounter.select_fds++;
		if (commCheckICPIncoming)
		    comm_select_icp_incoming();
		if (commCheckDNSIncoming)
		    comm_select_dns_incoming();
		if (commCheckHTTPIncoming)
		    comm_select_http_incoming();
	    }
	}
#endif
	return COMM_OK;
    }
    while (timeout > current_dtime);
    debug(5, 8) ("comm_select: time out: %d\n", (int) squid_curtime);
    return COMM_TIMEOUT;
}

static void
comm_select_dns_incoming(void)
{
    int nfds = 0;
    int fds[2];
    int nevents;
    dns_io_events = 0;
    if (DnsSocket < 0)
	return;
    fds[nfds++] = DnsSocket;
    nevents = comm_check_incoming_select_handlers(nfds, fds);
    if (nevents < 0)
	return;
    incoming_dns_interval += Config.comm_incoming.dns_average - nevents;
    if (incoming_dns_interval < Config.comm_incoming.dns_min_poll)
	incoming_dns_interval = Config.comm_incoming.dns_min_poll;
    if (incoming_dns_interval > MAX_INCOMING_INTERVAL)
	incoming_dns_interval = MAX_INCOMING_INTERVAL;
    if (nevents > INCOMING_DNS_MAX)
	nevents = INCOMING_DNS_MAX;
    statHistCount(&statCounter.comm_dns_incoming, nevents);
}

void
comm_select_init(void)
{
    zero_tv.tv_sec = 0;
    zero_tv.tv_usec = 0;
    cachemgrRegister("comm_incoming",
	"comm_incoming() stats",
	commIncomingStats, 0, 1);
    FD_ZERO(&global_readfds);
    FD_ZERO(&global_writefds);
    nreadfds = nwritefds = 0;
}

/*
 * examine_select - debug routine.
 *
 * I spend the day chasing this core dump that occurs when both the client
 * and the server side of a cache fetch simultaneoulsy abort the
 * connection.  While I haven't really studied the code to figure out how
 * it happens, the snippet below may prevent the cache from exitting:
 * 
 * Call this from where the select loop fails.
 */
static int
examine_select(fd_set * readfds, fd_set * writefds)
{
    int fd = 0;
    fd_set read_x;
    fd_set write_x;
    struct timeval tv;
    close_handler *ch = NULL;
    fde *F = NULL;
    struct stat sb;
    debug(5, 0) ("examine_select: Examining open file descriptors...\n");
    for (fd = 0; fd < Squid_MaxFD; fd++) {
	FD_ZERO(&read_x);
	FD_ZERO(&write_x);
	tv.tv_sec = tv.tv_usec = 0;
	if (FD_ISSET(fd, readfds))
	    FD_SET(fd, &read_x);
	else if (FD_ISSET(fd, writefds))
	    FD_SET(fd, &write_x);
	else
	    continue;
	statCounter.syscalls.selects++;
	errno = 0;
	if (!fstat(fd, &sb)) {
	    debug(5, 5) ("FD %d is valid.\n", fd);
	    continue;
	}
	F = &fd_table[fd];
	debug(5, 0) ("FD %d: %s\n", fd, xstrerror());
	debug(5, 0) ("WARNING: FD %d has handlers, but it's invalid.\n", fd);
	debug(5, 0) ("FD %d is a %s called '%s'\n",
	    fd,
	    fdTypeStr[F->type],
	    F->desc);
	debug(5, 0) ("tmout:%p read:%p write:%p\n",
	    F->timeout_handler,
	    F->read_handler,
	    F->write_handler);
	for (ch = F->close_handler; ch; ch = ch->next)
	    debug(5, 0) (" close handler: %p\n", ch->handler);
	if (F->close_handler) {
	    commCallCloseHandlers(fd);
	} else if (F->timeout_handler) {
	    debug(5, 0) ("examine_select: Calling Timeout Handler\n");
	    F->timeout_handler(fd, F->timeout_data);
	}
	F->close_handler = NULL;
	F->timeout_handler = NULL;
	F->read_handler = NULL;
	F->write_handler = NULL;
	FD_CLR(fd, readfds);
	FD_CLR(fd, writefds);
    }
    return 0;
}


static void
commIncomingStats(StoreEntry * sentry)
{
    StatCounters *f = &statCounter;
    storeAppendPrintf(sentry, "Current incoming_icp_interval: %d\n",
	incoming_icp_interval >> INCOMING_FACTOR);
    storeAppendPrintf(sentry, "Current incoming_dns_interval: %d\n",
	incoming_dns_interval >> INCOMING_FACTOR);
    storeAppendPrintf(sentry, "Current incoming_http_interval: %d\n",
	incoming_http_interval >> INCOMING_FACTOR);
    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "Histogram of events per incoming socket type\n");
    storeAppendPrintf(sentry, "ICP Messages handled per comm_select_icp_incoming() call:\n");
    statHistDump(&f->comm_icp_incoming, sentry, statHistIntDumper);
    storeAppendPrintf(sentry, "DNS Messages handled per comm_select_dns_incoming() call:\n");
    statHistDump(&f->comm_dns_incoming, sentry, statHistIntDumper);
    storeAppendPrintf(sentry, "HTTP Messages handled per comm_select_http_incoming() call:\n");
    statHistDump(&f->comm_http_incoming, sentry, statHistIntDumper);
}

void
commUpdateReadBits(int fd, PF * handler)
{
    if (handler && !FD_ISSET(fd, &global_readfds)) {
	FD_SET(fd, &global_readfds);
	nreadfds++;
    } else if (!handler && FD_ISSET(fd, &global_readfds)) {
	FD_CLR(fd, &global_readfds);
	nreadfds--;
    }
}

void
commUpdateWriteBits(int fd, PF * handler)
{
    if (handler && !FD_ISSET(fd, &global_writefds)) {
	FD_SET(fd, &global_writefds);
	nwritefds++;
    } else if (!handler && FD_ISSET(fd, &global_writefds)) {
	FD_CLR(fd, &global_writefds);
	nwritefds--;
    }
}

/* Called by async-io or diskd to speed up the polling */
void
comm_quick_poll_required(void)
{
    MAX_POLL_TIME = 10;
}

#endif /* USE_SELECT */

/* 
 * $Id: cancon.c,v 1.5 2001/07/31 08:53:58 garlick Exp $
 *
 *    Copyright (C) 2000-2001  Regents of the University of California
 *    See ./DISCLAIMER.
 *
 * Theory of operation:
 * 
 * There is one reader thread, called reader(), that takes packets off the CAN 
 * and processes them.  There are three classes of relevant packets:
 * 1) DAT packets directed at us, which we relay to stdout
 * 2) WO packets directed at the FORCE_DISCONNECT object, which causes
 *    cancon to terminate (see below)
 * 3) ACK/NAK packets, which we make available to writers by adding them
 * to ackbuf[] and signalling acknak_cond.
 * All other packets are ignored.
 *
 * At any time, there is at most one writer thread.  During initialization,
 * this is the main thread calling connect_protocol() and executing the connect/
 * console theft protocol.  After initialization, it is writer(), which 
 * consumes characters from the queue produced by the tty_reader() thread
 * and sends them out as DAT packets.  During termination, it is disconnect(), 
 * which executes the disconnect protocol.
 *
 * The tty_reader() thread just reads chars from stdin and and adds them
 * to a queue.
 * 
 * Termination occurs when the writer detects that the user typed the exit 
 * escape sequence, or when the reader receives a FORCE_DISCONNECT directive.
 * In both cases, the disconnect() thread is started up.  disconnect() kills 
 * the current writer, executes the disconnect protocol, then kills the 
 * reader().  When the main thread joins with the reader, the program exits.
 * 
 * References:
 *   "MK401 SPARC (IO) Board Specification", 1993, Meiko World Inc.
 *   "Overview of the Control Area Network (CAN)", 1995, Meiko World Inc.
 *   "CAN protocols", Meiko World Inc.
 *   /usr/include/sys/canobj.h
 * 
 * Todo: 
 *   support escape-reset
 */

#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <pthread.h>
#include <sys/signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>	/* for uintN_t types */
#include <string.h>
#include <errno.h>
#include "can.h"

#define PKTSIZE		(sizeof(struct can_packet))
#define ESCAPE_CHAR	'&'
#define CANSEND_TMOUT	2	/* seconds */
#define MAX_RETRIES	20
#define ACKBUFLEN	16
#define OUTCHARSLEN	1024

static can_header_ext console_object;
static struct canhostname target_ch;
static int fd;
static int consobj;
static unsigned long nodeid;
static struct canobj resetobj;

typedef enum { NONE, ACK, NAK, TIMEDOUT } acknak_t;


struct ack_pkt {
	can_header_ext ext;
	can_dat dat;
};
	
/* all writers wait on this condition after every send */
static pthread_mutex_t  acknak_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   acknak_cond  = PTHREAD_COND_INITIALIZER;
static struct ack_pkt 	ackbuf[ACKBUFLEN];
static int		ackbuflen = 0;

static pthread_t thread_reader, thread_writer, thread_tty_reader;
static pthread_attr_t attr_reader, attr_writer, attr_tty_reader;

static pthread_mutex_t	outchars_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   outchars_cond  = PTHREAD_COND_INITIALIZER;
static char		outchars[OUTCHARSLEN];
static int		outcharslen = 0;


static int fopt = 0;

static void start_disconnect(void);
static void send_dat(char *buf, int len);

/* 
 * Set a can_header_ext data structure using type and object from parameters,
 * and cluster,module,node from canhostname data structure.
 */
static void 
load_ext_ch(can_header_ext *ext, int tp, int obj, struct canhostname *ch)
{
	ext->ext.type = tp;
	ext->ext.object = obj;
	ext->ext.cluster = ch->cluster;
	ext->ext.module= ch->module;
	ext->ext.node= ch->node;
}

/* 
 * Set a can_header_ext data structure using type and object from command line,
 * and cluster,module,node from my packed nodeid.
 */
static void
load_ext_nodeid(can_header_ext *ext, int tp, int obj)
{
	ext->ext.type = tp;
	ext->ext.object = obj;
	ext->ext.cluster = UNPACK_CLUSTER(nodeid);
	ext->ext.module = UNPACK_MODULE(nodeid);
	ext->ext.node = UNPACK_NODE(nodeid);
}

/*
 * Given the cluster,module,node address contained in a can_header_ext
 * data structure, look up the hostname.
 */
static int
can_gethostbyext(can_header_ext *ext, struct canhostname *ch)
{
	int i = can_gethostbyaddr(ext->ext.cluster, ext->ext.module, 
	    ext->ext.node, ch);
	if (i == -1)
		ch->hostname[0] = '\0';
	return i;
}

/*
 * Create a string representing the address contained in a can_header_ext
 * data structure.  The string is overwritten on each call, and is therefore
 * not MT safe.
 */
static char * 
extaddr_to_str(can_header_ext *ext)
{
	static char tmp[255];
	struct canhostname ch;

	can_gethostbyext(ext, &ch);
	sprintf(tmp, "%s (%-2.2x,%-2.2x,%-2.2x)", ch.hostname,
	    ext->ext.cluster, ext->ext.module, ext->ext.node);
	return tmp;
}


/*
 * Called by reader when ACK or NAK packet is received.  Writer waits on
 * acknak_cond after a write.  We deposit the contents of the ACK or NAK
 * packet in global variables protected by acknak_mutex, and writer will
 * pick them up.
 */
static void 
signal_acknak(can_header_ext *target, can_dat *dat)
{
	pthread_mutex_lock(&acknak_mutex);
	if (ackbuflen < ACKBUFLEN) {
		ackbuf[ackbuflen].ext = *target;
		ackbuf[ackbuflen].dat = *dat;
		ackbuflen++;
		pthread_cond_signal(&acknak_cond);
	}
	pthread_mutex_unlock(&acknak_mutex);
}

#define IS_OUR_ACKNAK(x, y) ((x)->ext.object == (y)->ext.object \
    && (x)->ext.cluster == (y)->ext.cluster \
    && (x)->ext.module == (y)->ext.module \
    && (x)->ext.node == (y)->ext.node)

#define IS_ACKNAK(x)	((x) == CANTYPE_ACK ? ACK : NAK)

/* 
 * Called by the writer after a CAN write to wait for the reader to signal
 * that an ACK or NAK packet has been received that matches the target object.
 * Will also return after a timeout period.  The data argument, if non-NULL,
 * is filled in with any data provided by the ACK or NAK packet.
 */
static acknak_t 
waitfor_acknak(can_header_ext *target, can_dat *dat)
{
	int rv;
	acknak_t response = NONE;
	struct timespec ts;
	struct timeval tv;
	int i;

	gettimeofday(&tv, 0);
	tv.tv_sec += CANSEND_TMOUT;
	TIMEVAL_TO_TIMESPEC(&tv, &ts);

	pthread_mutex_lock(&acknak_mutex);	
	do {
		if (ackbuflen == 0) {
			rv = pthread_cond_timedwait(&acknak_cond, 
			    &acknak_mutex, &ts);
			if (rv == ETIMEDOUT)
				response = TIMEDOUT;
		} 
		for (i = 0; i < ackbuflen; i++) {
			if (IS_OUR_ACKNAK(&ackbuf[i].ext, target)) {
				response = IS_ACKNAK(ackbuf[i].ext.ext.type);
				if (dat != NULL)
					*dat = ackbuf[i].dat;
			}
		}
		ackbuflen = 0;
	} while (response == NONE);
	pthread_mutex_unlock(&acknak_mutex);

	return response;
}

/*
 * Main reader thread, just a loop that reads packets and processes them.  If 
 * an ACK or NAK packet is received, signal the writer.  Process DAT packets 
 * sent to our console object, or WO packets sent to FORCE_DISCONNECT object.
 */
static void *
reader(void *args)
{
	can_header_ext target;
	can_dat dat;
	int len;
	can_dat stolenack = { 1, };
	struct can_packet ack;

	while (can_recv(fd, &target, &dat, &len, &ack) != -1) {
		switch (target.ext.type) {
		case CANTYPE_ACK:
		case CANTYPE_NAK:
			signal_acknak(&target, &dat);
			break;
		case CANTYPE_DAT:
			if (target.ext_dat == console_object.ext_dat) {
				fwrite(&dat.dat_b[0], len, 1, stdout);
				fflush(stdout);
				can_ack(fd, NULL, 0, CANTYPE_ACK, &ack);
			}
			break;
		case CANTYPE_WO:
			if (target.ext.object == CANOBJ_FORCE_DISCONN
				      && dat.dat == console_object.ext_dat) {
				fprintf(stderr, "\r\nConsole has been stolen!");
				can_ack(fd, &stolenack, 4, CANTYPE_ACK, &ack);
				start_disconnect();
			}
			break;
		}
	}
	return NULL;
}

static int  
signal_outchars(char *buf, int len)
{
	int count = 0;

	pthread_mutex_lock(&outchars_mutex);
	while (outcharslen < OUTCHARSLEN && count < len) {
		outchars[outcharslen++] = *buf++;
		count++;
	}
	if (count > 0)
		pthread_cond_signal(&outchars_cond);
	pthread_mutex_unlock(&outchars_mutex);

	return count;
}

#define XMIN(a,b)	((a) < (b) ? (a) : (b))

static int
waitfor_outchars(char *buf, int maxlen)
{
	int count;

	pthread_mutex_lock(&outchars_mutex);	
	if (outcharslen == 0)
		pthread_cond_wait(&outchars_cond, &outchars_mutex);
	count = XMIN(outcharslen, maxlen);
	memmove(buf, outchars, count);
	memmove(outchars, outchars + count, outcharslen - count);
	outcharslen -= count;
	pthread_mutex_unlock(&outchars_mutex);
	
	return count;
}

/*
 * Main writer thread.  
 */
static void *
writer(void *args)
{
	int nbytes;
	char buf[4];

	while (1) {
		nbytes = waitfor_outchars(buf, 4);
		send_dat(buf, nbytes);
	}
}

/* 
 * Display ? help screen.
 */
static void 
print_help(void)
{
	fprintf(stderr, "\r\n-------------------------------\r\n");
	fprintf(stderr, "%c .		disconnect\r\n",ESCAPE_CHAR);
	fprintf(stderr, "%c ?		help\r\n", 	ESCAPE_CHAR);
	fprintf(stderr, "%c #		send break\r\n",ESCAPE_CHAR);
	fprintf(stderr, "%c r		send reset to node H8\r\n",ESCAPE_CHAR);
	fprintf(stderr, "-------------------------------\r\n");
}

/*
 * Called by writer to send a packet and synchronously wait for ACK, NAK, or 
 * timeout.  Retry NAK/timeout MAX_RETRIES times, then return failure to caller
 * who must decide whether to close down the whole session or whatever.
 * Return ACK/NAK data in ack parameter if non-NULL.
 */
static acknak_t 
mysend(can_header_ext *target, can_dat *dat, int len, can_dat *ack, 
		int max_retries)
{
	int try = 0;
	acknak_t response;

	do {
		try++;
		can_send(fd, target, dat, len);

		response = waitfor_acknak(target, ack);

		if (response == NAK)
			usleep(1000);
		else if (response == TIMEDOUT && max_retries > 0)
			fprintf(stderr, ".");

	} while (response != ACK && try < max_retries);

	if (response != ACK && max_retries > 0)
		fprintf(stderr, "\r\ncancon: send aborted after %d tr%s\r\n", 
		    try, try == 1 ? "y" : "ies");

	return response;
}

static acknak_t 
send(can_header_ext *target, can_dat *dat, int len, can_dat *ack)
{
	return mysend(target, dat, len, ack, MAX_RETRIES);
}

static acknak_t 
send_nb(can_header_ext *target, can_dat *dat, int len, can_dat *ack)
{
	return mysend(target, dat, len, ack, 0);
}



/* 
 * Send a DAT packet containing 0-4 characters.
 */
static void 
send_dat(char *buf, int len)
{
	can_header_ext target;
	can_dat dat;

	load_ext_ch(&target, CANTYPE_DAT, 0, &target_ch);
	memcpy(&dat, buf, len);
	send(&target, &dat, len, NULL);
}

/*
 * Send a BREAK packet.
 */
static void 
send_break(void)
{
	can_header_ext target;
	acknak_t response;

	load_ext_ch(&target, CANTYPE_WO, CANOBJ_BREAK, &target_ch);
	response = send(&target, 0, 0, 0);
	if (response == ACK)
		fprintf(stderr, "\r\nSent break.\r\n");
	else
		fprintf(stderr, "CAN giving up sending break.\r\n");
}

/*
 * Send a RESET to node H8.
 */
static void 
send_reset_h8(void)
{
	can_header_ext target;
	acknak_t response;
	can_dat dat = { 2, };

	load_ext_ch(&target, CANTYPE_WO, resetobj.id, &target_ch);
	target.ext.node = CAN_GET_BOARD_H8(target.ext.node);
	response = send(&target, &dat, sizeof(dat), NULL);
	if (response == ACK)
		fprintf(stderr, "\r\nSent reset to H8\r\n");
	else
		fprintf(stderr, "CAN giving up sending reset to H8.\r\n");
}		

#define ISCRNL(c)	((c) == '\n' || (c) == '\r')

static void *
tty_reader(void *args)
{
	int nbytes, i, j, dropped;
	char buf[4], outbuf[8], hist[2] = "\r\r";

	while ((nbytes = read(0, buf, 4)) > 0) {
		for (i = 0, j = 0; i < nbytes; i++) {
			if (ISCRNL(hist[0]) && hist[1] == ESCAPE_CHAR) {
				switch(buf[i]) {
					case '.':
						start_disconnect();
						break;
					case '?':
						print_help();
						break;
					case '#':
						send_break();
						break;
					case 'r':
					case 'R':
						send_reset_h8();
						break;
					case 0x1a: /* ^Z - suspend XXX */
					case '!':  /* !  - shell escape XXX */
					default:
						outbuf[j++] = ESCAPE_CHAR;
						outbuf[j++] = buf[i];
						break;
				}
			} else if (buf[i] != ESCAPE_CHAR || !ISCRNL(hist[1]))
				outbuf[j++] = buf[i];
			hist[0] = hist[1];
			hist[1] = buf[i];
		}
		dropped = j - signal_outchars(outbuf, j);
		if (dropped > 0)
			fprintf(stderr, "\r\ncancon: %d tty chars dropped\r\n",
			    dropped);
	}
	return NULL;
}

/*
 * Send a CONSOLE_CONNECT packet (read or write).
 */
static acknak_t 
send_connect(int type, can_header_ext *cons)
{
	can_header_ext target;
	acknak_t response;	

	load_ext_ch(&target, type, CANOBJ_CONSOLE_CONNECT, &target_ch);
	if (type == CANTYPE_RO)  {
		response = send(&target, NULL, 0, (can_dat *)cons);
	} else {
		response = send(&target, (can_dat *)cons, 4, NULL);
	}
	if (response != ACK)
		fprintf(stderr, "Failed to %s CONSOLE_CONNECT object.\r\n",
		    type == CANTYPE_RO ? "read from" : "write to");
	return response;
}

/*
 * Send a CONSOLE_DISCONN packet (read or write).
 */
static acknak_t 
send_disconnect(can_header_ext *cons)
{
	can_header_ext target;
	acknak_t response;

	load_ext_ch(&target, CANTYPE_WO, CANOBJ_CONSOLE_DISCONN, &target_ch);
	response = send(&target, (can_dat *)cons, 4, NULL);
	if (response != ACK)
		fprintf(stderr, "Failed to write to CONSOLE_DISCONN object.\r\n");
	return response;
}

/*
 * Send a FORCE_DISCONN packet.  Don't block.
 */
static acknak_t 
send_force_disconnect_nb(can_header_ext *cons)
{
	can_header_ext target = *cons;
	acknak_t response;	

	target.ext.type = CANTYPE_WO;
	target.ext.object = CANOBJ_FORCE_DISCONN;
	response = send_nb(&target, (can_dat *)cons, 4, NULL);
	if (response != ACK)
		fprintf(stderr,"No response from other cancon.  Beware...\r\n");
	return response;
}
	
/*
 * Steal a console from another node.
 */
static int 
heist_connection(can_header_ext *oldcon)
{
	int try = 0;

	if (send_force_disconnect_nb(oldcon) != ACK)
		if (send_disconnect(oldcon) != ACK)
			return -1;
	do {
		usleep(1000);
		if (send_connect(CANTYPE_RO, oldcon) != ACK)
			return -1;
		try++;
	} while (!CANCON_UNCONNECTED(*oldcon) && try < 5);

	if (try == 5) {
		if (send_disconnect(oldcon) != ACK)
			return -1;
	}
	return 0;
}


/* 
 * Connect protocol.
 */
static int 
connect_protocol()
{
	can_header_ext oldcon;

	fprintf(stderr, "Connecting...\n");
	if (send_connect(CANTYPE_RO, &oldcon) != ACK)
		return -1;

	/* if console is in use, either quit or try to steal it */
	if (!CANCON_UNCONNECTED(oldcon)) {
		if (fopt) {
			fprintf(stderr, "Stealing console from %s...\n", 
			    extaddr_to_str(&oldcon));
			if (heist_connection(&oldcon) == -1)
				return -1;
		} else {
			fprintf(stderr, "Console is already in use by %s\n",
			    extaddr_to_str(&oldcon));
			return -1;
		}
	}

	load_ext_nodeid(&console_object, CANTYPE_DAT, consobj);
	if (send_connect(CANTYPE_WO, &console_object) != ACK)
		return -1;

	fprintf(stderr, "Connected to %s (%-2.2x,%-2.2x,%-2.2x).  ", 
	    target_ch.hostname, target_ch.cluster, target_ch.module, 
	    target_ch.node);
	fprintf(stderr, "Escape char is `%c'.\n", ESCAPE_CHAR);
	return 0;
}

/*
 * Disconnect from the remote node by writing to the CONSOLE_DISCONN object.
 * Kill the writer and reader threads.  Restore tty modes.
 */
static void *
disconnect(void *args)
{
	fprintf(stderr, "\r\nCleaning up...");
	pthread_kill(thread_tty_reader, SIGTERM);
	pthread_kill(thread_writer, SIGTERM);
	send_disconnect(&console_object);
	pthread_kill(thread_reader, SIGTERM);
	return NULL;
}

/*
 * Start the disconnect() thread asynchronously.  We do this so we can be called
 * by the reader(), but still be able to process ACK/NAK packets from the
 * disconnect protocol.
 */
static void 
start_disconnect()
{
	pthread_t thread_disconnect;
	pthread_attr_t attr_disconnect;
	
	pthread_attr_init(&attr_disconnect);
	pthread_attr_setdetachstate(&attr_disconnect, PTHREAD_CREATE_DETACHED);
        pthread_create(&thread_disconnect, &attr_disconnect, disconnect, NULL);
}

#define MAXFD 8

/*
 * Set the tty associated with fd to raw mode (if raw = 1) or back to the 
 * original mode (if raw = 0).  A no-op if fd is not at tty.  Note that the
 * VMIN/VTIME setting is important to allow fast typists to get up to four
 * characters packed into a CAN packet.  Using a separate packet for each 
 * character, in early tests, resulted in some NAK's when typing really fast.
 * Allowing characters to aggregate seemed to alleviate that problem.
 */
static void
set_tty_mode(int fd, int raw)
{
	static struct termios saved[MAXFD], new;
	
	if (!isatty(fd))
		return;

	assert(fd >= 0 && fd < MAXFD);
	if (raw) {
		tcgetattr(fd, &saved[fd]);
		new = saved[fd];
		cfmakeraw(&new);
		new.c_cc[VMIN] = 4;
		new.c_cc[VTIME] = 1;
		tcsetattr(fd, TCSAFLUSH, &new);
	} else {
		tcsetattr(fd, TCSAFLUSH, &saved[fd]);
	}
}

static void 
usage(void)
{	
	fprintf(stderr, "Usage:  cancon [-f] node\n");
	exit(1);
}

/*
 * Emulate signal() but with BSD semantics (i.e. no need to call again every
 * time the handler is invoked).
 */
static void xsignal(int signal, void (*handler)(int))
{
        struct sigaction sa, old_sa;
 
        sa.sa_handler = handler;
        sigemptyset(&sa.sa_mask);
        sigaddset(&sa.sa_mask, signal);
        sa.sa_flags = 0;
        sigaction(signal, &sa, &old_sa);
}

void sighandler(int dummy)
{
	start_disconnect();
}	

int
main(int argc, char *argv[])
{
	extern int optind;
        extern char *optarg;
	int c;

	while ((c = getopt(argc, argv, "f")) != EOF) {
		switch (c) {
			case 'f':
				fopt = 1;
				break;
			default:
				usage();
		}
	}
	if (optind != argc - 1)
		usage();

	if (can_getobjbyname("RESET", &resetobj) < 0) {
		fprintf(stderr, "cancon: could not look up RESET CAN object\n");
		exit(1);
	}

	fd = open("/dev/can", O_RDWR);
	if (fd < 0) {
		perror("/dev/can");
		exit(1);
	}
	if (ioctl(fd, CAN_GET_ADDR, &nodeid) < 0) {
		perror("ioctl CAN_GET_ADDR");
		return -1;
	}
	if (ioctl(fd, CAN_GET_CONSOBJ, &consobj) < 0) {
		perror("ioctl CAN_GET_CONSOBJ");
		return -1;
	}
	if (can_gethostbyname(argv[optind], &target_ch) == -1) {
		fprintf(stderr, "cancon: %s: unknown can host\n", argv[1]);
		exit(1);
	}
	printf("hostname %s\n", target_ch.hostname); /* XXX */

	pthread_attr_init(&attr_reader);
        pthread_create(&thread_reader, &attr_reader, reader, NULL);

	if (connect_protocol() != -1) {

		xsignal(SIGINT, sighandler);
		xsignal(SIGHUP, sighandler);

		set_tty_mode(0, 1);	/* enter raw tty mode */
		pthread_attr_init(&attr_tty_reader);
		pthread_create(&thread_tty_reader, &attr_tty_reader, 
		    tty_reader, NULL);

		pthread_attr_init(&attr_writer);
		pthread_create(&thread_writer, &attr_writer, writer, NULL);

		pthread_join(thread_tty_reader, NULL);
		set_tty_mode(0, 0);	/* get out of raw mode */

		pthread_join(thread_writer, NULL);

		pthread_join(thread_reader, NULL);
	} 

	exit(0);
}

/*
 * gen_uuid.c --- generate a DCE-compatible uuid
 *
 * Copyright (C) 1996, 1997, 1998, 1999 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU 
 * Library General Public License.
 * %End-Header%
 */

/*
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
*/

#undef _XOPEN_SOURCE

#include "libnetxms.h"
#include "uuidP.h"

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#if HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#if HAVE_NET_IF_H
#include <net/if.h>
#endif
#if HAVE_NET_IF_ARP_H
#include <net/if_arp.h>
#endif
#if HAVE_NET_IF_DL_H
#include <net/if_dl.h>
#endif

#ifdef HAVE_SRANDOM
#define srand(x) 	srandom(x)
#define rand() 		random()
#endif

#ifndef _WIN32

static int get_random_fd()
{
	int fd = -2;
	int	i;

	if (fd == -2)
   {
		fd = open("/dev/urandom", O_RDONLY);
		if (fd == -1)
			fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
		srand((getpid() << 16) ^ getuid() ^ time(0));
	}
	/* Crank the random number generator a few times */
	for (i = time(0) & 0x1F; i > 0; i--)
		rand();
	return fd;
}

#endif


/*
 * Generate a series of random bytes.  Use /dev/urandom if possible,
 * and if not, use srandom/random.
 */

static void get_random_bytes(void *buf, int nbytes)
{
	int i;
	char *cp = (char *)buf;
#ifndef _WIN32
   int fd = get_random_fd();
	int lose_counter = 0;

	if (fd >= 0)
   {
		while (nbytes > 0)
      {
			i = read(fd, cp, nbytes);
			if ((i < 0) &&
			    ((errno == EINTR) || (errno == EAGAIN)))
				continue;
			if (i <= 0) {
				if (lose_counter++ == 8)
					break;
				continue;
			}
			nbytes -= i;
			cp += i;
			lose_counter = 0;
		}
		close(fd);
	}
#endif
	if (nbytes == 0)
		return;

	/* FIXME: put something better here if no /dev/random! */
   srand((unsigned int)time(NULL) ^ getpid());
	for (i = 0; i < nbytes; i++)
		*cp++ = rand() & 0xFF;
	return;
}

/*
 * Get the ethernet hardware address, if we can find it...
 */
static int get_node_id(unsigned char *node_id)
{
#ifdef HAVE_NET_IF_H
	int sd;
	struct ifreq ifr, *ifrp;
	struct ifconf ifc;
	char buf[1024];
	int n, i;
	unsigned char *a;
	
/*
 * BSD 4.4 defines the size of an ifreq to be
 * max(sizeof(ifreq), sizeof(ifreq.ifr_name)+ifreq.ifr_addr.sa_len
 * However, under earlier systems, sa_len isn't present, so the size is 
 * just sizeof(struct ifreq)
 */
#ifdef HAVE_SA_LEN
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#define ifreq_size(i) max(sizeof(struct ifreq),\
     sizeof((i).ifr_name)+(i).ifr_addr.sa_len)
#else
#define ifreq_size(i) sizeof(struct ifreq)
#endif /* HAVE_SA_LEN*/

#ifdef AF_INET6
	sd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_IP);
	if (sd < 0)
#endif
	{
		sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
		if (sd < 0)
		{
			return -1;
		}
	}
	memset(buf, 0, sizeof(buf));
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl(sd, SIOCGIFCONF, (char *)&ifc) < 0)
	{
		close(sd);
		return -1;
	}
	n = ifc.ifc_len;
	for (i = 0; i < n; i+= ifreq_size(*ifr) ) {
		ifrp = (struct ifreq *)((char *) ifc.ifc_buf+i);
		strncpy(ifr.ifr_name, ifrp->ifr_name, IFNAMSIZ);
#ifdef SIOCGIFHWADDR
		if (ioctl(sd, SIOCGIFHWADDR, &ifr) < 0)
			continue;
		a = (unsigned char *) &ifr.ifr_hwaddr.sa_data;
#else
#ifdef SIOCGENADDR
		if (ioctl(sd, SIOCGENADDR, &ifr) < 0)
			continue;
		a = (unsigned char *) ifr.ifr_enaddr;
#else
		/*
		 * XXX we don't have a way of getting the hardware
		 * address
		 */
		close(sd);
		return 0;
#endif /* SIOCGENADDR */
#endif /* SIOCGIFHWADDR */
		if (!a[0] && !a[1] && !a[2] && !a[3] && !a[4] && !a[5])
			continue;
		if (node_id)
      {
			memcpy(node_id, a, 6);
			close(sd);
			return 1;
		}
	}
	close(sd);
#endif
	return 0;
}

/* Assume that the gettimeofday() has microsecond granularity */
#define MAX_ADJUSTMENT 10

static int get_clock(DWORD *clock_high, DWORD *clock_low, WORD *ret_clock_seq)
{
	static int adjustment = 0;
	static struct timeval last = {0, 0};
	static unsigned short clock_seq;
	struct timeval tv;
	QWORD clock_reg;
#ifdef _WIN32
   FILETIME ft;
   ULARGE_INTEGER li;
   unsigned __int64 t;
#endif
	
try_again:
#ifdef _WIN32
   GetSystemTimeAsFileTime(&ft);
   li.LowPart  = ft.dwLowDateTime;
   li.HighPart = ft.dwHighDateTime;
   t = li.QuadPart;       // In 100-nanosecond intervals
   t /= 10;    // To microseconds
   tv.tv_sec = (long)(t / 1000000);
   tv.tv_usec = (long)(t % 1000000);
#else
	gettimeofday(&tv, NULL);
#endif
	if ((last.tv_sec == 0) && (last.tv_usec == 0))
   {
		get_random_bytes(&clock_seq, sizeof(clock_seq));
		clock_seq &= 0x1FFF;
		last = tv;
		last.tv_sec--;
	}
	if ((tv.tv_sec < last.tv_sec) ||
	    ((tv.tv_sec == last.tv_sec) &&
	     (tv.tv_usec < last.tv_usec)))
   {
		clock_seq = (clock_seq + 1) & 0x1FFF;
		adjustment = 0;
		last = tv;
	}
   else if ((tv.tv_sec == last.tv_sec) &&
	    (tv.tv_usec == last.tv_usec))
   {
		if (adjustment >= MAX_ADJUSTMENT)
			goto try_again;
		adjustment++;
	}
   else
   {
		adjustment = 0;
		last = tv;
	}
		
	clock_reg = tv.tv_usec * 10 + adjustment;
	clock_reg += ((QWORD)tv.tv_sec) * 10000000;
	clock_reg += (((QWORD)0x01B21DD2) << 32) + 0x13814000;

	*clock_high = (DWORD)(clock_reg >> 32);
	*clock_low = (DWORD)clock_reg;
	*ret_clock_seq = clock_seq;
	return 0;
}

static void uuid_generate_time(uuid_t out)
{
	static unsigned char node_id[6];
	static int has_init = 0;
	struct uuid uu;
	DWORD clock_mid;

	if (!has_init)
   {
		if (get_node_id(node_id) <= 0)
      {
			get_random_bytes(node_id, 6);
			/*
			 * Set multicast bit, to prevent conflicts
			 * with IEEE 802 addresses obtained from
			 * network cards
			 */
			node_id[0] |= 0x80;
		}
		has_init = 1;
	}
	get_clock(&clock_mid, &uu.time_low, &uu.clock_seq);
	uu.clock_seq |= 0x8000;
	uu.time_mid = (WORD)clock_mid;
	uu.time_hi_and_version = (WORD)((clock_mid >> 16) | 0x1000);
	memcpy(uu.node, node_id, 6);
	uuid_pack(&uu, out);
}

static void uuid_generate_random(uuid_t out)
{
	uuid_t	buf;
	struct uuid uu;

	get_random_bytes(buf, sizeof(buf));
	uuid_unpack(buf, &uu);

	uu.clock_seq = (uu.clock_seq & 0x3FFF) | 0x8000;
	uu.time_hi_and_version = (uu.time_hi_and_version & 0x0FFF) | 0x4000;
	uuid_pack(&uu, out);
}

/*
 * This is the generic front-end to uuid_generate_random and
 * uuid_generate_time.  It uses uuid_generate_random only if
 * /dev/urandom is available, since otherwise we won't have
 * high-quality randomness.
 */
void LIBNETXMS_EXPORTABLE uuid_generate(uuid_t out)
{
#ifndef _WIN32
	int fd;

	fd = get_random_fd();
	if (fd >= 0)
   {
		close(fd);
		uuid_generate_random(out);
	}
	else
#endif
		uuid_generate_time(out);
}

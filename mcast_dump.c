/* mcast_dump - Dump multicast packets to stdout */
/* Copyright (C) 2007 Stephen Kershaw */
/* steve@stevek.co.uk */

/* This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.

*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.

*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <netinet/in.h>		/* For struct ip_mreq */
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>		/* For gettimeofday */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>		/* For alarm, ^C handlers etc */

#define MSGBUFSIZE 10000	/* Big enough to cope with jumbo packets */
#define IPADDRSIZE 18
#define MAXPATHLEN 512

#ifdef HAVE_BUILTIN_EXPECT
#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)
#else
#define likely(x) x
#define unlikely(x) x
#endif

int main(int argc, char *argv[])
{
	/* Forward declarations */
	void exit_handler();

	/* Variable definitions */
	struct sockaddr_in addr;
	struct timeval ipg_timer;
	int sock_desc, nbytes, addrlen, term_seconds=-1;
	unsigned int yes = 1, first=1, mcast_udp_port=5000, flag_ip_set=0, flag_port_set=0, flag_output_file=0;
	struct ip_mreq mreq;
	char msgbuf[MSGBUFSIZE], output_filename[MAXPATHLEN], arg;
	unsigned long long int current_pkt_usecs, prev_pkt_usecs, ipg_usecs;
	unsigned char mcast_ip_address[IPADDRSIZE];
	FILE *output_fd;

	/* For command line args */
	extern char *optarg;

	/* Help message to print if program run with incorrect args */
	char *help ={
	"Usage: mcast_dump -option<parameter> [...]\n\
Options:\n\
	 -a = <multicast IP address, a.b.c.d>\n\
	 -o = [optional, default stdout] <output file>\n\
	 -p = <port no - default 5000>\n\
	 -t = <terminate after time specified in seconds - default 0=never>"};

	/* Check have enough commandline args */
	if (argc < 4) {
		fprintf (stderr, "%s \n", help);
		exit(EXIT_FAILURE);
	}

	/* Parse command-line args */
	while ((arg = getopt(argc, argv, "a:o:p:t:")) != EOF) {
	switch(arg) {

			case 'a':
				if (optarg != NULL) {
					memset(mcast_ip_address, 0, IPADDRSIZE); //memset deprecated?
					strncpy(mcast_ip_address, optarg, IPADDRSIZE-1);
					flag_ip_set=1;
				} else {
					perror("Must specify valid multicast address");
					fprintf (stderr, "%s \n", help);
					exit(EXIT_FAILURE);
				}
				break;

			case 'o':
				if (optarg != NULL) {
					strncpy(output_filename, optarg, strlen(optarg));
					flag_output_file=1;
				} else {
					perror("Must specify output file path");
					fprintf (stderr, "%s \n", help);
					exit(EXIT_FAILURE);
				}
				break;


			case 'p':
				if (optarg != NULL) {
					mcast_udp_port =  atoi(optarg);
					flag_port_set=1;
				} else {
					perror("Must specify valid port");
					fprintf (stderr, "%s \n", help);
					exit(EXIT_FAILURE);
				}
				break;

			case 't':
				if (optarg != NULL) {
					term_seconds = atoi(optarg);
					if (term_seconds < 0) {
						fprintf (stderr, "%s \n", help);
						exit(EXIT_FAILURE);
					}
				} else {
					term_seconds = 0;
				}
				break;

			default:
				exit(EXIT_FAILURE);
				break;
		} /* End switch */

	} /* End parse-commandline while */

	/* Check that have at least ip, port, time */
	if (!flag_ip_set || !flag_port_set || term_seconds==-1) {
		fprintf(stderr, "You need more args!\n\n %s \n", help);
		exit(EXIT_FAILURE);
	}

	/* Assign signal handler to SIGALRM, to allow us terminate after a set time */
	signal (SIGALRM, exit_handler);
	signal (SIGINT, exit_handler);	/* Handler for ctrl-c */
	//signal (SIGTSTP, cntlz_handler);

	/* Schedule an alarm, term_seconds in the future */
	alarm(term_seconds);

	/* Create socket - UDP for IP multicast */
	if ((sock_desc = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("Socket could not be created");
		exit(EXIT_FAILURE);
	}
	/* Allow multiple sockets to use the same PORT number - permitted with multicast */
	if (setsockopt(sock_desc, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		perror("Reusing ADDR failed");
		exit(EXIT_FAILURE);
	}

	/* set up destination address */
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY); /* N.B.: differs from sender */
	addr.sin_port = htons(mcast_udp_port);

	/* bind to receive address */
	if (bind(sock_desc, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("Bind to receive address failed");
		exit(EXIT_FAILURE);
	}

	/* To become a member of a group the following struct */
	/*  must be populated and passed to kernel using setsockopt */
	mreq.imr_multiaddr.s_addr = inet_addr(mcast_ip_address);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);

	/* use setsockopt() to request that the kernel join a multicast group */
	/* TODO (perhaps): Check return code and loop 'n' times or until success */
	if (setsockopt(sock_desc, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
		perror("Could not join mcast group using setsockopt");
		exit(EXIT_FAILURE);
	}

	/* Open output file if required - note using fopen, not fopen64 */
	output_fd=stdout;
	if (flag_output_file) {
		output_fd = fopen(output_filename, "w");
		if(output_fd==NULL) {
			perror("fopen error");
			exit(EXIT_FAILURE);
		}
	}

	/* After successful join, now just enter a read-dump loop */
	addrlen=sizeof(addr);
	while (1) {
		if (unlikely((nbytes=recvfrom(sock_desc, msgbuf, MSGBUFSIZE, 0,
		 (struct sockaddr *) &addr,&addrlen)) < 0)) {
			perror("Error on socket read.");
			exit(EXIT_FAILURE);
		}

		fwrite(msgbuf, nbytes, 1, output_fd );
	} /* End main while(1) loop, terminating only with ^c or alarm */

}	/* End main */

void exit_handler()
{
	/* Drop membership perhaps - but done automatically when exit */
	fflush(stdout);
	exit(EXIT_SUCCESS);
} /* End exit_handler */

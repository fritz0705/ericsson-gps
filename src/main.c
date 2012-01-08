/* (c) 2012 Fritz Grimpen
	 Insert MIT license HERE
	 */
/* Usage:
	$ eval `./egps`
	*/
#define _XOPEN_SOURCE 1000

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include <termios.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Some error handling */
#include <error.h>
#include <errno.h>

static void wait_for(int fd, char *str)
{
	int len = strlen(str);
	char *buf = malloc(len + 1);
	buf[len] = '\0';
	while (strcmp(buf, str) != 0)
	{
		memmove(buf, buf + 1, len - 1);
		read(fd, &buf[len - 1], 1);
	}
	free(buf);
}

int main(int argc, char **argv)
{
	char *init_line = "AT*E2GPSCTL=1,1,1";
	if (argc >= 2)
		init_line = argv[1];

	int acm2_fd = open("/dev/ttyACM1", O_RDWR | O_NOCTTY, 0);
	if (acm2_fd == -1)
		error_at_line(1, errno, __FILE__, __LINE__, "open(/dev/ttyACM2 failed");

	/* Set ACM2 fd to 9600 baud */
	struct termios acm2_term;
	tcgetattr(acm2_fd, &acm2_term);
	cfsetospeed(&acm2_term, 9600);
	cfsetispeed(&acm2_term, 9600);
	tcsetattr(acm2_fd, TCSANOW, &acm2_term);

	int ptmx_fd = posix_openpt(O_RDWR | O_NOCTTY);
	if (ptmx_fd == -1)
		error_at_line(1, errno, __FILE__, __LINE__, "open(/dev/ptmx) failed");

	char *pts = ptsname(ptmx_fd);
	grantpt(ptmx_fd);
	unlockpt(ptmx_fd);

	printf("gpsd -N %s\n", pts);

	/* We should wait for "*EMRDY: 1" here */
	wait_for(acm2_fd, "\r\n*EMRDY: 1\r\n");

	write(acm2_fd, init_line, strlen(init_line));
	write(acm2_fd, "\r\n", 2);
	wait_for(acm2_fd, "OK\r\n");
	write(acm2_fd, "AT*E2GPSNPD\r\n", strlen("AT*E2GPSNPD\r\n"));
	wait_for(acm2_fd, "OK\r\n");

	int epoll_fd = epoll_create1(0);
	if (epoll_fd == -1)
		error_at_line(1, errno, __FILE__, __LINE__, "epoll_create() failed");

	struct epoll_event acm2_event, ptmx_event;
	struct epoll_event events[16];

	acm2_event.events = EPOLLIN;
	acm2_event.data.fd = acm2_fd;

	ptmx_event.events = EPOLLIN;
	ptmx_event.data.fd = ptmx_fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, acm2_fd, &acm2_event) == -1)
		error_at_line(1, errno, __FILE__, __LINE__, "epoll_ctl() failed");

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ptmx_fd, &ptmx_event) == -1)
		error_at_line(1, errno, __FILE__, __LINE__, "epoll_ctl() failed");

	for (;;)
	{
		int nfds = epoll_wait(epoll_fd, events, 16, -1);

		char c;
		for (int n = 0; n < nfds; ++n)
		{
			if (events[n].data.fd == acm2_fd)
			{
				if (read(acm2_fd, &c, 1) <= 0)
					error_at_line(1, errno, __FILE__, __LINE__, "read() failed; something is wrong");
#				ifdef DEBUG
				write(2, &c, 1);
#				endif
				write(ptmx_fd, &c, 1);
			}
			else if (events[n].data.fd == ptmx_fd)
			{
				read(ptmx_fd, &c, 1);
#				ifdef DEBUG
				write(2, "\e[7m", 4);
				write(2, &c, 1);
				write(2, "\e[27m", 5);
#				endif
				if (write(acm2_fd, &c, 1) <= 0)
					error_at_line(1, errno, __FILE__, __LINE__, "write() failed; something is wrong");
			}
		}
	}
}

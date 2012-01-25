/* (c) 2012 Fritz Grimpen
	 Insert MIT license HERE
	 */
/* Usage:
	$ eval `./egps`
	*/
#define _XOPEN_SOURCE 1000
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include <termios.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <alloca.h>

/* Some error handling */
#include <error.h>
#include <errno.h>

#define ERROR(...) error_at_line(1, errno, __FILE__, __LINE__, __VA_ARGS__)

static struct {
	char *device;
	char *init_line;
	int daemon;
	char *pidfile;
} program_state = {
	.device = "/dev/ttyACM2",
	.init_line = "AT*E2GPSCTL=1,1,1",
	.daemon = 0,
	.pidfile = "egps.pid"
};

static void _expect(int fd, char *str)
{
	int len = strlen(str);
	char *tmp = alloca(len + 1);
	tmp[len] = 0;
	while (strcmp(tmp, str))
	{
		memmove(tmp, tmp + 1, len - 1);
		read(fd, &tmp[len - 1], 1);
	}
}

static void print_help(char *name)
{
	fprintf(stderr, "Usage: %s [-device DEV] [-help] INIT_LINE\n", name);
}

static void _parse_argv(int argc, char **argv)
{
	int state = 0;
	for (int i = 1; i < argc; ++i)
	{
		switch (state)
		{
			case 0:
				if (!strcmp(argv[i], "-device"))
					state = 1;
				else if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "-h"))
				{
					print_help(argv[0]);
					_exit(0);
				}
				else if (!strcmp(argv[i], "-daemon"))
					program_state.daemon = 1;
				else if (!strcmp(argv[i], "-pidfile"))
					state = 3;
				else
					program_state.init_line = argv[i];
				break;
			case 1:
				program_state.device = argv[i];
				state = 0;
			case 2:
			case 3:
				program_state.pidfile = argv[i];
		}
	}
}

int main(int argc, char **argv)
{
	_parse_argv(argc, argv);

	int acm2_fd = open(program_state.device, O_RDWR | O_NOCTTY, 0);
	if (acm2_fd == -1)
		ERROR("open(\"%s\") failed", program_state.device);

	struct termios acm2_term;
	tcgetattr(acm2_fd, &acm2_term);
	cfsetspeed(&acm2_term, 9600);
	tcsetattr(acm2_fd, TCSANOW, &acm2_term);

	int ptmx_fd = posix_openpt(O_RDWR | O_NOCTTY);
	if (ptmx_fd == -1)
		ERROR("open(\"/dev/ptmx\") failed");

	char *pts = ptsname(ptmx_fd);
	grantpt(ptmx_fd);
	unlockpt(ptmx_fd);

	printf("Allocated pts %s\n", pts);

	/* We should wait for "*EMRDY: 1" here */
	_expect(acm2_fd, "\r\n*EMRDY: 1\r\n");

	write(acm2_fd, program_state.init_line, strlen(program_state.init_line));
	write(acm2_fd, "\r\n", 2);
	_expect(acm2_fd, "OK\r\n");
	write(acm2_fd, "AT*E2GPSNPD\r\n", strlen("AT*E2GPSNPD\r\n"));
	_expect(acm2_fd, "OK\r\n");

	int epoll_fd = epoll_create1(0);
	if (epoll_fd == -1)
		ERROR("epoll_create() failed");

	struct epoll_event acm2_event, ptmx_event;
	struct epoll_event events[16];

	acm2_event.events = EPOLLIN;
	acm2_event.data.fd = acm2_fd;

	ptmx_event.events = EPOLLIN;
	ptmx_event.data.fd = ptmx_fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, acm2_fd, &acm2_event) == -1)
		ERROR("epoll_ctl() failed");

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ptmx_fd, &ptmx_event) == -1)
		ERROR("epoll_ctl() failed");

	if (program_state.daemon)
	{
		pid_t forked = fork();

		if (forked == -1)
			ERROR("fork() failed");

		if (forked != 0)
		{
			/* Daemonize */
			int pidfd = creat(program_state.pidfile, O_WRONLY);
			if (pidfd == -1)
				ERROR("Could not open pidfile; pid is %d", forked);

			FILE *pidfile = fdopen(pidfd, "w");

			fprintf(pidfile, "%d\n", forked);
			fclose(pidfile);
			_exit(0);
		}
		
		setsid();
	}

	for (;;)
	{
		int nfds = epoll_wait(epoll_fd, events, 16, -1);

		char c;
		for (int n = 0; n < nfds; ++n)
		{
			if (events[n].data.fd == acm2_fd)
			{
				if (read(acm2_fd, &c, 1) <= 0)
					ERROR("read() failed; something is wrong");
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
					ERROR("write() failed; something is wrong");
			}
		}
	}
}

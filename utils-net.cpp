#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

#include "utils-str.h"

ssize_t WRITE(int fd, const void *whereto, size_t len)
{
        ssize_t cnt=0;

        while(len>0)
        {
		ssize_t rc = write(fd, whereto, len);

		if (rc == -1)
		{
			if (errno != EINTR && errno != EINPROGRESS && errno != EAGAIN)
				return -1;

			continue;
		}
		else if (rc == 0)
			return 0;
		else
		{
			whereto += rc;
			len -= rc;
			cnt += rc;
		}
        }

        return cnt;
}

ssize_t READ(int fd, void *whereto, size_t len)
{
        ssize_t cnt=0;

        while(len>0)
        {
		ssize_t rc = read(fd, whereto, len);

		if (rc == -1)
		{
			if (errno != EINTR && errno != EAGAIN)
				return -1;

			continue;
		}
		else if (rc == 0)
			break;
		else
		{
			whereto += rc;
			len -= rc;
			cnt += rc;
		}
        }

        return cnt;
}

int connect_to(std::string hostname, int port)
{
	std::string portstr = format("%d", port);
	int fd = -1;

        struct addrinfo hints;
        struct addrinfo* result = NULL;
        memset(&hints, 0x00, sizeof(struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = 0;
        hints.ai_protocol = 0;
        int rc = getaddrinfo(hostname.c_str(), portstr.c_str(), &hints, &result);
        if (rc)
        {
                fprintf(stderr, "Cannot resolve host name %s: %s\n", hostname.c_str(), gai_strerror(rc));
		freeaddrinfo(result);
		return -1;
        }

	for (struct addrinfo *rp = result; rp != NULL; rp = rp -> ai_next)
	{
		fd = socket(rp -> ai_family, rp -> ai_socktype, rp -> ai_protocol);
		if (fd == -1)
			continue;

		int flag = 1;
		if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) == -1)
		{
			fprintf(stderr, "could not set TCP_NODELAY on socket (%s)\n", strerror(errno));
			return -1;
		}

		/* wait for connection */
		/* connect to peer */
		if (connect(fd, rp -> ai_addr, rp -> ai_addrlen) == 0)
		{
			/* connection made, return */
			freeaddrinfo(result);
			return fd;
		}

		fd = -1;
	}

	freeaddrinfo(result);

	return fd;
}

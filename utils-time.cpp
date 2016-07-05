#include <errno.h>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

double get_ts()
{
        struct timeval ts;

        if (gettimeofday(&ts, NULL) == -1)
	{
                std::cerr << "gettimeofday failed\n";
		return -1;
	}

        return double(ts.tv_sec) + double(ts.tv_usec) / 1000000.0;
}

void USLEEP(useconds_t how_long)
{
	for(;;)
	{
		int rc = usleep(how_long);

		if (rc == -1)
		{
			if (errno == EINTR)
				continue;

			std::cerr << "usleep (" << how_long << ") failed: " << strerror(errno) << std::endl;
		}

		break;
	}
}

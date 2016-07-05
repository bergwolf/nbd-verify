#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

std::string format(const char *fmt, ...)
{
	char *buffer = NULL;
        va_list ap;
	int ignored __attribute__((unused));

        va_start(ap, fmt);
        ignored = vasprintf(&buffer, fmt, ap);
        va_end(ap);

	std::string result = buffer;
	free(buffer);

	return result;
}

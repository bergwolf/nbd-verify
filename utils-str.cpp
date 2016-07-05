#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

std::string format(const char *fmt, ...)
{
	char *buffer = NULL;
        va_list ap;

        va_start(ap, fmt);
        (void)vasprintf(&buffer, fmt, ap);
        va_end(ap);

	std::string result = buffer;
	free(buffer);

	return result;
}

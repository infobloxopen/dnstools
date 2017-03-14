#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#define TIMESTAMP_MAXLENGTH 64
#define TIMESTAMP_FORMAT "%x %X"

void print_timestamp(FILE *stream)
{
	time_t timestamp = time(NULL);
	struct tm *local_timestamp = localtime(&timestamp);

	char timestamp_str[TIMESTAMP_MAXLENGTH];
	strftime(timestamp_str, sizeof(timestamp_str), TIMESTAMP_FORMAT, local_timestamp);

	fprintf(stream, "[%s] ", timestamp_str);
}

void log_message(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	print_timestamp(stdout);
	vprintf(format, ap);
	printf("\n");
	fflush(stdout);
}

void log_error(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	print_timestamp(stderr);
	vfprintf(stderr, format, ap);
	fprintf(stderr, "\n");
	fflush(stderr);
}

void log_errno(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	int errnum = errno;

	print_timestamp(stderr);
	vfprintf(stderr, format, ap);
	fprintf(stderr, " (%d: %s)\n", errnum, strerror(errnum));
	fflush(stderr);
}

void log_errno_ex(int errnum, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	print_timestamp(stderr);
	vfprintf(stderr, format, ap);
	fprintf(stderr, " (%d: %s)\n", errnum, strerror(errnum));
	fflush(stderr);
}

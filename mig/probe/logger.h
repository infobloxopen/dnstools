#ifndef __LOGGER_H__
#define __LOGGER_H__

void log_message(const char *format, ...);
void log_error(const char *format, ...);
void log_errno(const char *format, ...);
void log_errno_ex(int errnum, const char *format, ...);

#endif // __LOGGER_H__

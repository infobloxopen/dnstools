#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "logger.h"

#ifdef CLOCK_MONOTONIC_RAW
	#define CLOCK_SOURCE CLOCK_MONOTONIC_RAW
#else
	#define CLOCK_SOURCE CLOCK_MONOTONIC
#endif

#define CLIENT_ID_LENGTH 16
#define RECEIVE_BUFFER_SIZE 65535
#define DOMAINS_FILE_LIMIT (50*1024*1024)

#define NANOSECONDS 1000000000

#define RECV_TIMEOUT 35

char additional[] = {'\x00', '\x00', '\x29', '\x10', '\x00', '\x00', '\x00', '\x80',
                     '\x00', '\x00', '\x14', '\xff', '\xee', '\x00', '\x10'};

struct pair_timespec
{
	struct timespec sent;
	unsigned int answer;
	struct timespec received;
};

struct dns_query
{
	unsigned short transaction_id;
	unsigned short flags;
	unsigned short questions;
	unsigned short answers;
	unsigned short authorities;
	unsigned short additional;
};

void *make_queries(char *names, size_t count, char *client)
{
	size_t i;
	size_t total = 0;
	char *name = names;

	for (i = 0; i < count; i++)
	{
		size_t name_size = strlen(name) + 1;
		size_t size = sizeof(size_t) + sizeof(struct dns_query) + name_size + 2*sizeof(unsigned short);
		if (client) size += sizeof(additional) + CLIENT_ID_LENGTH;

		total += size;
		name += name_size;
		if (*name == '\0') name = names;
	}

	void *buffer = malloc(total);
	if (buffer == NULL) return NULL;

	name = names;
	char *offset = (char *) buffer;
	for (i = 0; i < count; i++)
	{
		size_t name_size = strlen(name) + 1;
		size_t size = sizeof(struct dns_query) + name_size + 2*sizeof(unsigned short);
		if (client) size += sizeof(additional) + CLIENT_ID_LENGTH;

		*((size_t *) offset) = size;
		offset += sizeof(size_t);

		struct dns_query *q = (struct dns_query *) offset;
		q->transaction_id = htons((unsigned short) (i % USHRT_MAX));
		q->flags = htons(0x0100);
		q->questions = htons(1);
		q->answers = htons(0);
		q->authorities = htons(0);
		q->additional = htons(client? 1 : 0);
		offset += sizeof(struct dns_query);

		char *q_name = (char *) offset;
		strcpy(q_name, name);
		offset += name_size;

		name += name_size;
		if (*name == '\0') name = names;

		unsigned short *q_type = (unsigned short *) offset;
		*q_type = htons(0x0001);
		offset += sizeof(unsigned short);

		unsigned short *q_class = (unsigned short *) offset;
		*q_class = htons(0x0001);
		offset += sizeof(unsigned short);

		if (client)
		{
			memcpy(offset, additional, sizeof(additional));
			offset += sizeof(additional);

			memcpy(offset, client, CLIENT_ID_LENGTH);
			offset += CLIENT_ID_LENGTH;
		}
	}

	return buffer;
}

void *get_next_query(char **offset, size_t *size)
{
	*size = *(size_t *) *offset;
	*offset += sizeof(size_t);

	void *query = *offset;
	*offset += *size;

	return query;
}

int sent_query(int fd, struct sockaddr_in *server, void *query, size_t size,
               size_t *index, struct timespec *sends, struct pair_timespec *pairs, int verbose)
{
	ssize_t bytes_sent = sendto(fd, query, size, 0, (struct sockaddr *) server, sizeof(struct sockaddr_in));
	if (bytes_sent == -1)
	{
		int errnum = errno;

		char address[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, server, address, sizeof(address));

		log_errno_ex(errnum, "[%s] Error on sending to %s:%hu.", address, htons(server->sin_port));
		return -1;
	}

	if (bytes_sent != size)
	{
		log_error("Expected to send %lu bytes but actually sent %ld.", size, bytes_sent);
		return -1;
	}

	int r = clock_gettime(CLOCK_SOURCE, &sends[*index]);
	if (r == -1)
	{
		log_errno("Error on getting timestamp.");
		return -1;
	}

	pairs[*index].sent = sends[*index];

	if (verbose) log_message("Sent %ld bytes.", bytes_sent);

	(*index)++;

	return 0;
}

int recv_answer(int fd, struct sockaddr_in *server, void *buffer, size_t size,
                size_t *index, size_t count, struct timespec *receives, struct pair_timespec *pairs, int verbose)
{
	while (1)
	{
		ssize_t bytes_received = recv(fd, buffer, size, 0);
		if (bytes_received == -1)
		{
			int errnum = errno;
			if (errnum == EAGAIN) break;

			char address[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, server, address, sizeof(address));

			log_errno_ex(errnum, "Error on receiving from %s:%hu.", address, htons(server->sin_port));
			return -1;
		}

		if (bytes_received < sizeof(struct dns_query))
		{
			log_error("Expected at least %lu bytes but got only %ld.",
			          sizeof(struct dns_query), bytes_received);
			return -1;
		}

		struct timespec received;
		int r = clock_gettime(CLOCK_SOURCE, &received);
		if (r == -1)
		{
			log_errno("Error on getting timestamp.");
			return -1;
		}

		if (verbose) log_message("Got %ld bytes.", bytes_received);

		struct dns_query *query = (struct dns_query *) buffer;
		query->transaction_id = htons(query->transaction_id);
		query->flags = htons(query->flags);
		query->questions = htons(query->questions);
		query->answers = htons(query->answers);
		query->authorities = htons(query->authorities);
		query->additional = htons(query->additional);

		if (count < USHRT_MAX && query->transaction_id >= count)
		{
			log_error("Recevied message with transaction id %hu while expected maximum is %lu.",
			          query->transaction_id, count);
			return -1;
		}

		size_t pair_index = query->transaction_id;
		struct pair_timespec *pair = NULL;

		unsigned long long received_nsec = received.tv_sec;
		received_nsec *= NANOSECONDS;
		received_nsec += received.tv_nsec;

		while (pair_index < count)
		{
			pair = &pairs[pair_index];
			unsigned long long sent_nsec = pair->sent.tv_sec;
			sent_nsec *= NANOSECONDS;
			sent_nsec += pair->sent.tv_nsec;
			if (sent_nsec < received_nsec && pair->answer == 0) break;

			pair_index += USHRT_MAX;
		}

		if (pair_index < count)
		{
			pair->answer++;
			receives[*index] = received;
			pair->received = receives[*index];

			if (verbose) log_message("Answer:\n"
			                         "\tID.........: %hu\n"
			                         "\tFlags......: 0x%hx\n"
			                         "\tQueries....: %hu\n"
			                         "\tAnswers....: %hu\n"
			                         "\tAuthorities: %hu\n"
			                         "\tAdditional.: %hu\n\n",
			                         query->transaction_id,
			                         query->flags,
			                         query->questions,
			                         query->answers,
			                         query->authorities,
			                         query->additional);

			(*index)++;
			if (verbose) log_message("Remains messages: %lu.", count - *index);
		}
		else if (verbose) log_error("Received duplicate answer for query with transaction id %hu.",
		                            query->transaction_id);
	}

	return 0;
}

void usage(void)
{
	printf("mig - DNS performance measurement tool\n\n"
	       "Usage: mig <options>\n\n"
	       "Options:\n"
	       "\t-s, --server  - name server IPv4 address (required);\n"
	       "\t-p, --port    - name server port (default 53);\n"
	       "\t-c, --client  - client id (16 bytes hex string);\n"
	       "\t-n, --queries - number of queries (default length of domain set);\n"
	       "\t-l, --limit   - limit query rate to the number (default - no limit);\n"
	       "\t-d, --domains - file with list of domains to query (ASCII lowercase separated by new line);\n"
	       "\t-v, --verbose - print more details;\n"
	       "\t-o, --output  - write statistics to specified file (default stdout);\n"
               "\t-h, --help    - this message.\n");
}

struct mdig_options
{
	struct sockaddr_in server;

	int got_client;
	char client[CLIENT_ID_LENGTH];

	int got_query_number;
	size_t query_number;
	size_t query_limit;

	size_t domain_count;
	char *domains;

	FILE *output;

	int verbose;
};

static struct option long_options[] = {
	{"help",    no_argument,       NULL, 'h'},
	{"server",  required_argument, NULL, 's'},
	{"port",    required_argument, NULL, 'p'},
	{"client",  required_argument, NULL, 'c'},
	{"queries", required_argument, NULL, 'n'},
	{"limit",   required_argument, NULL, 'l'},
	{"domains", required_argument, NULL, 'd'},
	{"verbose", no_argument,       NULL, 'v'},
	{"output",  required_argument, NULL, 'o'},
	{NULL,      0,                 NULL, 0}
};

enum get_options_result
{
	GOR_OK,
	GOR_HELP,
	GOR_ERROR
};

extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;

int get_port_value(char *string, unsigned short *port)
{
	char *endptr = NULL;

	errno = 0;
	unsigned long value = strtoul(string, &endptr, 10);

	if (*endptr != '\0' || errno != 0 || value > USHRT_MAX) return -1;

	*port = (unsigned short) value;

	return 0;
}

int get_client_value(char *string, char *client)
{
	if (strlen(string) != 2*CLIENT_ID_LENGTH) return -1;

	char byte[] = "00";
	char *endptr = NULL;
	int i;
	for (i = 0; i < CLIENT_ID_LENGTH; i++)
	{
		byte[0] = string[2*i];
		byte[1] = string[2*i + 1];

		errno = 0;
		unsigned long value = strtoul(byte, &endptr, 16);
		if (*endptr != '\0' || errno != 0 || value > UCHAR_MAX)	return -1;

		client[i] = (char) value;
	}

	return 0;
}

int get_query_number_value(char *string, size_t *number)
{
	char *endptr = NULL;

	errno = 0;
	unsigned long long value = strtoull(string, &endptr, 10);

	if (*endptr != '\0' || errno != 0) return -1;

	*number = (size_t) value;
	return 0;
}

int get_domains(char *string, size_t *count, char **domains)
{
	int fd = open(string, O_RDONLY);
	if (fd == -1)
	{
		log_errno("Can't open file.");
		return -1;
	}

	struct stat stat;
	int r = fstat(fd, &stat);
	if (r == -1)
	{
		log_errno("Can't get file size.");

		close(fd);
		return -1;
	}

	if (stat.st_size > DOMAINS_FILE_LIMIT)
	{
		log_error("File too big (%lld > %lld)\n", stat.st_size, (long long) DOMAINS_FILE_LIMIT);

		close(fd);
		return -1;
	}

	char *buffer = malloc(stat.st_size);
	if (buffer == NULL)
	{
		log_errno("Can't allocate %lld bytes for read buffer.", stat.st_size);

		close(fd);
		return -1;
	}

	ssize_t bytes_read = read(fd, buffer, stat.st_size);
	if (bytes_read < 0)
	{
		log_errno("Error on reading file.");

		free(buffer);
		close(fd);
		return -1;
	}

	close(fd);

	if (bytes_read == 0)
	{
		log_error("File is empty\n");

		free(buffer);
		return -1;
	}

	*count = 0;
	*domains = malloc(3*(bytes_read/2 + bytes_read % 2) + 1);
	if (*domains == NULL)
	{
		log_errno("Can't allocate %ld butes to store domains.", 3*(bytes_read/2 + bytes_read % 2) + 1);

		free(buffer);
		return -1;
	}

	char *src_offset = buffer;
	char *dst_offset = *domains;

	int zone_count = 0;
	size_t length = 0;
	size_t line = 1;

	size_t i;
	for (i = 0; i < bytes_read; i++)
	{
		char *current = buffer + i;

		switch (*current)
		{
			case '.':
				length++;

				if (zone_count == 0)
				{
					log_error("Invalid domain name at line %lu.", line);

					free(buffer);
					free(*domains);
					return -1;
				}

				*dst_offset = (char) zone_count;
				dst_offset++;

				memcpy(dst_offset, src_offset, zone_count);
				dst_offset += zone_count;

				zone_count = 0;
				src_offset = current + 1;
				break;

			case '\n':
				if (length == 0)
				{
					line++;
					break;
				}

				if (zone_count == 0)
				{
					log_error("Invalid domain name at line %lu.", line);

					free(buffer);
					free(*domains);
					return -1;
				}

				*dst_offset = (char) zone_count;
				dst_offset++;

				memcpy(dst_offset, src_offset, zone_count);
				dst_offset += zone_count;

				*dst_offset = '\0';
				dst_offset++;

				(*count)++;

				zone_count = 0;
				src_offset = current + 1;

				length = 0;
				line++;
				break;

			default:
				zone_count++;
				if (zone_count > UCHAR_MAX)
				{
					log_error("Invalid domain name at line %lu.", line);

					free(buffer);
					free(*domains);
					return -1;
				}
				length++;
		}
	}

	if (length > 0)
	{
		if (zone_count == 0)
		{
			log_error("Invalid domain name at line %lu.", line);

			free(buffer);
			free(*domains);
			return -1;
		}

		*dst_offset = (char) zone_count;
		dst_offset++;

		memcpy(dst_offset, src_offset, zone_count);
		dst_offset += zone_count;

		*dst_offset = '\0';
		 dst_offset++;

		(*count)++;
	}

	*dst_offset = '\0';

	free(buffer);
	return 0;
}

void print_domains(size_t count, char *domains)
{
	printf("Domains (%lu):", count);

	size_t index = 0;
	char *offset = domains;
	char zone[256];
	while (*offset != '\0')
	{
		printf("\n\t%lu:", ++index);
		while (*offset != '\0')
		{
			size_t zone_count = (size_t) *(unsigned char *) offset;
			offset++;

			printf(" \\0%lo", zone_count);

			memcpy(zone, offset, zone_count);
			zone[zone_count] = '\0';
			printf(" \"%s\"", zone);

			offset += zone_count;
		}

		offset++;
	}

	printf("\n\n");
}

int open_output(char *string, FILE **output)
{
	*output = fopen(string, "w");
	if (*output == NULL)
	{
		log_errno("");
		return -1;
	}

	return 0;
}

enum get_options_result get_options(int argc, char *argv[], struct mdig_options *mdig_options)
{
	opterr = 0;
	int option_char;

	mdig_options->server.sin_family = AF_INET;
	mdig_options->server.sin_port = htons(53);

	int got_address = 0;
	mdig_options->got_client = 0;
	mdig_options->got_query_number = 0;
	mdig_options->query_limit = 0;
	mdig_options->domain_count = 0;
	mdig_options->domains = NULL;
	mdig_options->output = stdout;
	mdig_options->verbose = 0;
	while ((option_char = getopt_long(argc, argv, "hs:p:c:n:l:d:vo:", long_options, NULL)) != -1)
	{
		switch (option_char)
		{
			case 'h':
				free(mdig_options->domains);
				return GOR_HELP;

			case 's':
				if (inet_aton(optarg, &mdig_options->server.sin_addr) <= 0)
				{
					printf("Invalid name server address: \"%s\"\n\n", optarg);
					goto error;
				}

				got_address = 1;
				break;

			case 'p':
				if (get_port_value(optarg, &mdig_options->server.sin_port) != 0)
				{
					printf("Invalid port value: \"%s\"\n\n", optarg);
					goto error;
				}

				mdig_options->server.sin_port = htons(mdig_options->server.sin_port);
				break;

			case 'c':
				if (get_client_value(optarg, mdig_options->client) != 0)
				{
					printf("Invalid client id: \"%s\"\n\n", optarg);
					goto error;
				}

				mdig_options->got_client = 1;
				break;

			case 'n':
				if (get_query_number_value(optarg, &mdig_options->query_number) != 0)
				{
					printf("Invalid query number: \"%s\"\n\n", optarg);
					goto error;
				}

				mdig_options->got_query_number = 1;
				break;

			case 'l':
				if (get_query_number_value(optarg, &mdig_options->query_limit) != 0)
				{
					printf("Invalid query limit: \"%s\"\n\n", optarg);
					goto error;
				}
				break;

			case 'd':
				if (get_domains(optarg, &mdig_options->domain_count, &mdig_options->domains) != 0)
				{
					printf("Failed to read domains from: \"%s\"\n\n", optarg);
					goto error;
				}

				break;

			case 'o':
				if (open_output(optarg, &mdig_options->output) != 0)
				{
					printf("Can't open file: \"%s\"\n\n", optarg);
					goto error;
				}
				break;

			case 'v':
				mdig_options->verbose = 1;
				break;

			case '?':
				if (optarg)
					printf("Error: invalid option: \"%c\": \"%s\"\n\n", (char) optopt, optarg);
				else
					printf("Error: invalid option: \"%c\"\n\n", (char) optopt);

				goto error;

			default:
				printf("Unexpected getopt_long return value 0x%x \"%c\":\n"
				       "\toptarg..: %s\n"
				       "\toptind..: 0x%x\n"
				       "\toptopt..: 0x%x \"%c\"\n"
				       "\topterr..: 0x%x\n",
				       option_char,
				       (char) option_char,
				       optarg,
				       optind,
				       optopt,
				       (char) optopt,
				       opterr);

				goto error;
		}
	}

	if (!got_address) {
		printf("Missing name server address\n\n");
		goto error;
	}

	if (mdig_options->domain_count < 1)
	{
		printf("Missing domains to query\n\n");
		goto error;
	}

	return GOR_OK;

error:
	usage();
	free(mdig_options->domains);
	if (mdig_options->output != stdout)
	{
		fclose(mdig_options->output);
		mdig_options->output = stdout;
	}

	return GOR_ERROR;
}

int main(int argc, char *argv[])
{
	struct mdig_options mdig_options;
	enum get_options_result r = get_options(argc, argv, &mdig_options);
	if (r == GOR_HELP)
	{
		usage();
		return 0;
	}

	if (r == GOR_ERROR)
	{
		return 1;
	}

	if (mdig_options.verbose) print_domains(mdig_options.domain_count, mdig_options.domains);

	size_t count = mdig_options.got_query_number? mdig_options.query_number : mdig_options.domain_count;
	char *client = mdig_options.got_client? mdig_options.client : NULL;

	unsigned long long write_interval = 0;
	if (mdig_options.query_limit > 0)
	{
		write_interval = NANOSECONDS/mdig_options.query_limit;
		if (NANOSECONDS % mdig_options.query_limit >= NANOSECONDS/2) write_interval++;
	}

	void *queries = make_queries(mdig_options.domains, count, client);
	if (queries == NULL)
	{
		log_errno("Can't allocate buffer for DNS queries.");

		free(mdig_options.domains);
		if (mdig_options.output != stdout)
		{
			fclose(mdig_options.output);
			mdig_options.output = stdout;
		}

		return 1;
	}

	struct timespec *sends = malloc(count*sizeof(struct timespec));
	if (sends == NULL)
	{
		log_errno("Can't allocate send timestamp buffer of %lu bytes.", count*sizeof(struct timespec));

		free(queries);
		free(mdig_options.domains);
		if (mdig_options.output != stdout)
		{
			fclose(mdig_options.output);
			mdig_options.output = stdout;
		}

		return 1;
	}

	struct timespec *receives = malloc(count*sizeof(struct timespec));
	if (receives == NULL)
	{
		log_errno("Can't allocate receive timestamp buffer of %lu bytes.", count*sizeof(struct timespec));

		free(sends);
		free(queries);
		free(mdig_options.domains);
		if (mdig_options.output != stdout)
		{
			fclose(mdig_options.output);
			mdig_options.output = stdout;
		}

		return 1;
	}

	struct pair_timespec *pairs = malloc(count*sizeof(struct pair_timespec));
	if (pairs == NULL)
	{
		log_errno("Can't allocate processing timestamp buffer of %lu bytes.",
		          count*sizeof(struct pair_timespec));

		free(receives);
		free(sends);
		free(queries);
		free(mdig_options.domains);
		if (mdig_options.output != stdout)
		{
			fclose(mdig_options.output);
			mdig_options.output = stdout;
		}

		return 1;
	}

	size_t i;
	for (i = 0; i < count; i++)
	{
		receives[i].tv_sec = 0;
		receives[i].tv_nsec = 0;
		pairs[i].answer = 0;
	}

	log_message("Starting...");
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1)
	{
		log_errno("Can't open UDP socket. Exiting...");

		free(pairs);
		free(receives);
		free(sends);
		free(queries);
		free(mdig_options.domains);
		if (mdig_options.output != stdout)
		{
			fclose(mdig_options.output);
			mdig_options.output = stdout;
		}

		return 1;
	}

	errno = 0;
	int sflags = fcntl(s, F_GETFL);
	if (errno != 0)
	{
		log_errno("Can't get flags for UDP socket. Exiting...");

		close(s);
		free(pairs);
		free(receives);
		free(sends);
		free(queries);
		free(mdig_options.domains);
		if (mdig_options.output != stdout)
		{
			fclose(mdig_options.output);
			mdig_options.output = stdout;
		}

		return 1;
	}

	if (fcntl(s, F_SETFL, sflags | O_NONBLOCK) == -1)
	{
		log_errno("Can't set O_NONBLOCK flag to UDP socket. Exiting...");

		close(s);
		free(pairs);
		free(receives);
		free(sends);
		free(queries);
		free(mdig_options.domains);
		if (mdig_options.output != stdout)
		{
			fclose(mdig_options.output);
			mdig_options.output = stdout;
		}

		return 1;
	}

	fd_set readfds;
	fd_set writefds;

	FD_ZERO(&readfds);
	FD_SET(s, &readfds);

	FD_ZERO(&writefds);
	FD_SET(s, &writefds);

	void *iobuffer = malloc(RECEIVE_BUFFER_SIZE);
	if (iobuffer == NULL)
	{
		log_errno("Can't allocate I/O buffer of %lu size.", (size_t) RECEIVE_BUFFER_SIZE);

		close(s);
		free(pairs);
		free(receives);
		free(sends);
		free(queries);
		free(mdig_options.domains);
		if (mdig_options.output != stdout)
		{
			fclose(mdig_options.output);
			mdig_options.output = stdout;
		}

		return 1;
	}

	char *offset = (char *) queries;
	size_t messages_sent = 0;
	size_t messages_received = 0;
	unsigned long long last_sent = 0;
	while (messages_sent < count)
	{
		struct timespec timeout = {1, 0};
		int fd_count = pselect(s + 1, &readfds, &writefds, NULL, &timeout, 0);
		if (fd_count == -1)
		{
			log_errno("Error on select. Exiting...");

			close(s);
			free(pairs);
			free(receives);
			free(sends);
			free(queries);
			free(mdig_options.domains);
			if (mdig_options.output != stdout)
			{
				fclose(mdig_options.output);
				mdig_options.output = stdout;
			}

			return 1;
		}

		if (fd_count > 0)
		{
			if (FD_ISSET(s, &readfds))
			{
				if (recv_answer(s, &mdig_options.server, iobuffer, RECEIVE_BUFFER_SIZE,
				                &messages_received, count, receives, pairs, mdig_options.verbose) == -1)
				{
					close(s);
					free(pairs);
					free(receives);
					free(sends);
					free(queries);
					free(mdig_options.domains);
					if (mdig_options.output != stdout)
					{
						fclose(mdig_options.output);
						mdig_options.output = stdout;
					}

					return 1;
				}

			}
			else FD_SET(s, &readfds);

			if (FD_ISSET(s, &writefds))
			{
				int do_send_query = write_interval <= 0 || messages_sent <= 0;
				if (!do_send_query)
				{
					struct timespec now;
					if (clock_gettime(CLOCK_SOURCE, &now) == -1)
					{
						log_errno("Error on getting timestamp. Exiting...");

						close(s);
						free(pairs);
						free(receives);
						free(sends);
						free(queries);
						free(mdig_options.domains);
						if (mdig_options.output != stdout)
						{
							fclose(mdig_options.output);
							mdig_options.output = stdout;
						}

						return 1;
					}

					unsigned long long passed = now.tv_sec;
					passed *= NANOSECONDS;
					passed += now.tv_nsec;
					passed -= last_sent;

					do_send_query = passed >= write_interval;
				}

				if (do_send_query)
				{
					size_t size;
					void *q = get_next_query(&offset, &size);

					if (sent_query(s, &mdig_options.server, q, size,
					               &messages_sent, sends, pairs, mdig_options.verbose) == -1)
					{
						close(s);
						free(pairs);
						free(receives);
						free(sends);
						free(queries);
						free(mdig_options.domains);
						if (mdig_options.output != stdout)
						{
							fclose(mdig_options.output);
							mdig_options.output = stdout;
						}

						return 1;
					}

					if (write_interval > 0)
					{
						struct timespec *last_sentspec = &pairs[messages_sent - 1].sent;
						last_sent = last_sentspec->tv_sec;
						last_sent *= NANOSECONDS;
						last_sent += last_sentspec->tv_nsec;
					}
				}
			}
			else FD_SET(s, &writefds);
		}
		else
		{
			FD_SET(s, &readfds);
			FD_SET(s, &writefds);
		}
	}

	size_t attempts = RECV_TIMEOUT;
	while (messages_received < count && attempts > 0)
	{
		struct timeval timeout = {1, 0};
		int fd_count = select(s + 1, &readfds, NULL, NULL, &timeout);

		if (fd_count == -1)
		{
			log_errno("Error on select. Exiting...");

			close(s);
			free(pairs);
			free(receives);
			free(sends);
			free(queries);
			free(mdig_options.domains);
			if (mdig_options.output != stdout)
			{
				fclose(mdig_options.output);
				mdig_options.output = stdout;
			}

			return 1;
		}

		if (fd_count > 0)
		{
			if (FD_ISSET(s, &readfds))
			{
				if (recv_answer(s, &mdig_options.server, iobuffer, RECEIVE_BUFFER_SIZE,
				                &messages_received, count, receives, pairs, mdig_options.verbose) == -1)
				{
					close(s);
					free(pairs);
					free(receives);
					free(sends);
					free(queries);
					free(mdig_options.domains);
					if (mdig_options.output != stdout)
					{
						fclose(mdig_options.output);
						mdig_options.output = stdout;
					}

					return 1;
				}

				attempts = RECV_TIMEOUT;
			}
			else FD_SET(s, &readfds);
		}
		else
		{
			attempts--;
			FD_SET(s, &readfds);
		}
	}

	close(s);

	log_message("Messages:\n"
	            "\tSent....: %ld;\n"
	            "\tReceived: %ld;\n"
	            "\tLost....: %ld.\n\n", count, messages_received, count - messages_received);

	fprintf(mdig_options.output, "{\"sends\":\n\t[");
	if (count > 0)
	{
		unsigned long long timestamp;
		for (i = 0; i < count - 1; i++)
		{
			timestamp = sends[i].tv_sec;
			timestamp *= NANOSECONDS;
			timestamp += sends[i].tv_nsec;

			fprintf(mdig_options.output,
			        "\n\t\t%llu,", timestamp);
		}

		timestamp = sends[i].tv_sec;
		timestamp *= NANOSECONDS;
		timestamp += sends[i].tv_nsec;

		fprintf(mdig_options.output,
		        "\n\t\t%llu\n\t", timestamp);
	}

	fprintf(mdig_options.output, "],\n \"receives\":\n\t[");
	if (count > 0)
	{
		unsigned long long timestamp;
		for (i = 0; i < count - 1; i++)
		{
			timestamp = receives[i].tv_sec;
			timestamp *= NANOSECONDS;
			timestamp += receives[i].tv_nsec;

			fprintf(mdig_options.output,
			        "\n\t\t%llu,", timestamp);
		}

		timestamp = receives[i].tv_sec;
		timestamp *= NANOSECONDS;
		timestamp += receives[i].tv_nsec;

		fprintf(mdig_options.output,
		        "\n\t\t%llu\n\t", timestamp);
	}

	fprintf(mdig_options.output, "],\n \"pairs\":\n\t[");
	if (count > 0)
	{
		unsigned long long sent;
		for (i = 0; i < count - 1; i++)
		{
			sent = pairs[i].sent.tv_sec;
			sent *= NANOSECONDS;
			sent += pairs[i].sent.tv_nsec;

			if (pairs[i].answer > 0)
			{
				unsigned long long received = pairs[i].received.tv_sec;
				received *= NANOSECONDS;
				received += pairs[i].received.tv_nsec;

				fprintf(mdig_options.output,
				        "\n\t\t[%llu, %llu, %lld],", sent, received, received - sent);
			}
			else
			{
				fprintf(mdig_options.output,
				        "\n\t\t[%llu],", sent);
			}
		}

		sent = pairs[i].sent.tv_sec;
		sent *= NANOSECONDS;
		sent += pairs[i].sent.tv_nsec;

		if (pairs[i].answer > 0)
		{
			unsigned long long received = pairs[i].received.tv_sec;
			received *= NANOSECONDS;
			received += pairs[i].received.tv_nsec;

			fprintf(mdig_options.output,
			        "\n\t\t[%llu, %llu, %lld]\n\t", sent, received, received - sent);
		}
		else
		{
			fprintf(mdig_options.output,
			        "\n\t\t[%llu]\n\t", sent);
		}
	}

	fprintf(mdig_options.output, "]\n}\n");
	if (mdig_options.output != stdout)
	{
		fclose(mdig_options.output);
		mdig_options.output = stdout;
	}

	log_message("Exiting...");
	return 0;
}


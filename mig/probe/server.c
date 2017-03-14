#include <stdio.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <signal.h>

#include "logger.h"
#include "message_queue.h"

#ifdef CLOCK_MONOTONIC_RAW
	#define CLOCK_SOURCE CLOCK_MONOTONIC_RAW
#else
	#define CLOCK_SOURCE CLOCK_MONOTONIC
#endif

#ifdef SIGINFO
	#define SIGDUMPTIMESTAMPS SIGINFO
#else
	#define SIGDUMPTIMESTAMPS SIGUSR1
#endif

#define NANOSECONDS 1000000000

#define RECEIVE_BUFFER_SIZE 65535
#define SEND_BUFFER_SIZE 65535
#define SEND_QUEUE_SIZE 100*1024*1024

#define TIMESTAMPS_MAXLENGTH 10000000

#ifndef STDIN_FILENO
	#define STDIN_FILENO 0
#endif
#define STDIN_BUFFER_SIZE 10240
#define STOP_CHARACTER 's'

int check_int_code()
{
	char buffer[STDIN_BUFFER_SIZE];
	while (1) {
		size_t bytes_received = fread(buffer, sizeof(char), sizeof(buffer)/sizeof(char), stdin);
		if (bytes_received == 0) {
			if (errno == EAGAIN) break;

			log_errno("Error on reading stdin.");
			return -1;
		}

		size_t i;
		for (i = 0; i < bytes_received; i++)
		{
			if (buffer[i] == STOP_CHARACTER) {
				log_message("Stop character received.");
				return -1;
			}
		}
	}

	return 0;
}

struct dns_query
{
	unsigned short transaction_id;
	unsigned short flags;
	unsigned short questions;
	unsigned short answers;
	unsigned short authorities;
	unsigned short additional;
};

void make_refused_answer(void *query, size_t query_size, void *answer, size_t *answer_size)
{
	memcpy(answer, query, query_size);
	*answer_size = query_size;

	struct dns_query *header = (struct dns_query *) answer;
	header->flags = (0x7079 & header->flags) | 0x0580;
}

int make_answer(void *query, size_t query_size, void *answer, size_t *answer_size)
{
	if (query_size < sizeof(struct dns_query))
	{
		log_error("Expected query size at least of DNS query header size (%lu) but got only %lu.",
		          sizeof(struct dns_query), query_size);
		return -1;
	}

	struct dns_query *header = (struct dns_query *) query;

	unsigned short flags = header->flags;
	if (flags & ~0x1)
	{
		make_refused_answer(query, query_size, answer, answer_size);
		return 0;
	}

	if (header->questions != 0x100)
	{
		make_refused_answer(query, query_size, answer, answer_size);
		return 0;
	}

	if (header->answers != 0)
	{
		make_refused_answer(query, query_size, answer, answer_size);
		return 0;
	}

	if (header->authorities != 0)
	{
		make_refused_answer(query, query_size, answer, answer_size);
		return 0;
	}

	char *src = (char *) query;

	size_t name_size = strlen(src + sizeof(struct dns_query)) + 1;
	size_t question_size = name_size + 2*sizeof(unsigned short);
	size_t before_answer_size =  sizeof(struct dns_query) + question_size;
	if (query_size < before_answer_size)
	{
		log_error("Expected query size at least of DNS query header size and question section size (%lu) "
		          "but got only %lu.",
		          before_answer_size, query_size);
		return -1;
	}

	unsigned short *query_type = (unsigned short *) (src + sizeof(struct dns_query) + name_size);
	if (*query_type != 0x100)
	{
		make_refused_answer(query, query_size, answer, answer_size);
		return 0;
	}

	char *dest = (char *) answer;

	memcpy(dest, src, before_answer_size);
	header = (struct dns_query *) dest;
	header->flags = (0x7079 & header->flags) | 0x0084;
	header->answers = htons(1);

	dest += before_answer_size;
	src += before_answer_size;

	memcpy(dest, "\xc0\x0c\x00\x01\x00\x01\x00\x00\x0e\x10\x00\x04\x01\x02\x03\x04", 16);
	dest += 16;

	if (query_size > before_answer_size) memcpy(dest, src, query_size - before_answer_size);

	*answer_size = query_size + 16;

	return 0;
}

void usage(void)
{
	printf("server - dummy DNS performance measurement server\n"
	       "         only responds to A query with the same A record)\n\n"
	       "Usage: server <options>\n\n"
	       "Options:\n"
	       "\t-a, --address - IPv4 address to listen on (required);\n"
	       "\t-p, --port    - port (default 53);\n"
	       "\t-o, --output  - report send and receive timestamps to given file (limited to 10.000.000 items);\n"
	       "\t-h, --help    - this message.\n");
}

struct server_options
{
	struct sockaddr_in address;
	const char *output;
};

static struct option long_options[] = {
	{"help",    no_argument,       NULL, 'h'},
	{"address", required_argument, NULL, 'a'},
	{"port",    required_argument, NULL, 'p'},
	{"output",  required_argument, NULL, 'o'},
	{NULL,	    0,		       NULL, 0}
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

enum get_options_result get_options(int argc, char *argv[], struct server_options *server_options)
{
	opterr = 0;
	int option_char;

	server_options->address.sin_family = AF_INET;
	server_options->address.sin_port = htons(53);
	server_options->output = NULL;

	int got_address = 0;
	while ((option_char = getopt_long(argc, argv, "ha:p:o:", long_options, NULL)) != -1)
	{
		switch (option_char)
		{
			case 'h':
				return GOR_HELP;

			case 'a':
				if (inet_aton(optarg, &server_options->address.sin_addr) <= 0)
				{
					printf("Invalid address: \"%s\"\n\n", optarg);
					return GOR_ERROR;
				}

				got_address = 1;
				break;

			case 'p':
				if (get_port_value(optarg, &server_options->address.sin_port) != 0)
				{
					printf("Invalid port value: \"%s\"\n\n", optarg);
					return GOR_ERROR;
				}

				server_options->address.sin_port = htons(server_options->address.sin_port);
				break;

			case 'o':
				server_options->output = optarg;
				break;

			case '?':
				if (optarg)
					printf("Error: invalid option: \"%c\": \"%s\"\n\n", (char) optopt, optarg);
				else
					printf("Error: invalid option: \"%c\"\n\n", (char) optopt);

				return GOR_ERROR;

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

				return GOR_ERROR;
		}
	}

	if (!got_address) {
		printf("Missing address\n\n");
		return GOR_ERROR;
	}

	return GOR_OK;
}

int recv_all(int s, void *recv_buffer, void *send_buffer, struct message_queue *queue,
             size_t *messages_received, struct timespec *receives, size_t *index)
{
	while (1)
	{
		struct sockaddr_storage client;
		socklen_t clinet_length = sizeof(client);
		ssize_t bytes_received = recvfrom(s, recv_buffer, RECEIVE_BUFFER_SIZE, 0,
		                                  (struct sockaddr *) &client, &clinet_length);
		if (bytes_received == -1)
		{
			if (errno == EAGAIN) break;

			log_errno("Error on receiving.");
			return -1;
		}

		size_t bytes_to_send;
		if (make_answer(recv_buffer, bytes_received, send_buffer, &bytes_to_send) != 0) return -1;

		if (push_message(queue,
		                 (struct sockaddr *) &client, clinet_length,
		                 send_buffer, bytes_to_send) != 0)
		{
			log_error("Failed to queue message to send.");
			return -1;
		}

		(*messages_received)++;
		if (receives && *index < TIMESTAMPS_MAXLENGTH)
		{
			if (clock_gettime(CLOCK_SOURCE, receives + *index) == -1)
			{
				log_errno("Error on getting timestamp.");
				return -1;
			}

			(*index)++;
		}
	}

	return 0;
}

int send_all(int s, struct message_queue *queue, struct timespec *sends, size_t *index)
{
	while (!IS_QUEUE_EMPTY(*queue))
	{
		struct sockaddr *client;
		socklen_t client_length;
		void *message;
		size_t message_length;
		void *head;

		if (get_message(queue, &head, &client, &client_length, &message, &message_length) != 0)
		{
			log_error("Failed to get message to send.");
			return -1;
		}

		ssize_t bytes_sent = sendto(s, message, message_length, 0, client, client_length);
		if (bytes_sent < 0)
		{
			if (errno == EAGAIN) break;

			log_errno("Error on sending.");
			return -1;
		}

		if (bytes_sent < message_length)
		{
			log_error("Expected to send %lu bytes but sent only %ld.", message_length, bytes_sent);
			return -1;
		}

		queue->head = head;
		if (sends && *index < TIMESTAMPS_MAXLENGTH)
		{
			if (clock_gettime(CLOCK_SOURCE, sends + *index) == -1)
			{
				log_errno("Error on getting timestamp.");
				return -1;
			}

			(*index)++;
		}
	}

	return 0;
}

volatile sig_atomic_t do_dump_timestamps = 0;

void catch_info(int sig)
{
	do_dump_timestamps = 1;
	signal(sig, catch_info);
}

int dump_timestamps(const char *name,
                    struct timespec *receives, size_t receives_count,
                    struct timespec *sends, size_t sends_count)
{
	FILE *f = fopen(name, "w");
	if (f == NULL)
	{
		log_errno("Can't open file %s.", name);
		return -1;
	}

	size_t i;

	fprintf(f, "{\"receives\":\n\t[");
	if (receives_count > 0)
	{
		unsigned long long timestamp;
		for (i = 0; i < receives_count - 1; i++)
		{
			timestamp = receives[i].tv_sec;
			timestamp *= NANOSECONDS;
			timestamp += receives[i].tv_nsec;

			fprintf(f, "\n\t\t%llu,", timestamp);
		}

		timestamp = receives[i].tv_sec;
		timestamp *= NANOSECONDS;
		timestamp += receives[i].tv_nsec;

		fprintf(f, "\n\t\t%llu\n\t", timestamp);
	}

	fprintf(f, "],\n \"sends\":\n\t[");
	if (sends_count > 0)
	{
		unsigned long long timestamp;
		for (i = 0; i < sends_count - 1; i++)
		{
			timestamp = sends[i].tv_sec;
			timestamp *= NANOSECONDS;
			timestamp += sends[i].tv_nsec;

			fprintf(f, "\n\t\t%llu,", timestamp);
		}

		timestamp = sends[i].tv_sec;
		timestamp *= NANOSECONDS;
		timestamp += sends[i].tv_nsec;

		fprintf(f, "\n\t\t%llu\n\t", timestamp);
	}

	fprintf(f, "]\n}\n");

	fclose(f);

	log_message("Dumped %lu receive events and %lu send events.", receives_count, sends_count);
	return 0;
}

void serve(int s, void *recv_buffer, void *send_buffer, struct message_queue *queue,
           const char * name, struct timespec *receives, struct timespec * sends)
{
	fd_set readfds;
	fd_set writefds;
	fd_set *pwritefds = &writefds;

	FD_ZERO(&readfds);
	FD_SET(s, &readfds);
	FD_SET(STDIN_FILENO, &readfds);

	FD_ZERO(pwritefds);

	size_t messages_received = 0;
	size_t receives_position = 0;
	size_t sends_position = 0;
	while (1)
	{
		if (do_dump_timestamps)
		{
			do_dump_timestamps = 0;
			if (dump_timestamps(name, receives, receives_position, sends, sends_position) != 0) return;

			receives_position = 0;
			sends_position = 0;
		}

		if (!IS_QUEUE_EMPTY(*queue))
		{
			pwritefds = &writefds;
			FD_SET(s, pwritefds);
		}

		struct timespec timeout = {1, 0};
		int fd_count = pselect((s > STDIN_FILENO)? s + 1 : STDIN_FILENO + 1,
		                       &readfds, pwritefds, NULL, &timeout, 0);
		if (fd_count == -1) {
			if (errno != EINTR)
			{
				log_errno("Error on waiting for request.");
				return;
			}

			fd_count = 0;
		}

		if (fd_count > 0)
		{
			if (FD_ISSET(s, &readfds))
 			{
				if (recv_all(s, recv_buffer, send_buffer, queue,
				    &messages_received, receives, &receives_position) != 0) return;
			}
			else FD_SET(s, &readfds);

			if (FD_ISSET(STDIN_FILENO, &readfds))
			{
				if (check_int_code() != 0) return;
			}
			else FD_SET(STDIN_FILENO, &readfds);

			if (pwritefds)
			{
				if (FD_ISSET(s, pwritefds))
				{
					if (send_all(s, queue,
					             sends, &sends_position) != 0) return;

					if (IS_QUEUE_EMPTY(*queue))
					{
						FD_CLR(s, pwritefds);
						pwritefds = NULL;
					}

				} else FD_SET(s, pwritefds);
			}
		}
		else
		{
			FD_SET(s, &readfds);
			FD_SET(STDIN_FILENO, &readfds);
			if (pwritefds) FD_SET(s, pwritefds);

			if (messages_received > 0)
			{
				log_message("Got %lu message(s).", messages_received);
				messages_received = 0;
			}
		}
	}

}

int main(int argc, char *argv[])
{
        struct server_options server_options;
        enum get_options_result r = get_options(argc, argv, &server_options);
	if (r != GOR_OK)
	{
		usage();
		return (r == GOR_ERROR)? 1 : 0;
        }

	log_message("Starting...");

	errno = 0;
	int sflags = fcntl(STDIN_FILENO, F_GETFL);
	if (errno != 0)
	{
		log_errno("Can't get flags for stdin. Exiting...");
		return 1;
	}
	log_message("Got stdin flags 0x%x.", sflags);

	sflags |= O_NONBLOCK;
	if (fcntl(STDIN_FILENO, F_SETFL, sflags) == -1)
	{
		log_errno("Can't set O_NONBLOCK flag to stdin. Exiting...");
		return 1;
	}
	log_message("Made stdin nonblocking (0x%x).", sflags);

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1)
	{
		log_errno("Can't open UDP socket. Exiting...");
		return 1;
	}
	log_message("Got socket %d.", s);

	errno = 0;
	sflags = fcntl(s, F_GETFL);
	if (errno != 0)
	{
		log_errno("Can't get flags for UDP socket. Exiting...");
		close(s);
		return 1;
	}
	log_message("Got socket flags 0x%x.", sflags);

	sflags |= O_NONBLOCK;
	if (fcntl(s, F_SETFL, sflags) == -1)
	{
		log_errno("Can't set O_NONBLOCK flag to UDP socket. Exiting...");
		close(s);
		return 1;
	}
	log_message("Made socket nonblocking (0x%x).", sflags);

	char address[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &server_options.address.sin_addr, address, sizeof(address));
	if (bind(s, (struct sockaddr *) &server_options.address, sizeof(server_options.address)) != 0)
	{
		int errnum = errno;

		log_errno_ex(errnum, "Can't bind socket to %s:%hu. Exiting...",
		             address, htons(server_options.address.sin_port));
		close(s);
		return 1;
	}
	log_message("Bound to %s:%hu.", address, htons(server_options.address.sin_port));

	void *recv_buffer = malloc(RECEIVE_BUFFER_SIZE);
	if (recv_buffer == NULL)
	{
		log_errno("Can't allocate %lu bytes for receiver buffer. Exiting...", (size_t) RECEIVE_BUFFER_SIZE);

		close(s);
		return 1;
	}
	log_message("Allocated %lu bytes for receiver buffer.", (size_t) RECEIVE_BUFFER_SIZE);

	void *send_buffer = malloc(SEND_BUFFER_SIZE);
	if (send_buffer == NULL)
	{
		log_errno("Can't allocate %lu bytes for sender buffer. Exiting...", (size_t) SEND_BUFFER_SIZE);

		free(recv_buffer);
		close(s);
		return 1;
	}
	log_message("Allocated %lu bytes for sender buffer.", (size_t) SEND_BUFFER_SIZE);

	struct message_queue queue;
	if (make_message_queue(SEND_QUEUE_SIZE, &queue) != 0)
	{
		log_error("Can't make message queue of %lu bytes. Exiting...", (size_t) SEND_QUEUE_SIZE);

		free(send_buffer);
		free(recv_buffer);
		close(s);
		return 1;
	}
	log_message("Created message queue of %lu bytes.", (size_t) SEND_QUEUE_SIZE);

	struct timespec *receives = NULL;
	struct timespec *sends = NULL;
	if (server_options.output != NULL)
	{
		receives = (struct timespec *) malloc(TIMESTAMPS_MAXLENGTH*sizeof(struct timespec));
		if (receives == NULL)
		{
			log_errno("Can't allocate %lu bytes for receive timestamps. Exiting...",
			          TIMESTAMPS_MAXLENGTH*sizeof(struct timespec));

			free(send_buffer);
			free(recv_buffer);
			close(s);
			return 1;
		}

		sends = (struct timespec *) malloc(TIMESTAMPS_MAXLENGTH*sizeof(struct timespec));
		if (sends == NULL)
		{
			log_errno("Can't allocate %lu bytes for send timestamps. Exiting...",
			          TIMESTAMPS_MAXLENGTH*sizeof(struct timespec));

			free(receives);
			free(send_buffer);
			free(recv_buffer);
			close(s);
			return 1;
		}

		signal(SIGDUMPTIMESTAMPS, catch_info);
	}

	log_message("Serving...");

	serve(s, recv_buffer, send_buffer, &queue, server_options.output, receives, sends);

	log_message("Exiting...");

	free(sends);
	free(receives);
	free(queue.buffer);
	free(send_buffer);
	free(recv_buffer);
	close(s);
	return 0;
}

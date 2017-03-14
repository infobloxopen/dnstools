#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include "message_queue.h"
#include "logger.h"

int make_message_queue(size_t size, struct message_queue *queue)
{
	void *buffer = malloc(size);
	if (buffer == NULL)
	{
		log_errno("Can't allocate %lu bytes for message queue.", size);
		return -1;
	}

	queue->size = size;
	queue->buffer = buffer;

	queue->head = buffer;
	queue->tail = buffer;

	return 0;
}

#define MESSAGE_FLAG '\0'
#define QUEUE_WRAP_FLAG '\xff'

int push_message_to_the_limit(char **tail, char *limit,
                              struct sockaddr *address, socklen_t address_length,
                              void *message, size_t size)
{
	size_t message_size = 1 + sizeof(address_length) + address_length + sizeof(size) + size;
	if (*tail + message_size >= limit) return -1;

	**tail = MESSAGE_FLAG;
	(*tail)++;

	memcpy(*tail, &address_length, sizeof(address_length));
	*tail += sizeof(address_length);

	memcpy(*tail, address, address_length);
	*tail += address_length;

	memcpy(*tail, &size, sizeof(size));
	*tail += sizeof(size);

	memcpy(*tail, message, size);
	*tail += size;

	return 0;
}

int push_message(struct message_queue *queue,
                 struct sockaddr *address, socklen_t address_length,
                 void *message, size_t size)
{
	char *head = (char *) queue->head;
	char *tail = (char *) queue->tail;

	if (tail < head)
	{
		if (push_message_to_the_limit(&tail, head,
		                              address, address_length, message, size) != 0) return -1;
	}
	else
	{
		char *buffer = (char *) queue->buffer;
		if (push_message_to_the_limit(&tail, buffer + queue->size,
		                              address, address_length, message, size) != 0)
		{
			if (head == buffer) return -1;

			*tail = QUEUE_WRAP_FLAG;
			tail = buffer;

			if (push_message_to_the_limit(&tail, head,
			                              address, address_length, message, size) !=0) return -1;
		}
	}

	queue->tail = tail;
	return 0;
}

int get_message(const struct message_queue *queue, void **new_head,
                struct sockaddr **address, socklen_t *address_length,
                void **message, size_t *size)
{
	char *head = (char *) queue->head;
	if (head == (char *) queue->tail) return -1;

	if (*head == QUEUE_WRAP_FLAG) head = (char *) queue->buffer;
	head++;

	memcpy(address_length, head, sizeof(socklen_t));
	head += sizeof(socklen_t);

	*address = (struct sockaddr *) head;
	head += *address_length;

	memcpy(size, head, sizeof(size_t));
	head += sizeof(size_t);

	*message = head;
	head += *size;

	*new_head = head;

	return 0;
}

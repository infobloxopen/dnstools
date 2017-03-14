#ifndef __MESSAGE_QUEUE_H__
#define __MESSAGE_QUEUE_H__

#define IS_QUEUE_EMPTY(q) ((q).head == (q).tail)

struct message_queue
{
	size_t size;
	void *buffer;

	void *head;
	void *tail;
};

int make_message_queue(size_t size, struct message_queue *queue);
int push_message(struct message_queue *queue,
                 struct sockaddr *address, socklen_t address_length,
                 void *message, size_t size);
int get_message(const struct message_queue *queue, void **new_head,
                struct sockaddr **address, socklen_t *address_length,
                void **message, size_t *size);

#endif // __MESSAGE_QUEUE_H__

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "network.h"

#define QUEUE_SIZE 64
typedef struct {
	message_t data[QUEUE_SIZE];
	int count;
	int first;
	int last;
	SDL_SpinLock lock;
} queue_t;

static bool enqueue(queue_t *queue, message_t *item) {
	bool result = false;
	// printf("ENQ begin\n");
	SDL_AtomicLock(&queue->lock);
	if (queue->count < QUEUE_SIZE) {
		queue->data[queue->last] = *item;
		queue->last = (queue->last + 1) % QUEUE_SIZE;
		queue->count++;
		result = true;
	}
	SDL_AtomicUnlock(&queue->lock);
	// printf("ENQ end\n");
	return result;
}

static bool dequeue(queue_t *queue, message_t *message) {
	bool result = false;
	// printf("DEQ begin\n");
	SDL_AtomicLock(&queue->lock);
	if (queue->count) {
		*message = queue->data[queue->first];
		queue->first = (queue->first + 1) % QUEUE_SIZE;
		queue->count--;
		result = true;
	}
	SDL_AtomicUnlock(&queue->lock);
	// printf("DEQ end\n");
	return result;
}

// Returns socket descriptor on success, -1 on failure
static int start_server(char *port) {
	int sockd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockd == -1) {
		perror("ERROR create socket");
		return -1;
	}
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(port));
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sockd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		char buff[100];
		sprintf(buff, "ERROR: bind socket to port %s", port);
		perror(buff);
		return -1;
	}
	if (listen(sockd, 1) == -1) {
		perror("ERROR listen");
		return -1;
	}
	printf("Waiting for connections on port %s...\n", port);
	int nsockd = accept(sockd, 0, 0);
	if (nsockd == -1) {
		perror("ERROR accept");
		return -1;
	}
	printf("Connected with client\n");
	if (close(sockd) == -1) {
		perror("ERROR close");
		return 1;
	}
	return nsockd;
}

// Returns socket descriptor on success, -1 on failure
static int start_client(char *host, char *port) {
	struct addrinfo hints = {};
	hints.ai_flags = 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	struct addrinfo *info;
	int err = getaddrinfo(host, port, &hints, &info);
	if (err) {
		fprintf(stderr, "ERROR: getaddrinfo: %s\n", gai_strerror(err));
		return -1;
	}

	int sockd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockd == -1) {
		perror("ERROR create socket");
		freeaddrinfo(info);
		return -1;
	}
	printf("Connecting with server %s at port %s...\n", host, port);
	if (connect(sockd, info->ai_addr, info->ai_addrlen) == -1) {
		perror("ERROR connect");
		freeaddrinfo(info);
		return -1;
	}
	printf("Connected with server\n");
	freeaddrinfo(info);
	return sockd;
}

struct _net_state {
	int sock;
	queue_t recv_queue;
	SDL_Thread *recv_thread;
	queue_t send_queue;
	SDL_Thread *send_thread;
};

static int recv_thread_proc(void *data) {
	net_state_t *net = data;
	char buffer[256];
	for (;;) {
		int rc = read(net->sock, buffer, sizeof(buffer) - 1);
		if (rc == -1) {
			perror("ERROR read socket");
			return 1;
		}
		message_t msg = {};
		if (rc == 0) {
			msg.type = MSG_CLOSE;
		} else {
			if (buffer[rc - 1]) {
				fprintf(stderr, "ERROR: received message isn't null terminated\n");
				return 1;
			}
			msg.type = MSG_STR;
			strcpy(msg.str, buffer);
		}
		if (!enqueue(&net->recv_queue, &msg)) {
			fprintf(stderr, "ERROR: receive queue is full\n");
			return 1;
		}

		if (msg.type == MSG_CLOSE) {
			break;
		}
	}
	// printf("DEBUG exited recv_thread_proc\n");
	return 0;
}

static int send_thread_proc(void *data) {
	net_state_t *net = data;
	message_t msg;
	for (;;) {
		if (dequeue(&net->send_queue, &msg)) {
			if (msg.type == MSG_CLOSE) {
				break;
			} else if (msg.type == MSG_STR) {
				int msg_len = strlen(msg.str) + 1;
				int wc = write(net->sock, msg.str, msg_len);
				if (wc == -1) {
					perror("ERROR write socket");
					return 1;
				} else if (wc != msg_len) {
					fprintf(stderr, "ERROR: write failed to write all bytes\n");
					return 1;
				}
			}
		} else {
			SDL_Delay(50);
		}
	}
	// printf("DEBUG exited send_thread_proc\n");
	return 0;
}

extern net_state_t *net_start(net_mode_t mode, char *host, char *port) {
	net_state_t *net = malloc(sizeof(net_state_t));
	if (!net) {
		perror("ERROR malloc");
		return 0;
	}
	memset(net, 0, sizeof(net_state_t));

	net->sock = -1;
	switch (mode) {
		case NET_SERVER:
			net->sock = start_server(port);
			break;
		case NET_CLIENT:
			net->sock = start_client(host, port);
			break;
	}
	if (net->sock == -1)
		goto error;

	net->recv_thread = SDL_CreateThread(recv_thread_proc, "receiver", net);
	if (!net->recv_thread) {
		fprintf(stderr, "ERROR SDL_CreateThread: %s\n", SDL_GetError());
		goto error;
	}
	net->send_thread = SDL_CreateThread(send_thread_proc, "sender", net);
	if (!net->send_thread) {
		fprintf(stderr, "ERROR SDL_CreateThread: %s\n", SDL_GetError());
		goto error;
	}

	return net;
error:
	free(net);
	return 0;
}

extern void net_quit(net_state_t *net) {
	int thread_res;
	SDL_WaitThread(net->recv_thread, &thread_res);
	if (thread_res)
		fprintf(stderr, "receiver thread exit with error\n");
	SDL_WaitThread(net->send_thread, &thread_res);
	if (thread_res)
		fprintf(stderr, "sender thread exit with error\n");

	if (close(net->sock) == -1)
		perror("ERROR close");
}

extern bool net_poll_message(net_state_t *net, message_t *msg) {
	if (dequeue(&net->recv_queue, msg)) {
		if (msg->type == MSG_CLOSE) {
			if (!enqueue(&net->send_queue, msg)) {
				fprintf(stderr, "ERROR: send message queue is full\n");
				return false;
			}
		}
		return true;
	} else {
		return false;
	}
}

extern bool net_send_message(net_state_t *net, message_t *msg) {
	if (!enqueue(&net->send_queue, msg)) {
		fprintf(stderr, "ERROR: send message queue is full\n");
		return false;
	}
	if (msg->type == MSG_CLOSE) {
		if (shutdown(net->sock, SHUT_RDWR) == -1) {
			perror("ERROR shutdown");
			return false;
		}
	}
	return true;
}

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

typedef enum {
	MSG_CLOSE,
	MSG_STR,
} message_type_t;

typedef struct {
	message_type_t type;
	char str[256];
} message_t;

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
		perror("ERROR: create socket");
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
		perror("ERROR: listen");
		return -1;
	}
	printf("Waiting for connections on port %s...\n", port);
	int nsockd = accept(sockd, 0, 0);
	if (nsockd == -1) {
		perror("ERROR: accept");
		return -1;
	}
	printf("Connected with client\n");
	if (close(sockd) == -1) {
		perror("ERROR: close");
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
		perror("ERROR: create socket");
		freeaddrinfo(info);
		return -1;
	}
	printf("Connecting with server %s at port %s...\n", host, port);
	if (connect(sockd, info->ai_addr, info->ai_addrlen) == -1) {
		perror("ERROR: connect");
		freeaddrinfo(info);
		return -1;
	}
	printf("Connected with server\n");
	freeaddrinfo(info);
	return sockd;
}

struct thread_ctx {
	queue_t *queue;
	int sock;
};

static int recv_thread_proc(void *data) {
	struct thread_ctx ctx = *((struct thread_ctx*)data);
	char buffer[256];
	for (;;) {
		int rc = read(ctx.sock, buffer, sizeof(buffer) - 1);
		if (rc == -1) {
			perror("ERROR: read socket");
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
		enqueue(ctx.queue, &msg);

		if (msg.type == MSG_CLOSE) {
			break;
		}
	}
	// printf("DEBUG exited recv_thread_proc\n");
	return 0;
}

static int send_thread_proc(void *data) {
	struct thread_ctx ctx = *((struct thread_ctx*)data);
	message_t msg;
	for (;;) {
		if (dequeue(ctx.queue, &msg)) {
			if (msg.type == MSG_CLOSE) {
				break;
			} else if (msg.type == MSG_STR) {
				int msg_len = strlen(msg.str) + 1;
				int wc = write(ctx.sock, msg.str, msg_len);
				if (wc == -1) {
					perror("ERROR: write socket");
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

void exit_usage(char *script) {
	fprintf(stderr,
		"Usage:\n"
		"    %s server PORT\n"
		"    %s client HOST PORT\n",
		script, script
	);
	exit(1);
}

int main(int argc, char **argv) {
	int sockd = -1;
	bool server = false;
	if (argc < 2)
		exit_usage(argv[0]);
	if (strcmp(argv[1], "server") == 0) {
		if (argc != 3)
			exit_usage(argv[0]);
		char *port = argv[2];
		sockd = start_server(port);
		server = true;
	} else if (strcmp(argv[1], "client") == 0) {
		if (argc != 4)
			exit_usage(argv[0]);
		char *host = argv[2];
		char *port = argv[3];
		sockd = start_client(host, port);
	} else {
		exit_usage(argv[0]);
	}

	if (sockd == -1)
		return 1;

	queue_t recv_queue = {};
	struct thread_ctx recv_ctx = { &recv_queue, sockd };
	SDL_Thread *recv_thread = SDL_CreateThread(recv_thread_proc, "receiver", &recv_ctx);

	queue_t send_queue = {};
	struct thread_ctx send_ctx = { &send_queue, sockd };
	SDL_Thread *send_thread = SDL_CreateThread(send_thread_proc, "sender", &send_ctx);

	bool send = !server;
	for (;;) {
		message_t msg;
		if (send) {
			send = false;

			printf("> ");
			fgets(msg.str, 255, stdin);
			msg.str[strlen(msg.str) - 1] = 0; // remove newline
			if (strcmp(msg.str, "!close") == 0) {
				msg.type = MSG_CLOSE;
			} else {
				msg.type = MSG_STR;
			}
			enqueue(&send_queue, &msg);
			if (msg.type == MSG_CLOSE) {
				if (shutdown(sockd, SHUT_RDWR) == -1) {
					perror("ERROR: shutdown");
					return 1;
				}
				break;
			}
		} else {
			send = true;

			while (!dequeue(&recv_queue, &msg)) {
				SDL_Delay(50);
			}
			if (msg.type == MSG_STR) {
				printf("MSG_STR: %s\n", msg.str);
			} else if (msg.type == MSG_CLOSE) {
				enqueue(&send_queue, &msg);
				printf("Connection closed by %s\n", server ? "client" : "server");
				break;
			}
		}
	}

	printf("Exiting...\n");

	int thread_res;
	SDL_WaitThread(recv_thread, &thread_res);
	if (thread_res)
		fprintf(stderr, "receiver thread exit with error\n");
	SDL_WaitThread(send_thread, &thread_res);
	if (thread_res)
		fprintf(stderr, "sender thread exit with error\n");

	if (close(sockd) == -1) {
		perror("ERROR: close");
		return 1;
	}
}

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "network.h"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
typedef int ssize_t;
#define SHUT_WR SD_BOTH
void log_sock_error(char *msg) {
	char *res;
	int flags = FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
	FormatMessageA(flags, 0, WSAGetLastError(), 0, &res, 0, 0);
	fprintf(stderr, "%s: %s\n", msg, res);
	LocalFree(res);
}

#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define log_sock_error(msg) perror(msg)
#define closesocket(sock) close(sock)

#endif

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
static sock_t start_server(char *port) {
	sock_t client_sock = INVALID_SOCKET;
	sock_t server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == INVALID_SOCKET) {
		log_sock_error("ERROR create socket");
		goto exit;
	}
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(port));
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
		char buff[100];
		sprintf(buff, "ERROR: bind socket to port %s", port);
		log_sock_error(buff);
		goto exit;
	}
	if (listen(server_sock, 1) == SOCKET_ERROR) {
		log_sock_error("ERROR listen");
		goto exit;
	}
	printf("Waiting for connections on port %s...\n", port);
	client_sock = accept(server_sock, 0, 0);
	if (client_sock == INVALID_SOCKET) {
		log_sock_error("ERROR accept");
		goto exit;
	}
	printf("Connected with client\n");

exit:
	if (server_sock != INVALID_SOCKET) {
		if (closesocket(server_sock) == SOCKET_ERROR) {
			log_sock_error("ERROR closesocket");
			client_sock = INVALID_SOCKET;
		}
	}
	return client_sock;
}

// Returns socket descriptor on success, -1 on failure
static sock_t start_client(char *host, char *port) {
	sock_t result = INVALID_SOCKET;

	struct addrinfo hints = {0};
	hints.ai_flags = 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	struct addrinfo *info;
	int err = getaddrinfo(host, port, &hints, &info);
	if (err) {
		fprintf(stderr, "ERROR getaddrinfo: %s\n", gai_strerror(err));
		goto exit;
	}

	result = socket(AF_INET, SOCK_STREAM, 0);
	if (result == INVALID_SOCKET) {
		log_sock_error("ERROR create socket");
		goto exit;
	}
	printf("Connecting with server %s at port %s...\n", host, port);
	if (connect(result, info->ai_addr, info->ai_addrlen) == SOCKET_ERROR) {
		log_sock_error("ERROR connect");
		if (closesocket(result) == SOCKET_ERROR)
			log_sock_error("ERROR closesocket");
		result = INVALID_SOCKET;
		goto exit;
	}
	printf("Connected with server\n");

exit:
	if (info)
		freeaddrinfo(info);
	return result;
}

struct _net_state {
	SDL_atomic_t open;
	sock_t sock;
	queue_t recv_queue;
	SDL_Thread *recv_thread;
	queue_t send_queue;
	SDL_Thread *send_thread;
};

static int recv_thread_proc(void *data) {
	net_state_t *net = data;
	char buffer[256];
	while (SDL_AtomicGet(&net->open)) {
		ssize_t rc = recv(net->sock, buffer, sizeof(buffer) - 1, 0);
		if (rc == SOCKET_ERROR) {
			log_sock_error("ERROR recv socket");
			goto error;
		}
		if (rc == 0) {
			SDL_AtomicSet(&net->open, 0);
			break;
		} else {
			message_t msg = {0};
			if (buffer[rc - 1]) {
				fprintf(stderr, "ERROR: received message isn't null terminated\n");
				goto error;
			}
			// parse message
			int status = sscanf(
				buffer,
				"MOVE %d %d TO %d %d",
				&msg.move_piece.row, &msg.move_piece.col,
				&msg.move_target.row, &msg.move_target.col
			);
			if (status != 4) {
				fprintf(stderr, "ERROR: failed to parse message\n");
				goto error;
			}
			if (!enqueue(&net->recv_queue, &msg)) {
				fprintf(stderr, "ERROR: receive queue is full\n");
				goto error;
			}
		}
	}
	// printf("DEBUG exited recv_thread_proc\n");
	return 0;
error:
	SDL_AtomicSet(&net->open, 0);
	return 1;
}

static int send_thread_proc(void *data) {
	net_state_t *net = data;
	message_t msg;
	while (SDL_AtomicGet(&net->open)) {
		if (dequeue(&net->send_queue, &msg)) {
			char buffer[256];
			// serialize message
			int msg_len = sprintf(
				buffer,
				"MOVE %d %d TO %d %d",
				msg.move_piece.row, msg.move_piece.col,
				msg.move_target.row, msg.move_target.col
			);

			ssize_t wc = send(net->sock, buffer, msg_len + 1, 0);
			if (wc == SOCKET_ERROR) {
				log_sock_error("ERROR send socket");
				goto error;
			} else if (wc != msg_len + 1) {
				fprintf(stderr, "ERROR: failed to write all bytes\n");
				goto error;
			}
		} else {
			SDL_Delay(50); // TODO think in this
		}
	}
	// printf("DEBUG exited send_thread_proc\n");
	return 0;
error:
	SDL_AtomicSet(&net->open, 0);
	return 1;
}

extern net_state_t *net_connect(net_mode_t mode, char *host, char *port) {
	net_state_t *net = malloc(sizeof(net_state_t));
	if (!net) {
		perror("ERROR malloc");
		return 0;
	}
	memset(net, 0, sizeof(net_state_t));

#ifdef _WIN32
	WSADATA wsa_data;
	int wsa_res = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if (wsa_res) {
	    fprintf(stderr, "ERROR WSAStartup: %d\n", wsa_res);
	    goto error;
	}
#endif

	net->sock = INVALID_SOCKET;
	switch (mode) {
		case NET_SERVER:
			net->sock = start_server(port);
			break;
		case NET_CLIENT:
			net->sock = start_client(host, port);
			break;
	}
	if (net->sock == INVALID_SOCKET)
		goto error;

	SDL_AtomicSet(&net->open, 1);
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
	net_quit(net);
	return 0;
}

extern void net_quit(net_state_t *net) {
	int was_open = SDL_AtomicSet(&net->open, 0);
	if (was_open) {
		if (shutdown(net->sock, SHUT_WR))
			log_sock_error("shutdown");
	}

	int thread_res;
	if (net->recv_thread) {
		SDL_WaitThread(net->recv_thread, &thread_res);
		if (thread_res)
			fprintf(stderr, "receiver thread exit with error\n");
	}
	if (net->send_thread) {
		SDL_WaitThread(net->send_thread, &thread_res);
		if (thread_res)
			fprintf(stderr, "sender thread exit with error\n");
	}

	if (net->sock != INVALID_SOCKET) {
		if (closesocket(net->sock) == SOCKET_ERROR)
			log_sock_error("ERROR closesocket");
	}

#ifdef _WIN32
	WSACleanup();
#endif

	free(net);
}

extern bool net_is_open(net_state_t *net) {
	return SDL_AtomicGet(&net->open);
}

extern bool net_poll_message(net_state_t *net, message_t *msg) {
	return dequeue(&net->recv_queue, msg);
}

extern bool net_send_message(net_state_t *net, message_t *msg) {
	if (enqueue(&net->send_queue, msg)) {
		return true;
	} else {
		fprintf(stderr, "ERROR: send message queue is full\n");
		return false;
	}
}

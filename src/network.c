#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "network.h"

/*
 * TODO nonblocking connect and getaddrinfo
 */

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
typedef int ssize_t;
#define SHUT_WR SD_BOTH
#define sock_errno WSAGetLastError()
char *sock_error_str() {
	char *res;
	int flags = FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
	FormatMessageA(flags, 0, WSAGetLastError(), 0, &res, 0, 0);
	return res;
	// LocalFree(res); TODO when to destroy this?
}

#else

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
typedef int sock_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(sock) close(sock)
#define sock_errno errno
char *sock_error_str() {
	return strerror(errno);
}

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
	SDL_AtomicLock(&queue->lock);
	if (queue->count < QUEUE_SIZE) {
		queue->data[queue->last] = *item;
		queue->last = (queue->last + 1) % QUEUE_SIZE;
		queue->count++;
		result = true;
	}
	SDL_AtomicUnlock(&queue->lock);
	return result;
}

static bool dequeue(queue_t *queue, message_t *message) {
	bool result = false;
	SDL_AtomicLock(&queue->lock);
	if (queue->count) {
		*message = queue->data[queue->first];
		queue->first = (queue->first + 1) % QUEUE_SIZE;
		queue->count--;
		result = true;
	}
	SDL_AtomicUnlock(&queue->lock);
	return result;
}

struct _net_context {
	net_mode_t mode;
	char host[256];
	char port[6];
	SDL_atomic_t running;
	SDL_atomic_t state;
	SDL_Thread *thread;
	sock_t sock;
	queue_t recv_queue;
	queue_t send_queue;
	net_error_t error;
	char error_str[512];
};

static void set_error(net_context_t *net, net_error_t err, const char *fmt, ...) {
	// TODO cleanup existing error?
	net->error = err;
	va_list args;
	va_start(args, fmt);
	vsnprintf(net->error_str, sizeof(net->error_str), fmt, args);
	va_end(args);
}

static int connection_proc(void *data) {
	net_context_t *net = data;

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 1e3; // 1 ms

	if (net->mode == NET_SERVER) {
		sock_t server_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (server_sock == INVALID_SOCKET) {
			set_error(net, NET_EUNKNOWN, "create socket: %s", sock_error_str());
			goto server_cleanup;
		}
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(atoi(net->port));
		addr.sin_addr.s_addr = INADDR_ANY;
		if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
			net_error_t err = NET_EUNKNOWN;
			if (errno == EACCES) {
				err = NET_EPORTNOACCESS;
			} else if (errno == EADDRINUSE) {
				err = NET_EPORTINUSE;
			}
			set_error(net, err, "bind to port %s: %s", net->port, sock_error_str());
			goto server_cleanup;
		}
		if (listen(server_sock, 1) == SOCKET_ERROR) {
			set_error(net, NET_EUNKNOWN, "listen: %s", sock_error_str());
			goto server_cleanup;
		}

		for (;;) {
			fd_set read_fds;
			FD_ZERO(&read_fds);
			FD_SET(server_sock, &read_fds);
			if (select(FD_SETSIZE, &read_fds, 0, 0, &timeout) == SOCKET_ERROR) {
				set_error(net, NET_EUNKNOWN, "select server: %s", sock_error_str());
				goto server_cleanup;
			}
			if (!SDL_AtomicGet(&net->running)) {
				goto server_cleanup;
			} else if (FD_ISSET(server_sock, &read_fds)) {
				break;
			}
		}

		net->sock = accept(server_sock, 0, 0);
		if (net->sock == INVALID_SOCKET) {
			set_error(net, NET_EUNKNOWN, "accept: %s", sock_error_str());
			goto server_cleanup;
		}
	server_cleanup:
		if (server_sock != INVALID_SOCKET) {
			if (closesocket(server_sock) == SOCKET_ERROR) {
				if (!net->error)
					set_error(net, NET_EUNKNOWN, "closesocket server: %s", sock_error_str());
			}
		}
	} else {
		struct addrinfo hints = {0};
		hints.ai_flags = 0;
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		struct addrinfo *info = 0;
		int err = getaddrinfo(net->host, net->port, &hints, &info);
		if (err) {
			net_error_t net_err = (err == EAI_NONAME) ? NET_EDNSFAIL : NET_EUNKNOWN;
			set_error(net, net_err, "getaddrinfo: %s", gai_strerror(err));
			goto client_cleanup;
		}
		net->sock = socket(AF_INET, SOCK_STREAM, 0);
		if (net->sock == INVALID_SOCKET) {
			set_error(net, NET_EUNKNOWN, "socket: %s", sock_error_str());
			goto client_cleanup;
		}
		if (!SDL_AtomicGet(&net->running)) {
			goto client_cleanup;
		}
//        for (;;) {
//            fd_set write_fds;
//            FD_ZERO(&write_fds);
//            FD_SET(net->sock, &write_fds);
//            if (select(FD_SETSIZE, 0, &write_fds, 0, &timeout) == SOCKET_ERROR) {
//                set_error(net, NET_EUNKNOWN, "select server: %s", sock_error_str());
//                goto client_cleanup;
//            }
//            if (!SDL_AtomicGet(&net->running)) {
//                goto client_cleanup;
//            } else if (FD_ISSET(net->sock, &write_fds)) {
//                break;
//            }
//        }
		if (connect(net->sock, info->ai_addr, info->ai_addrlen) == SOCKET_ERROR) {
			net_error_t err = NET_EUNKNOWN;
			if (sock_errno == ECONNREFUSED)
				err = NET_ECONNREFUSED;
			set_error(net, err, "connect: %s", sock_error_str());
		}
	client_cleanup:
		if (info)
			freeaddrinfo(info);
	}

	if (net->error || net->sock == INVALID_SOCKET)
		goto exit;

	SDL_AtomicSet(&net->state, NET_RUNNING);
	while (SDL_AtomicGet(&net->running)) {
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(net->sock, &read_fds);

		fd_set write_fds;
		FD_ZERO(&write_fds);
		FD_SET(net->sock, &write_fds);

		if (select(FD_SETSIZE, &read_fds, &write_fds, 0, &timeout) == SOCKET_ERROR) {
			set_error(net, NET_EUNKNOWN, "select message loop: %s", sock_error_str());
			goto exit;
		}
		char buffer[256];
		if (FD_ISSET(net->sock, &read_fds)) {
			ssize_t rc = recv(net->sock, buffer, sizeof(buffer) - 1, 0);
			if (rc == SOCKET_ERROR) {
				set_error(net, NET_EUNKNOWN, "recv: %s", sock_error_str());
				goto exit;
			}
			if (rc == 0) {
				SDL_AtomicSet(&net->running, 0);
			}
			message_t msg = {0};
			if (buffer[rc - 1]) {
				set_error(net, NET_EUNKNOWN, "received message isn't null terminated");
				goto exit;
			}
			// parse message
			int status = sscanf(
				buffer,
				"MOVE %d %d TO %d %d",
				&msg.move_piece.row, &msg.move_piece.col,
				&msg.move_target.row, &msg.move_target.col
			);
			if (status != 4) {
				set_error(net, NET_EUNKNOWN, "failed to parse message");
				goto exit;
			}
			if (!enqueue(&net->recv_queue, &msg)) {
				set_error(net, NET_EUNKNOWN, "receive queue is full");
				goto exit;
			}
		}
		if (FD_ISSET(net->sock, &write_fds)) {
			message_t msg;
			if (dequeue(&net->send_queue, &msg)) {
				int msg_len = sprintf(
					buffer,
					"MOVE %d %d TO %d %d",
					msg.move_piece.row, msg.move_piece.col,
					msg.move_target.row, msg.move_target.col
				);
				ssize_t wc = send(net->sock, buffer, msg_len + 1, 0);
				if (wc == SOCKET_ERROR) {
					set_error(net, NET_EUNKNOWN, "send", sock_error_str());
					goto exit;
				} else if (wc != msg_len + 1) {
					set_error(net, NET_EUNKNOWN, "failed to write all bytes");
					goto exit;
				}
			}
		}
	}
exit:
	if (net->error) {
		SDL_AtomicSet(&net->state, NET_ERROR);
		return 1;
	} else {
		SDL_AtomicSet(&net->state, NET_CLOSED);
		return 0;
	}
}

extern net_context_t *net_init() {
#ifdef _WIN32
	WSADATA wsa_data;
	int wsa_res = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if (wsa_res) {
		fprintf(stderr, "ERROR WSAStartup: %d\n", wsa_res);
		return 0;
	}
#endif

	net_context_t *net = malloc(sizeof(net_context_t));
	if (!net) {
		perror("ERROR malloc");
		return 0;
	}

	memset(net, 0, sizeof(net_context_t));
	SDL_AtomicSet(&net->state, NET_CLOSED);
	net->sock = INVALID_SOCKET;

	return net;
}

extern void net_start(net_context_t *net, net_mode_t mode, char *host, char *port) {
	net->mode = mode;
	strncpy(net->host, host, sizeof(net->host));
	strncpy(net->port, port, sizeof(net->port));
	net->error = NET_ENONE;
	net->error_str[0] = '\0';

	SDL_AtomicSet(&net->running, 1);
	SDL_AtomicSet(&net->state, NET_CONNECTING);

	net->thread = SDL_CreateThread(connection_proc, "network", net);
	if (!net->thread) {
		SDL_AtomicSet(&net->state, NET_ERROR);
		set_error(net, NET_EUNKNOWN, "SDL_CreateThread: %s", SDL_GetError());
	}
}

extern void net_stop(net_context_t *net) {
	SDL_AtomicSet(&net->running, 0);

	int thread_res;
	if (net->thread) {
		SDL_WaitThread(net->thread, &thread_res);
		net->thread = 0;
		if (thread_res)
			fprintf(stderr, "receiver thread exit with error\n");
	}

	if (net->sock != INVALID_SOCKET) {
		if (closesocket(net->sock) == SOCKET_ERROR)
			set_error(net, NET_EUNKNOWN, "closesocket: %s", sock_error_str());
		net->sock = INVALID_SOCKET;
	}
}

extern void net_destroy(net_context_t *net) {
	if (SDL_AtomicGet(&net->state) == NET_RUNNING)
		net_stop(net);
	free(net);
#ifdef _WIN32
	WSACleanup();
#endif
}

extern net_state_t net_get_state(net_context_t *net) {
	return SDL_AtomicGet(&net->state);
}

extern net_error_t net_get_error(net_context_t *net) {
	return net->error;
}

extern const char *net_error_str(net_context_t *net) {
	return net->error_str;
}

extern bool net_poll_message(net_context_t *net, message_t *msg) {
	return dequeue(&net->recv_queue, msg);
}

extern bool net_send_message(net_context_t *net, message_t *msg) {
	if (enqueue(&net->send_queue, msg)) {
		return true;
	} else {
		set_error(net, NET_EUNKNOWN, "send message queue is full");
		return false;
	}
}

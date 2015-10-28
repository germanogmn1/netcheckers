#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

SDL_Thread *thread;

#define QUEUE_SIZE 256
typedef struct {
	char *data[QUEUE_SIZE];
	int first;
	int last;
} queue_t;

bool enqueue(queue_t *queue, char *el) {
	int r = (last + 1) % QUEUE_SIZE;
}

char *dequeue(queue_t *queue) {

}

static int server_thread(void *ptr) {
	int sockd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockd == -1) {
		perror("ERROR: create socket");
		return 1;
	}

	int port = 7568;
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sockd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		char buff[100];
		sprintf(buff, "ERROR: bind socket to port %d", port);
		perror(buff);
		return 1;
	}
	if (listen(sockd, 1) == -1) {
		perror("ERROR: listen");
		return 1;
	}
	int nsockd = accept(sockd, 0, 0);
	if (nsockd == -1) {
		perror("ERROR: accept");
		return 1;
	}

	printf("Accepted\n");

	char buffer[512];
	for (;;) {
		int rc = read(nsockd, buffer, sizeof(buffer) - 1);
		if (rc == -1) {
			perror("ERROR: read socket");
			return 1;
		}
		if (buffer[rc - 1]) {
			fprintf(stderr, "ERROR: received message isn't null terminated\n");
			return 1;
		}
		char *str = malloc(rc);
		strcpy(str, buffer);
		message_queue[message_count++] = str;
	}

	if (close(nsockd) == -1) {
		perror("ERROR: close socket from accept");
		return 1;
	}
	if (close(sockd) == -1) {
		perror("ERROR: close socket");
		return 1;
	}

	return 0;
}

int main(int argc, char **argv) {
	SDL_Thread *thread = SDL_CreateThread(server_thread, "server", 0);
	if (!thread) {
		fprintf(stderr, "SDL_CreateThread: %s\n", SDL_GetError());
	}

	for (;;) {
		if (message_count > 0) {
			printf("Got: '%s'\n", message_queue[message_count--]);
		} else if (message_count < 0) {
			fprintf(stderr, "message_count: %d\n", message_count);
		}
	}

	int thread_return;
	SDL_WaitThread(thread, &thread_return);
}

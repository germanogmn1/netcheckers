#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char **argv) {
	struct addrinfo hints = {};
	hints.ai_flags = 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	struct addrinfo *info;
	int err = getaddrinfo("localhost", "7568", &hints, &info);
	if (err) {
		fprintf(stderr, "ERROR: getaddrinfo: %s\n", gai_strerror(err));
		return 1;
	}

	int sockd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockd == -1) {
		perror("ERROR: create socket");
		return 1;
	}
	if (connect(sockd, info->ai_addr, info->ai_addrlen) == -1) {
		perror("ERROR: connect");
		return 1;
	}

	printf("Connected\n");
	int wc = write(sockd, "Hey! What's up", 15);
	if (wc == -1) {
		perror("ERROR: write socket");
		return 1;
	} else if (wc != 15) {
		fprintf(stderr, "ERROR: write failed to write all bytes\n");
		return 1;
	}

	char buffer[256];
	int rc = read(sockd, buffer, 255);
	if (rc == -1) {
		perror("ERROR: read socket");
		return 1;
	}
	buffer[rc] = '\0';
	printf("Got: %s\n", buffer);

	freeaddrinfo(info);

	if (close(sockd) == -1) {
		perror("ERROR: close socket");
		return 1;
	}
}

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main(int argc, char **argv) {
	int sockd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockd == -1) {
		perror("ERROR: create socket");
		return 1;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = 7568;
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sockd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		char buff[100];
		sprintf(buff, "ERROR: bind socket to port %d", addr.sin_port);
		perror(buff);
		return 1;
	}
	if (listen(sockd, 5) == -1) {
		perror("ERROR: listen");
		return 1;
	}
	int nsockd = accept(sockd, 0, 0);
	if (nsockd == -1) {
		perror("ERROR: accept");
		return 1;
	}

	if (close(nsockd) == -1) {
		perror("ERROR: close socket from accept");
		return 1;
	}
	if (close(sockd) == -1) {
		perror("ERROR: close socket");
		return 1;
	}
}

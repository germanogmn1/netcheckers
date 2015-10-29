#include <stdio.h>
#include <string.h>
#include "network.h"

int main(int argc, char **argv) {
	net_mode_t net_mode;
	char *host;
	char *port;
	if (argc == 3 && strcmp(argv[1], "server") == 0) {
		net_mode = NET_SERVER;
		host = 0;
		port = argv[2];
	} else if (argc == 4 && strcmp(argv[1], "client") == 0) {
		net_mode = NET_CLIENT;
		host = argv[2];
		port = argv[3];
	} else {
		fprintf(stderr,
			"Usage:\n"
			"    %s server PORT\n"
			"    %s client HOST PORT\n",
			argv[0], argv[0]
		);
		return 1;
	}

	net_state_t *net = net_start(net_mode, host, port);
	if (!net)
		return 1;

	bool send = (net_mode == NET_CLIENT);
	bool open = true;
	while (open) {
		message_t msg;
		if (send) {
			send = false;

			printf("> ");
			fgets(msg.str, 255, stdin);
			msg.str[strlen(msg.str) - 1] = 0; // remove newline
			if (strcmp(msg.str, "!close") == 0) {
				msg.type = MSG_CLOSE;
				open = false;
			} else {
				msg.type = MSG_STR;
			}
			net_send_message(net, &msg);
		} else {
			send = true;

			while (!net_poll_message(net, &msg)) {
			}
			if (msg.type == MSG_CLOSE) {
				printf("Connection closed by %s\n", (net_mode == NET_SERVER) ? "client" : "server");
				open = false;
			} else if (msg.type == MSG_STR) {
				printf("remote: %s\n", msg.str);
			}
		}
	}

	net_quit(net);
}

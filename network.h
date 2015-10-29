#include <stdbool.h>

typedef struct _net_state net_state_t;

typedef enum {
	MSG_CLOSE,
	MSG_STR,
} message_type_t;

typedef struct {
	message_type_t type;
	char str[256];
} message_t;

typedef enum { NET_SERVER, NET_CLIENT } net_mode_t;

net_state_t *net_start(net_mode_t mode, char *host, char *port);
void net_quit(net_state_t *net);
bool net_poll_message(net_state_t *net, message_t *msg);
bool net_send_message(net_state_t *net, message_t *msg);

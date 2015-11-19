#pragma once
#include <stdbool.h>
#include "common.h"

typedef struct {
	cell_pos_t move_piece;
	cell_pos_t move_target;
} message_t;

typedef struct _net_context net_context_t;

typedef enum { NET_SERVER, NET_CLIENT } net_mode_t;

typedef enum {
	NET_ERROR = -1,
	NET_CLOSED,
	NET_CONNECTING,
	NET_RUNNING,
} net_state_t;

typedef enum {
	NET_ENONE,
	NET_EUNKNOWN,
	NET_EPORTNOACCESS,
	NET_EPORTINUSE,
	NET_ECONNREFUSED,
	NET_EDNSFAIL,
} net_error_t;

net_error_t net_get_error(net_context_t *net);
const char *net_error_str(net_context_t *net);

net_context_t *net_init();
void net_destroy(net_context_t *net);

void net_start(net_context_t *net, net_mode_t mode, char *host, char *port);
void net_stop(net_context_t *net);

net_state_t net_get_state(net_context_t *net);
bool net_poll_message(net_context_t *net, message_t *msg);
bool net_send_message(net_context_t *net, message_t *msg);

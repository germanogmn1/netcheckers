#include "network.h"

typedef struct {
	bool success;
	net_context_t *network;
	net_mode_t net_mode;
	char host[1024];
	char port[6];
	char assets_path[1024];
} startup_info_t;

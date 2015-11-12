#ifdef __cplusplus
extern "C" {
#endif

#include "network.h"

typedef struct {
	bool success;
	net_context_t *network;
	net_mode_t net_mode;
	char host[1024];
	char port[6];
	char assets_path[1024];
} startup_info_t;

startup_info_t startup(int argc, char **argv);

#ifdef __cplusplus
} // extern "C"
#endif

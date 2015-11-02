#include <limits.h>

typedef struct {
    bool success;
    bool server_mode;
    char host[1024];
    char assets_path[1024];
    char port[6];
} startup_info_t;

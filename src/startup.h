#include <limits.h>

typedef struct {
    bool success;
    bool server_mode;
    char host[_POSIX_HOST_NAME_MAX];
    char port[10];
    char assets_path[PATH_MAX];
} startup_info_t;

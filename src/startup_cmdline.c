#include "startup.h"

static startup_info_t startup(int argc, char **argv) {
	startup_info_t result = {0};
	if (argc == 3 && strcmp(argv[1], "server") == 0) {
		result.success = true;
		result.server_mode = true;
		strncpy(result.port, argv[2], sizeof(result.port));
	} else if (argc == 4 && strcmp(argv[1], "client") == 0) {
		result.success = true;
		result.server_mode = false;
		strncpy(result.host, argv[2], sizeof(result.host));
		strncpy(result.port, argv[3], sizeof(result.port));
	} else {
		result.success = false;
		fprintf(stderr,
			"Usage:\n"
			"    %s server PORT\n"
			"    %s client HOST PORT\n",
			argv[0], argv[0]
		);
	}
	strcpy(result.assets_path, "assets");
	return result;
}

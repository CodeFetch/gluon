#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>
#include <libgluonutil.h>
#include <libbabelhelper/babelhelper.h>
#include "handle_neighbour.h"

static bool process_line_neighbours(char *lineptr, void *obj){
	if (!strncmp(lineptr, "ok", 2) ) {
		return false;
	}

	if (!strncmp(lineptr, "add neighbour", 13)) {
		handle_neighbour(lineptr, (struct json_object*)obj);
	}
	return true;
}

int main(void) {
	struct json_object *neighbours;

	printf("Content-type: text/event-stream\n\n");
	fflush(stdout);

	while (1) {
		neighbours = json_object_new_object();
		if (!neighbours)
			continue;

		struct babelhelper_ctx bhelper_ctx = {};
		bhelper_ctx.debug = false;
		babelhelper_readbabeldata(&bhelper_ctx, (void*)neighbours, process_line_neighbours);

		printf("data: %s\n\n", json_object_to_json_string(neighbours));
		fflush(stdout);
		json_object_put(neighbours);
		neighbours = NULL;
		sleep(10);
	}

	return 0;
}

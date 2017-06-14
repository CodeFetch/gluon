#include <unistd.h>

#include <libubox/blobmsg_json.h>
#include "libubus.h"
#include <stdbool.h>
#include <stdio.h>

#define IFNAMELEN 32
#define PROTOLEN 32
#define UBUS_TIMEOUT 30

static void blobmsg_handle_list(struct blob_attr *attr, int len, bool array);

static void blobmsg_handle_element(struct blob_attr *attr, bool head, char *ifname, char *proto) {
	void *data;

	if (!blobmsg_check_attr(attr, false))
		return;

	data = blobmsg_data(attr);

	switch (blob_id(attr)) {
		case  BLOBMSG_TYPE_STRING:
			if (!strncmp(blobmsg_name(attr),"device", 6)) {
				snprintf(ifname, IFNAMELEN, "%s", data);
			} else if (!strncmp(blobmsg_name(attr), "proto", 5)) {
				snprintf(proto, PROTOLEN, "%s", data);
			}
			return;
		case BLOBMSG_TYPE_ARRAY:
			blobmsg_handle_list(data, blobmsg_data_len(attr), true);
			return;
		case BLOBMSG_TYPE_TABLE:
			blobmsg_handle_list(data, blobmsg_data_len(attr), false);
	}
}

static void blobmsg_handle_list(struct blob_attr *attr, int len, bool array)
{
	struct blob_attr *pos;
	int rem = len;

	char *ifname = malloc(IFNAMELEN);
	char *proto = malloc(PROTOLEN);
	ifname[0]=0;
	proto[0]=0;

	__blob_for_each_attr(pos, attr, rem) {
		blobmsg_handle_element(pos, array, ifname, proto);
	}

	if (strlen(ifname) && strlen(proto)) {
		printf("EIN INTERFACE IST FERTIG AUSGELESEN: %s(%s)\n", ifname, proto);
		if (!strncmp(proto, "gluon_mesh", 10))
			printf("-- WIR HABEN EINEN GEWINNER: %s(%s)\n",ifname, proto);
	}

	free(ifname);
	free(proto);
}

static void receive_call_result_data(struct ubus_request *req, int type, struct blob_attr *msg)
{
	if (!msg) {
		printf("empty message\n");
		return;
	}

	blobmsg_handle_list(blobmsg_data(msg), blobmsg_data_len(msg), false);
}

int main(int argc, char **argv)
{
	static struct ubus_context *ubus_ctx;
	char *ubus_socket[20];
	int ret = 0;
	unsigned int id=8;
	static struct blob_buf b;

	strncpy(ubus_socket, "/var/run/ubus.sock", 19);
	ubus_ctx = ubus_connect(ubus_socket);
	if (!ubus_ctx) {
		return -1;
	}

	ret = -2;
	blob_buf_init(&b, 0);
	ubus_lookup_id(ubus_ctx, "network.interface", &id);
	ret = ubus_invoke(ubus_ctx, id, "dump", b.head, receive_call_result_data, NULL, UBUS_TIMEOUT * 1000);

	if (ret > 0)
		fprintf(stderr, "Command failed: %s\n", ubus_strerror(ret));
	else if (ret == -2)
		fprintf(stderr, "invalid call, exiting\n");
	ubus_free(ubus_ctx);
	return ret;
}


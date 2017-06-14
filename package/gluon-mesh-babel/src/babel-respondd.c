/*
   Copyright (c) 2016, Matthias Schiffer <mschiffer@universe-factory.net>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
   */


#include <respondd.h>

#include <iwinfo.h>
#include <json-c/json.h>
#include <libgluonutil.h>
#include <uci.h>

#include <alloca.h>
#include <glob.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <linux/ethtool.h>
#include <linux/if_addr.h>
#include <linux/sockios.h>

#include <netdb.h>
#include "errno.h"
#include <libbabelhelper/babelhelper.h>

#include <libubox/blobmsg_json.h>
#include "libubus.h"

#define _STRINGIFY(s) #s
#define STRINGIFY(s) _STRINGIFY(s)
#include <stdlib.h>

#define SOCKET_INPUT_BUFFER_SIZE 255
#define BABEL_PORT 33123
#define VPN_INTERFACE "mesh-vpn"
#define l3rdctl "/var/run/l3roamd.sock"
#include "handle_neighbour.h"

#define IFNAMELEN 32
#define PROTOLEN 32

#define UBUS_TIMEOUT 30
#define UBUS_SOCKET "/var/run/ubus.sock"

static struct babelhelper_ctx bhelper_ctx = {};

static char*  get_line_from_run(const char* command) {
	FILE *fp;
	char *line = NULL;
	size_t len = 0;

	fp = popen(command, "r");

	if (fp != NULL) {
		ssize_t r = getline(&line, &len, fp);
		if (r >= 0) {
			len = strlen(line);

			if (len && line[len-1] == '\n')
				line[len-1] = 0;
		}
		else {
			free(line);
			line = NULL;
		}

		pclose(fp);
	}
	return line;
}

static struct json_object * get_addresses(void) {
	char *primarymac = gluonutil_get_sysconfig("primary_mac");
	char *address = malloc(INET6_ADDRSTRLEN+1);
	char node_prefix_str[INET6_ADDRSTRLEN+1];
	struct in6_addr node_prefix = {};
	struct json_object *retval = json_object_new_array();

	if (!gluonutil_get_node_prefix6(&node_prefix)) {
		fprintf(stderr, "get_addresses: could not obtain mesh-prefix from site.conf. Not adding addresses to json data\n");
		goto free;
	}

	if (inet_ntop(AF_INET6, &(node_prefix.s6_addr), node_prefix_str, INET6_ADDRSTRLEN) == NULL) {
		fprintf(stderr, "get_addresses: could not convert mesh-prefix from site.conf to string\n");
		goto free;
	}

	char *prefix_addresspart = strndup(node_prefix_str, INET6_ADDRSTRLEN);
	if (! babelhelper_generateip_str(address, primarymac, prefix_addresspart) ) {
		fprintf(stderr, "IP-address could not be generated by babelhelper");
	}
	free(prefix_addresspart);

	json_object_array_add(retval, json_object_new_string(address));

free:
	free(address);
	free(primarymac);

	return retval;
}

static bool interface_file_exists(const char *ifname, const char *name) {
	const char *format = "/sys/class/net/%s/%s";
	char path[strlen(format) + strlen(ifname) + strlen(name)+1];
	snprintf(path, sizeof(path), format, ifname, name);

	return !access(path, F_OK);
}

static void mesh_add_if(const char *ifname, struct json_object *wireless,
		struct json_object *tunnel, struct json_object *other) {
	struct json_object *address = gluonutil_wrap_and_free_string(gluonutil_get_interface_address(ifname));

	if (interface_file_exists(ifname, "wireless"))
		json_object_array_add(wireless, address);
	else if (interface_file_exists(ifname, "tun_flags"))
		json_object_array_add(tunnel, address);
	else
		json_object_array_add(other, address);

}

static bool process_line_neighbours(char *lineptr, void *obj){
	if (!strncmp(lineptr, "ok", 2)) {
		return false;
	}

	if (!strncmp(lineptr, "add neighbour", 13)) {
		handle_neighbour(lineptr, (struct json_object*)obj);
	}
	return true;
}

static struct json_object * get_babel_neighbours(void) {

	struct json_object *neighbours;
	neighbours  = json_object_new_object();
	if (!neighbours)
		return NULL;

	babelhelper_readbabeldata(&bhelper_ctx, (void*)neighbours, process_line_neighbours);

	return(neighbours);
}

static void blobmsg_handle_list(struct blob_attr *attr, int len, bool array, struct json_object *wireless, struct json_object *tunnel, struct json_object *other);

static void blobmsg_handle_element(struct blob_attr *attr, bool head, char **ifname, char **proto, struct json_object *wireless, struct json_object *tunnel, struct json_object *other) {
	void *data;

	if (!blobmsg_check_attr(attr, false))
		return;

	data = blobmsg_data(attr);

	switch (blob_id(attr)) {
		case  BLOBMSG_TYPE_STRING:
			if (!strncmp(blobmsg_name(attr),"device", 6)) {
				free(*ifname);
				*ifname = strndup(data, IFNAMELEN);
			} else if (!strncmp(blobmsg_name(attr), "proto", 5)) {
				free(*proto);
				*proto = strndup(data, PROTOLEN);
			}
			return;
		case BLOBMSG_TYPE_ARRAY:
			blobmsg_handle_list(data, blobmsg_data_len(attr), true, wireless, tunnel, other);
			return;
		case BLOBMSG_TYPE_TABLE:
			blobmsg_handle_list(data, blobmsg_data_len(attr), false, wireless, tunnel, other);
	}
}

static void blobmsg_handle_list(struct blob_attr *attr, int len, bool array, struct json_object *wireless, struct json_object *tunnel, struct json_object *other) {
	struct blob_attr *pos;
	int rem = len;

	char *ifname = NULL;
	char *proto = NULL;

	__blob_for_each_attr(pos, attr, rem) {
		blobmsg_handle_element(pos, array, &ifname, &proto, wireless, tunnel, other);
	}

	if (ifname && proto) {
		if (!strncmp(proto, "gluon_mesh", 10)) {
			printf("mesh-interface interface found: %s(%s)\n",ifname, proto);
			mesh_add_if(ifname, wireless, tunnel, other);
		}
	}
	free(ifname);
	free(proto);
}

static void receive_call_result_data(struct ubus_request *req, int type, struct blob_attr *msg) {
	struct json_object *ret = json_object_new_object();
	struct json_object *wireless = json_object_new_array();
	struct json_object *tunnel = json_object_new_array();
	struct json_object *other = json_object_new_array();

	if (!ret || !wireless || !tunnel || !other) {
		json_object_put(wireless);
		json_object_put(tunnel);
		json_object_put(other);
		json_object_put(ret);
		return;
	}

	if (!msg) {
		printf("empty message\n");
		return;
	}

	blobmsg_handle_list(blobmsg_data(msg), blobmsg_data_len(msg), false, wireless, tunnel, other);

	json_object_object_add(ret, "wireless", wireless);
	json_object_object_add(ret, "tunnel", tunnel);
	json_object_object_add(ret, "other", other);

	*((struct json_object**)(req->priv)) = ret;
}


static struct json_object * get_mesh_ifs() {
	struct ubus_context *ubus_ctx;
	struct json_object *ret = NULL;
	struct blob_buf b = {};

	unsigned int id=8;

	ubus_ctx = ubus_connect(UBUS_SOCKET);
	if (!ubus_ctx) {
		fprintf(stderr,"could not connect to ubus, not providing mesh-data\n");
		goto end;
	}

	int uret = -2;
	blob_buf_init(&b, 0);
	ubus_lookup_id(ubus_ctx, "network.interface", &id);
	uret = ubus_invoke(ubus_ctx, id, "dump", b.head, receive_call_result_data, &ret, UBUS_TIMEOUT * 1000);

	if (uret > 0)
		fprintf(stderr, "ubus command failed: %s\n", ubus_strerror(uret));
	else if (uret == -2)
		fprintf(stderr, "invalid call, exiting\n");

	blob_buf_free(&b);

end:
	ubus_free(ubus_ctx);
	return ret;
}

static struct json_object * get_mesh(void) {
	struct json_object *ret = json_object_new_object();
	struct json_object *interfaces = NULL;
	interfaces = json_object_new_object();
	json_object_object_add(interfaces, "interfaces", get_mesh_ifs());
	json_object_object_add(ret, "babel", interfaces);
	return ret;
}

static struct json_object * get_babeld_version(void) {
	char *version = get_line_from_run("exec babeld -V 2>&1");
	struct json_object *ret = gluonutil_wrap_string(version);
	free(version);
	return ret;
}

static struct json_object * respondd_provider_nodeinfo(void) {
	bhelper_ctx.debug=false;
	struct json_object *ret = json_object_new_object();

	struct json_object *network = json_object_new_object();
	json_object_object_add(network, "addresses", get_addresses());
	json_object_object_add(network, "mesh", get_mesh());
	json_object_object_add(ret, "network", network);

	struct json_object *software = json_object_new_object();
	struct json_object *software_babeld = json_object_new_object();
	json_object_object_add(software_babeld, "version", get_babeld_version());
	json_object_object_add(software, "babeld", software_babeld);
	json_object_object_add(ret, "software", software);

	return ret;
}

static uint64_t getnumber(const char *ifname, const char *stat) {
	const char *format = "/sys/class/net/%s/statistics/%s";
	char path[strlen(format) + strlen(ifname) + strlen(stat)];
	snprintf(path, sizeof(path), format, ifname, stat);
	if (! access(path, F_OK))
	{
		char *line=gluonutil_read_line(path);
		long long i = atoll(line);
		free(line);
		return(i);
	}
	return 0;
}

static struct json_object * get_traffic(void) {
	char ifname[16];

	strncpy(ifname, "local-node", 16);

	struct json_object *ret = NULL;
	struct json_object *rx = json_object_new_object();
	struct json_object *tx = json_object_new_object();

	json_object_object_add(rx, "packets", json_object_new_int64(getnumber(ifname, "rx_packets")));
	json_object_object_add(rx, "bytes", json_object_new_int64(getnumber(ifname, "rx_bytes")));
	json_object_object_add(rx, "dropped", json_object_new_int64(getnumber(ifname, "tx_dropped")));
	json_object_object_add(tx, "packets", json_object_new_int64(getnumber(ifname, "tx_packets")));
	json_object_object_add(tx, "dropped", json_object_new_int64(getnumber(ifname, "tx_dropped")));
	json_object_object_add(tx, "bytes", json_object_new_int64(getnumber(ifname, "tx_bytes")));

	ret = json_object_new_object();
	json_object_object_add(ret, "rx", rx);
	json_object_object_add(ret, "tx", tx);

	return ret;
}

static void count_iface_stations(size_t *wifi24, size_t *wifi5, const char *ifname) {
	const struct iwinfo_ops *iw = iwinfo_backend(ifname);
	if (!iw)
		return;

	int freq;
	if (iw->frequency(ifname, &freq) < 0)
		return;

	size_t *wifi;
	if (freq >= 2400 && freq < 2500)
		wifi = wifi24;
	else if (freq >= 5000 && freq < 6000)
		wifi = wifi5;
	else
		return;

	int len;
	char buf[IWINFO_BUFSIZE];
	if (iw->assoclist(ifname, buf, &len) < 0)
		return;

	struct iwinfo_assoclist_entry *entry;
	for (entry = (struct iwinfo_assoclist_entry *)buf; (char*)(entry+1) <= buf + len; entry++)
		(*wifi)++;
}

static void count_stations(size_t *wifi24, size_t *wifi5) {
	struct uci_context *ctx = uci_alloc_context();
	ctx->flags &= ~UCI_FLAG_STRICT;


	struct uci_package *p;
	if (uci_load(ctx, "wireless", &p))
		goto end;


	struct uci_element *e;
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		if (strcmp(s->type, "wifi-iface"))
			continue;

		const char *network = uci_lookup_option_string(ctx, s, "network");
		if (!network || strcmp(network, "client"))
			continue;

		const char *mode = uci_lookup_option_string(ctx, s, "mode");
		if (!mode || strcmp(mode, "ap"))
			continue;

		const char *ifname = uci_lookup_option_string(ctx, s, "ifname");
		if (!ifname)
			continue;

		count_iface_stations(wifi24, wifi5, ifname);
	}

end:
	uci_free_context(ctx);
}

static void handle_route_addgw_nexthop(struct json_object *obj, char *line) {
	struct babelroute br = {};
	if (babelhelper_get_route(&br, line)) {
		if ( (! strncmp(br.prefix, "::/0", 4) ) && ( ! strncmp(br.from, "::/0", 4) ) ) {
			int gw_nexthoplen=strlen(br.via) + strlen(br.ifname)+2;
			char gw_nexthop[gw_nexthoplen];
			snprintf(gw_nexthop, gw_nexthoplen , "%s%%%s", br.via, br.ifname);
			json_object_object_add(obj, "gateway_nexthop", json_object_new_string(gw_nexthop));
		}
	}
	babelhelper_babelroute_free_members(&br);
}

static bool process_line_addgw(char *lineptr, void *obj){
	if (!strncmp(lineptr, "ok", 2)) {
		return false;
	}

	if  (!strncmp(lineptr, "add route", 9)) {
		handle_route_addgw_nexthop((struct json_object*)obj,lineptr);
	}
	return true;
}

static int json_parse_get_clients(json_object * object) {
	json_object_object_foreach(object, key, val) {
		if (! strcmp("clients", key))
		{
			return(json_object_get_int(val));
		}
	}
	return(-1);
}

static int ask_l3roamd_for_client_count() {
	struct sockaddr_un addr;
	const char *socket_path = "/var/run/l3roamd.sock";
	int fd;
	int clients = -1;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "could not setup l3roamd-control-socket\n");
		return(-1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		fprintf(stderr, "connect error\n");
		return(-1);
	}

	if (write(fd,"get_clients\n",12) != 12) {
		perror("could not send command to l3roamd socket: get_clients");
		goto end;
	}
	char buf[SOCKET_INPUT_BUFFER_SIZE];
	memset(buf, '\0', SOCKET_INPUT_BUFFER_SIZE);
	if (read(fd, buf, SOCKET_INPUT_BUFFER_SIZE) < 0 ) {
		perror("error on read in ask_l3roamd_for_client_count():");
	}

	json_object * jobj = json_tokener_parse(buf);
	clients = json_parse_get_clients(jobj);
	json_object_put(jobj);

end:
	close(fd);

	return clients;
}

static struct json_object * get_clients(void) {
	size_t wifi24 = 0, wifi5 = 0;

	count_stations(&wifi24, &wifi5);

	size_t total = ask_l3roamd_for_client_count();

	size_t wifi = wifi24 + wifi5;
	struct json_object *ret = json_object_new_object();

	if (total >= 0)
		json_object_object_add(ret, "total", json_object_new_int(total));

	json_object_object_add(ret, "wifi", json_object_new_int(wifi));
	json_object_object_add(ret, "wifi24", json_object_new_int(wifi24));
	json_object_object_add(ret, "wifi5", json_object_new_int(wifi5));
	return ret;
}

static struct json_object * respondd_provider_statistics(void) {
	struct json_object *ret = json_object_new_object();

	json_object_object_add(ret, "clients", get_clients());
	json_object_object_add(ret, "traffic", get_traffic());

	babelhelper_readbabeldata(&bhelper_ctx, (void*)ret, process_line_addgw);

	return ret;
}

static struct json_object * get_wifi_neighbours(const char *ifname) {
	const struct iwinfo_ops *iw = iwinfo_backend(ifname);
	if (!iw)
		return NULL;

	int len;
	char buf[IWINFO_BUFSIZE];
	if (iw->assoclist(ifname, buf, &len) < 0)
		return NULL;

	struct json_object *neighbours = json_object_new_object();

	struct iwinfo_assoclist_entry *entry;
	for (entry = (struct iwinfo_assoclist_entry *)buf; (char*)(entry+1) <= buf + len; entry++) {
		struct json_object *obj = json_object_new_object();

		json_object_object_add(obj, "signal", json_object_new_int(entry->signal));
		json_object_object_add(obj, "noise", json_object_new_int(entry->noise));
		json_object_object_add(obj, "inactive", json_object_new_int(entry->inactive));

		char mac[18];
		snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
				entry->mac[0], entry->mac[1], entry->mac[2],
				entry->mac[3], entry->mac[4], entry->mac[5]);

		json_object_object_add(neighbours, mac, obj);
	}

	struct json_object *ret = json_object_new_object();

	if (json_object_object_length(neighbours))
		json_object_object_add(ret, "neighbours", neighbours);
	else
		json_object_put(neighbours);

	return ret;
}

static struct json_object * get_wifi(void) {

	struct uci_context *ctx = uci_alloc_context();
	ctx->flags &= ~UCI_FLAG_STRICT;

	struct json_object *ret = json_object_new_object();

	struct uci_package *p;
	if (uci_load(ctx, "network", &p))
		goto end;


	struct uci_element *e;
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		if (strcmp(s->type, "interface"))
			continue;

		const char *proto = uci_lookup_option_string(ctx, s, "proto");
		if (!proto || strcmp(proto, "gluon_mesh"))
			continue;

		const char *ifname = uci_lookup_option_string(ctx, s, "ifname");
		if (!ifname)
			continue;

		char *ifaddr = gluonutil_get_interface_address(ifname);
		if (!ifaddr)
			continue;

		struct json_object *neighbours = get_wifi_neighbours(ifname);
		if (neighbours)
			json_object_object_add(ret, ifaddr, neighbours);

		free(ifaddr);
	}

end:
	uci_free_context(ctx);
	return ret;
}

static struct json_object * respondd_provider_neighbours(void) {
	struct json_object *ret = json_object_new_object();

	struct json_object *babel = get_babel_neighbours();
	if (babel)
		json_object_object_add(ret, "babel", babel);


	struct json_object *wifi = get_wifi();
	if (wifi)
		json_object_object_add(ret, "wifi", wifi);

	return ret;
}


const struct respondd_provider_info respondd_providers[] = {
	{"nodeinfo", respondd_provider_nodeinfo},
	{"statistics", respondd_provider_statistics},
	{"neighbours", respondd_provider_neighbours},
	{}
};

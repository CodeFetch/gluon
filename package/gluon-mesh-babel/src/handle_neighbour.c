#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <libgluonutil.h>
#include "handle_neighbour.h"
#include <libbabelhelper/babelhelper.h>

int obtain_ifmac(char *ifmac, char *ifname) {
	struct ifreq ifr = {};
	int sock;

	sock=socket(PF_INET, SOCK_STREAM, 0);
	if (-1==sock) {
		perror("socket() ");
		return 1;
	}

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)-1);

	if (-1==ioctl(sock, SIOCGIFHWADDR, &ifr)) {
		perror("ioctl(SIOCGIFHWADDR) ");
		return 1;
	}
	close(sock);

	unsigned char mac[6];
	memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
	sprintf(ifmac, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return 0;
}

void handle_neighbour(char *line, struct json_object *obj) {
	struct babelneighbour bn = {};

	if (babelhelper_get_neighbour(&bn, line) ) {
		struct json_object *neigh = json_object_new_object();

		json_object_object_add(neigh, "rxcost", json_object_new_int(bn.rxcost));
		json_object_object_add(neigh, "txcost", json_object_new_int(bn.txcost));
		json_object_object_add(neigh, "cost", json_object_new_int(bn.cost));
		json_object_object_add(neigh, "reachability", json_object_new_double(bn.reach));
		json_object_object_add(neigh, "ifname", json_object_new_string(bn.ifname));

		json_object_object_add(obj, bn.address_str , neigh);
	}

	babelhelper_babelneighbour_free_members(&bn);
}



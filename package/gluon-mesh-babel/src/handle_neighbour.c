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
		char mac[18] = {};
		struct json_object *neigh = json_object_new_object();

		if (babelhelper_ll_to_mac(mac, bn.address_str)) {
			struct in6_addr prefix = {};

			if (!gluonutil_get_node_prefix6(&prefix)) {
				fprintf(stderr, "Could not obtain node-prefix from site.conf. Exiting handle_neighbour.\n");
				json_object_put(neigh);
				goto cleanup;
			}

			json_object_object_add(neigh, "protocol", json_object_new_string("babel"));
			json_object_object_add(neigh, "rxcost", json_object_new_int(bn.rxcost));
			json_object_object_add(neigh, "txcost", json_object_new_int(bn.txcost));
			json_object_object_add(neigh, "cost", json_object_new_int(bn.cost));
			json_object_object_add(neigh, "reachability", json_object_new_double(bn.reach));
			json_object_object_add(neigh, "address-ll", json_object_new_string(bn.address_str));

			char meshaddress[INET6_ADDRSTRLEN+1] = {};
			char prefix_str[INET6_ADDRSTRLEN +1] = {};
			inet_ntop(AF_INET6, &(prefix.s6_addr), prefix_str, INET6_ADDRSTRLEN);
			babelhelper_generateip_str(meshaddress, mac, prefix_str);
			json_object_object_add(neigh, "address-mesh", json_object_new_string(meshaddress));

			struct json_object *nif = 0;
			char ifmac[18] = {};

			obtain_ifmac(ifmac, bn.ifname);

			if (!json_object_object_get_ex(obj, ifmac, &nif)) {
				nif = json_object_new_object();
				json_object_object_add(nif, "ifname", json_object_new_string(bn.ifname));
				json_object_object_add(obj, ifmac, nif);
			}

			struct json_object *neighborcollector = 0;
			if (!json_object_object_get_ex(nif, "neighbours", &neighborcollector)) {
				neighborcollector = json_object_new_object();
				json_object_object_add(nif, "neighbours", neighborcollector);
			}

			json_object_object_add(neighborcollector, mac, neigh);
		}
		else {
			json_object_put(neigh);
		}
	}

cleanup:
	babelhelper_babelneighbour_free_members(&bn);
}



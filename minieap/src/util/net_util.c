#include "net_util.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RESULT obtain_iface_mac(const char *name, uint8_t *out) {
    struct ifaddrs *all;
    if (getifaddrs(&all)) return FAILURE;
    int found = 0;
    for (struct ifaddrs *p = all; p; p = p->ifa_next) {
        if (!p->ifa_addr || strcmp(p->ifa_name, name) || p->ifa_addr->sa_family != AF_PACKET) continue;
        struct sockaddr_ll *addr = (struct sockaddr_ll *)p->ifa_addr;
        if (addr->sll_halen != 6) continue;
        memcpy(out, addr->sll_addr, 6);
        found = 1;
        break;
    }
    freeifaddrs(all);
    return found ? SUCCESS : FAILURE;
}
RESULT obtain_iface_ip_mask(const char *name, LIST_ELEMENT **list) {
    struct ifaddrs *all;
    if (getifaddrs(&all)) return FAILURE;
    for (struct ifaddrs *p = all; p; p = p->ifa_next) {
        if (!p->ifa_addr || strcmp(p->ifa_name, name)) continue;
        int family = p->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;
        IP_ADDR *addr = calloc(1, sizeof(*addr));
        if (!addr) { freeifaddrs(all); free_ip_list(list); return FAILURE; }
        addr->family = family;
        if (family == AF_INET6) memcpy(addr->ip, &((struct sockaddr_in6 *)p->ifa_addr)->sin6_addr, 16);
        else memcpy(addr->ip, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, 4);
        insert_data(list, addr);
    }
    freeifaddrs(all);
    return SUCCESS;
}
void free_ip_list(LIST_ELEMENT **list) { list_destroy(list, TRUE); }
RESULT obtain_dns_list(LIST_ELEMENT **list) {
    FILE *fp = fopen("/etc/resolv.conf", "r");
    if (!fp) return FAILURE;
    char line[256], key[32], value[46];
    while (fgets(line, sizeof(line), fp)) {
        uint8_t addr[16];
        if (sscanf(line, "%31s %45s", key, value) != 2 || strcmp(key, "nameserver")) continue;
        if (inet_pton(AF_INET, value, addr) != 1 && inet_pton(AF_INET6, value, addr) != 1) continue;
        char *copy = strdup(value);
        if (!copy) { fclose(fp); list_destroy(list, TRUE); return FAILURE; }
        insert_data(list, copy);
        if ((*list)->next) break;
    }
    fclose(fp);
    return *list ? SUCCESS : FAILURE;
}
void free_dns_list(LIST_ELEMENT **list) { list_destroy(list, TRUE); }

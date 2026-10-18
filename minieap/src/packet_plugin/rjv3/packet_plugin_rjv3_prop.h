#ifndef SYSU_RJV3_PROP_H
#define SYSU_RJV3_PROP_H
#include "packet_plugin_rjv3_priv.h"
#include "linkedlist.h"
int append_rjv3_prop(LIST_ELEMENT **list, uint8_t type, uint8_t *content, int len);
RJ_PROP *find_rjv3_prop(LIST_ELEMENT *list, uint8_t type);
int append_rjv3_prop_to_buffer(RJ_PROP *prop, uint8_t *buf, int buflen);
int append_rjv3_prop_list_to_buffer(LIST_ELEMENT *list, uint8_t *buf, int buflen);
RESULT parse_rjv3_buf_to_prop_list(LIST_ELEMENT **list, uint8_t *buf, int buflen, int bare);
void destroy_rjv3_prop_list(LIST_ELEMENT **list);
#endif

#include "linkedlist.h"
#include "minieap_common.h"
#include "packet_plugin_rjv3_priv.h"
#include "misc.h"
#include "logging.h"
#include "eth_frame.h"
#include "packet_util.h"
#include "packet_plugin_rjv3_prop.h"
#include "if_impl.h"

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

RJ_PROP* new_rjv3_prop() {
    RJ_PROP* _prop = (RJ_PROP*)malloc(sizeof(RJ_PROP));
    if (_prop == NULL) {
        PR_ERRNO("RJv3 字段结构内存分配失败");
        return NULL;
    }

    /* REAL MAGIC! */
    _prop->header1.header_type = 0x1a;
    _prop->header2.magic[0] = 0x00;
    _prop->header2.magic[1] = 0x00;
    _prop->header2.magic[2] = 0x13;
    _prop->header2.magic[3] = 0x11;
    return _prop;
}

int append_rjv3_prop(LIST_ELEMENT** list, uint8_t type, uint8_t* content, int len) {
    if (!list || len < 0 || len > 247 || (len && !content)) return -1;
    RJ_PROP* _prop = new_rjv3_prop();
    if (_prop == NULL) return -1;

    uint8_t* buf = NULL;
    if (len > 0) {
        buf = memdup(content, len);
        if (buf == NULL) {
            free(_prop);
            return -1;
        }
    }

    _prop->header1.header_len = len + sizeof(RJ_PROP_HEADER1) + sizeof(RJ_PROP_HEADER2);
    _prop->header2.type = type;
    _prop->header2.len = len + HEADER2_SIZE_NO_MAGIC(_prop);
    _prop->content = buf;
    insert_data(list, _prop);
    return _prop->header1.header_len;
}

static int rjv3_type_prop_compare(void *type, void *prop) {
    return ((RJ_PROP *)prop)->header2.type != *(uint8_t *)type;
}

RJ_PROP* find_rjv3_prop(LIST_ELEMENT* list, uint8_t type) {
    return (RJ_PROP*)lookup_data(list, &type, rjv3_type_prop_compare);
}

int append_rjv3_prop_to_buffer(RJ_PROP* prop, uint8_t* buf, int buflen) {
    int _content_len = PROP_TO_CONTENT_SIZE(prop);
    int _full_len = sizeof(RJ_PROP_HEADER1)+ sizeof(RJ_PROP_HEADER2) + _content_len;

    if (buflen < _full_len) {
        PR_ERR("缓冲空间不足，无法追加字段");
        return -1;
    }

    if (prop->header1.header_type != 0x1a) {
        PR_WARN("不支持写入 header_type 为 %hhu 的字段", prop->header1.header_type);
        return 0;
    }
    memmove(buf, &prop->header1, sizeof(RJ_PROP_HEADER1));
    memmove(buf + sizeof(RJ_PROP_HEADER1), &prop->header2, sizeof(RJ_PROP_HEADER2));
    if (_content_len > 0) {
        memmove(buf + sizeof(RJ_PROP_HEADER1) + sizeof(RJ_PROP_HEADER2),
                prop->content, _content_len);
    }
    return _full_len;
}

int append_rjv3_prop_list_to_buffer(LIST_ELEMENT* list, uint8_t* buf, int buflen) {
    int _props_len = 0, _single_len = 0;
    LIST_ELEMENT* _curr;

    for (_curr = list; _curr; _curr = _curr->next) {
        _single_len = append_rjv3_prop_to_buffer((RJ_PROP*)_curr->content,
                                                 buf + _props_len,
                                                 buflen - _props_len);
        if (_single_len > 0) {
            _props_len += _single_len;
        } else {
            return -1;
        }
    }

    return _props_len;
}

RESULT parse_rjv3_buf_to_prop_list(LIST_ELEMENT **list, uint8_t *buf, int buflen, int bare) {
    const uint8_t magic[] = {0, 0, 0x13, 0x11};
    int pos = 0;
    if (!list || !buf || buflen < 0 || buflen > FRAME_BUF_SIZE) return FAILURE;
    while (pos < buflen) {
        int padding = 1;
        for (int i = pos; i < buflen; i++) if (buf[i]) { padding = 0; break; }
        if (padding) break;
        int prefix = bare ? 0 : 2;
        if (buflen - pos < prefix + 6 || memcmp(buf + pos + prefix, magic, 4)) goto fail;
        const uint8_t *header = buf + pos + prefix;
        int type = header[4], len = header[5] - 2;
        pos += prefix + 6;
        if (!bare && buf[pos - 8] != 0x1a) goto fail;
        /* 部分锐捷结果字段的长度不可靠，用下一个完整头部限定边界。 */
        if (bare && (type == 0 || type == 1)) {
            len = buflen - pos;
            for (int i = 0; i + 6 <= buflen - pos; i++) {
                if (!memcmp(buf + pos + i, magic, 4)) { len = i; break; }
            }
        }
        if (len < 0 || len > buflen - pos || len > 247) goto fail;
        if (append_rjv3_prop(list, (uint8_t)type, buf + pos, len) < 0) goto fail;
        pos += len;
    }
    return SUCCESS;
fail:
    destroy_rjv3_prop_list(list);
    return FAILURE;
}

static void free_content(void* elem, void* unused) {
    free(((RJ_PROP*)elem)->content);
}

void destroy_rjv3_prop_list(LIST_ELEMENT** list) {
    list_traverse(*list, free_content, NULL);
    list_destroy(list, TRUE);
}

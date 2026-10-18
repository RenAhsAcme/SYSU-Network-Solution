#include "packet_util.h"
#include "if_impl.h"
#include <string.h>
#include <stdlib.h>

RESULT append_to_frame(ETH_EAP_FRAME *frame, const uint8_t *data, int len) {
    if (!frame || !frame->content || len < 0 || (len && !data) ||
        frame->actual_len > frame->buffer_len || (size_t)len > frame->buffer_len - frame->actual_len) return FAILURE;
    if (len) memcpy(frame->content + frame->actual_len, data, (size_t)len);
    frame->actual_len += (size_t)len;
    return SUCCESS;
}
/* 锐捷扩展位于标准 EAP 长度之外，保留尾部，但先验证所有标准字段。 */
int validate_eap_frame(const ETH_EAP_FRAME *frame) {
    if (!frame || !frame->content || frame->actual_len > frame->buffer_len ||
        frame->actual_len < 22 || frame->actual_len > FRAME_BUF_SIZE) return 0;
    const uint8_t *p = frame->content;
    if (p[12] != 0x88 || p[13] != 0x8e || p[15] != EAP_PACKET || p[14] < 1 || p[14] > 3) return 0;
    size_t outer = ((size_t)p[16] << 8) | p[17];
    size_t inner = ((size_t)p[20] << 8) | p[21];
    if (outer < 4 || outer > frame->actual_len - 18 || inner < 4 || inner > outer) return 0;
    if (p[18] == EAP_SUCCESS || p[18] == EAP_FAILURE) return inner == 4;
    if (p[18] != EAP_REQUEST || inner < 5) return 0;
    if (p[22] == IDENTITY) return 1;
    return p[22] == MD5_CHALLENGE && inner >= 22 && p[23] == 16;
}

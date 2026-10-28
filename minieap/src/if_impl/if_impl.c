#include "if_impl.h"
static IF_IMPL *iface;
extern IF_IMPL *sockraw_new(void);
int init_if_impl_list(void) { iface = sockraw_new(); return iface != NULL; }
IF_IMPL *get_if_impl(void) { return iface; }
void free_if_impl(void) {
    if (iface) iface->destroy(iface);
    iface = NULL;
}

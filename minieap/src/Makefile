CC ?= cc
CFLAGS ?= -Os
override CPPFLAGS += -D_GNU_SOURCE -Iinclude -Ipacket_plugin/rjv3 -Ipacket_plugin/rjv3/rjv3_hashes
override CFLAGS += -std=gnu11 -Wall -Wextra -Wformat=2 -Wformat-security -Wno-unused-parameter -fstack-protector-strong -ffunction-sections -fdata-sections
override LDFLAGS += -Wl,--gc-sections -Wl,-z,relro,-z,now -Wl,-z,noexecstack
SOURCES := minieap.c config.c eap_state_machine.c packet_builder.c md5.c \
    if_impl/if_impl.c if_impl/sockraw/if_impl_sockraw.c \
    util/packet_util.c util/net_util.c util/linkedlist.c util/misc.c util/logging.c util/sched_alarm.c util/sysu.c \
    packet_plugin/rjv3/packet_plugin_rjv3.c packet_plugin/rjv3/packet_plugin_rjv3_priv.c \
    packet_plugin/rjv3/packet_plugin_rjv3_prop.c packet_plugin/rjv3/packet_plugin_rjv3_keepalive.c \
    $(wildcard packet_plugin/rjv3/rjv3_hashes/*.c)
OBJECTS := $(SOURCES:%.c=build/%.o)
all: sysu-auth
sysu-auth: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(OBJECTS:.o=.d)
.PHONY: all clean
clean:
	rm -rf build sysu-auth

include $(TOPDIR)/rules.mk

PKG_NAME:=sysu-snat
PKG_VERSION:=2.0.0
PKG_RELEASE:=15
PKG_MAINTAINER:=RenAhsAcme
PKG_LICENSE:=GPL-3.0-only
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk

define Package/sysu-snat
  SECTION:=net
  CATEGORY:=Network
  TITLE:=SYSU SNAT
  URL:=https://github.com/RenAhsAcme/SYSU-Network-Solution
  DEPENDS:=+luci-base +rpcd +jshn +ucode +ucode-mod-fs +ucode-mod-uci +ucode-mod-ubus +ucode-mod-uloop +ip-full +nftables-json +curl +ca-bundle +busybox +jsonfilter +libnetfilter-conntrack
endef

define Package/sysu-snat/description
  Validated IPv4/IPv6 source pools with per-traffic connection policies,
  procd lifecycle and LuCI Services page. Disabled by default.
endef

define Package/sysu-snat/conffiles
/etc/config/sysu-snat
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) ./v2/*.c $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror $(PKG_BUILD_DIR)/arp.c $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/arp
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror $(PKG_BUILD_DIR)/net.c $(TARGET_LDFLAGS) -lnetfilter_conntrack -lnfnetlink -lmnl -o $(PKG_BUILD_DIR)/net-helper
endef

define Package/sysu-snat/install
	$(INSTALL_DIR) $(1)/usr/libexec/sysu-snat
	$(INSTALL_BIN) ./files/sysu-snat-dns $(1)/usr/libexec/sysu-snat/dns
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/arp $(PKG_BUILD_DIR)/net-helper $(1)/usr/libexec/sysu-snat/
	$(INSTALL_DIR) $(1)/usr/libexec/sysu-snat/v2
	$(INSTALL_DATA) ./v2/*.uc $(1)/usr/libexec/sysu-snat/v2/
	$(INSTALL_BIN) ./files/sysu-snat-dns.uc $(1)/usr/libexec/sysu-snat/dns-config
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) ./files/sysu-snat-preupgrade $(1)/usr/libexec/sysu-snat/preupgrade
	$(INSTALL_BIN) ./files/sysu-snat-postinst $(1)/usr/libexec/sysu-snat/postinst
	$(INSTALL_BIN) ./files/sysu-snat.init $(1)/etc/init.d/sysu-snat
	$(INSTALL_CONF) ./files/sysu-snat.config $(1)/etc/config/sysu-snat
	chmod 0600 $(1)/etc/config/sysu-snat
	$(INSTALL_DIR) $(1)/usr/libexec/rpcd $(1)/usr/share/rpcd/acl.d $(1)/usr/share/luci/menu.d
	$(INSTALL_BIN) ./files/sysu-snat.rpc $(1)/usr/libexec/rpcd/sysu-snat
	$(INSTALL_DATA) ./files/sysu-snat.acl.json $(1)/usr/share/rpcd/acl.d/sysu-snat.json
	$(INSTALL_DATA) ./files/sysu-snat.menu.json $(1)/usr/share/luci/menu.d/sysu-snat.json
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view $(1)/usr/share/licenses/sysu-snat
	$(INSTALL_DATA) ./files/sysu-snat.js $(1)/www/luci-static/resources/view/sysu-snat.js
	$(INSTALL_DATA) ./LICENSE ./NOTICE.md $(1)/usr/share/licenses/sysu-snat/
endef

define Package/sysu-snat/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
exec /usr/libexec/sysu-snat/postinst
endef

define Package/sysu-snat/prerm
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
[ "$${PKG_UPGRADE}" != 1 ] || { /usr/libexec/sysu-snat/preupgrade; exit $$?; }
/etc/init.d/sysu-snat stop
/usr/libexec/sysu-snat/dns release-wait
endef

$(eval $(call BuildPackage,sysu-snat))

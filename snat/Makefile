include $(TOPDIR)/rules.mk

PKG_NAME:=sysu-snat
PKG_VERSION:=1.0.0
PKG_RELEASE:=8
PKG_MAINTAINER:=RenAhsAcme
PKG_LICENSE:=GPL-3.0-only
PKG_LICENSE_FILES:=LICENSE
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/sysu-snat
  SECTION:=net
  CATEGORY:=Network
  TITLE:=SYSU SNAT
  URL:=https://github.com/RenAhsAcme/SYSU-Network-Solution
  DEPENDS:=+luci-base +luci-lib-jsonc +rpcd +jshn +ucode +ucode-mod-fs +ucode-mod-uci +ucode-mod-ubus +lua +ip-full +nftables-json +curl +ca-bundle +busybox +jsonfilter
endef

define Package/sysu-snat/description
  Validated IPv4/IPv6 source pools with per-traffic connection policies,
  procd lifecycle and LuCI Services page. Disabled by default.
endef

define Package/sysu-snat/conffiles
/etc/config/sysu-snat
endef

define Build/Compile
endef

define Package/sysu-snat/install
	$(INSTALL_DIR) $(1)/usr/lib/sysu-snat $(1)/usr/libexec/sysu-snat
	$(INSTALL_DATA) ./src/*.lua $(1)/usr/lib/sysu-snat/
	$(INSTALL_BIN) ./src/ipv4 ./src/ipv6 ./src/worker ./src/dns $(1)/usr/libexec/sysu-snat/
	$(INSTALL_BIN) ./files/sysu-snat-config.uc $(1)/usr/libexec/sysu-snat/config
	$(INSTALL_BIN) ./files/sysu-snat-dns.uc $(1)/usr/libexec/sysu-snat/dns-config
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/etc/config
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

define Package/sysu-snat/prerm
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
/etc/init.d/sysu-snat stop
/usr/libexec/sysu-snat/dns release-wait
endef

$(eval $(call BuildPackage,sysu-snat))

include $(TOPDIR)/rules.mk

PKG_NAME:=sysu-auth
PKG_VERSION:=1.1.0
PKG_RELEASE:=8
PKG_MAINTAINER:=RenAhsAcme
PKG_LICENSE:=GPLv3
PKG_LICENSE_FILES:=LICENSE
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1
PKG_BUILD_FLAGS:=no-mips16

include $(INCLUDE_DIR)/package.mk

define Package/sysu-auth
  SECTION:=net
  CATEGORY:=Network
  TITLE:=SYSU campus authentication with LuCI
  URL:=https://github.com/RenAhsAcme/SYSU-Network-Solution
  DEPENDS:=+libc +libgcc +luci-base +rpcd +rpcd-mod-file +jshn +ucode +ucode-mod-fs +ucode-mod-uci
  USERID:=sysu-auth:sysu-auth
endef

define Package/sysu-auth/description
  Minimal OpenWrt-only SYSU wired 802.1X client and LuCI Services page.
  Uses procd and a dedicated unprivileged runtime user. Disabled by default.
endef

define Package/sysu-auth/conffiles
/etc/config/sysu-auth
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/Makefile ./src/LICENSE ./src/*.c ./src/include ./src/util ./src/if_impl ./src/packet_plugin $(PKG_BUILD_DIR)/
endef

define Package/sysu-auth/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sysu-auth $(1)/usr/sbin/sysu-auth
	$(INSTALL_BIN) ./files/sysu-auth-import.uc $(1)/usr/sbin/sysu-auth-import
	$(INSTALL_BIN) ./files/sysu-auth.init $(1)/etc/init.d/sysu-auth
	$(INSTALL_CONF) ./files/sysu-auth.config $(1)/etc/config/sysu-auth
	chmod 0600 $(1)/etc/config/sysu-auth
	$(INSTALL_DIR) $(1)/usr/libexec/rpcd $(1)/usr/share/rpcd/acl.d $(1)/usr/share/luci/menu.d
	$(INSTALL_BIN) ./files/sysu-auth.rpc $(1)/usr/libexec/rpcd/sysu-auth
	$(INSTALL_DATA) ./files/sysu-auth.acl.json $(1)/usr/share/rpcd/acl.d/sysu-auth.json
	$(INSTALL_DATA) ./files/sysu-auth.menu.json $(1)/usr/share/luci/menu.d/sysu-auth.json
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view
	$(INSTALL_DATA) ./files/sysu-auth.js $(1)/www/luci-static/resources/view/sysu-auth.js
endef

$(eval $(call BuildPackage,sysu-auth))

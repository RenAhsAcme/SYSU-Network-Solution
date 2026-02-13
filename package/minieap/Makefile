include $(TOPDIR)/rules.mk

PKG_NAME:=minieap
PKG_VERSION:=0.93
PKG_RELEASE:=1
PKG_MAINTAINER:=KumaTea <KumaTea@outlook.com>
PKG_LICENSE:=GPLv3
PKG_LICENSE_FILES:=LICENSE

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/KumaTea/minieap.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=a787b1185a5c08d9d4136fbf7282a4fd4ef3129c
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
	SECTION:=net
	CATEGORY:=Network
	TITLE:=Extensible 802.1x client with Ruijie v3 (v4) plugin
	MAINTAINER:=updateing
	URL:=https://github.com/updateing/minieap
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/minieap $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/lib/netifd/proto
	$(INSTALL_BIN) ./files/minieap.sh $(1)/lib/netifd/proto/
	$(INSTALL_BIN) ./files/minieap.script $(1)/lib/netifd/

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_BIN) .files/etc/init.d/minieap $(1)/etc/init.d/
endef

define Package/$(PKG_NAME)/postinst
	#!/bin/sh
	/etc/init.d/minieap enable
	exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))

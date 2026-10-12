include $(TOPDIR)/rules.mk

PKG_NAME:=minieap
PKG_VERSION:=0.93-SYSU
PKG_RELEASE:=1
PKG_MAINTAINER:=RenAhsAcme
PKG_LICENSE:=GPLv3
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
	SECTION:=net
	CATEGORY:=Network
	TITLE:=SYSU campus network authentication based on MiniEAP.
	MAINTAINER:=RenAhsAcme
	URL:=https://github.com/sysu/Stu-Network
endef

define Build/Prepare
	$(RM) -r $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/minieap $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/lib/netifd/proto
	$(INSTALL_BIN) ./files/minieap.sh $(1)/lib/netifd/proto/
	$(INSTALL_BIN) ./files/minieap.script $(1)/lib/netifd/
endef

$(eval $(call BuildPackage,$(PKG_NAME)))

include $(TOPDIR)/rules.mk

PKG_NAME:=gluon-mesh-babel
PKG_VERSION:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)
PKG_BUILD_DEPENDS := respondd
PKG_BUILD_DEPENDS += libbabelhelper
PKG_BUILD_DEPENDS += libjson-c

include ../gluon.mk

define Package/gluon-mesh-babel
  SECTION:=gluon
  CATEGORY:=Gluon
  TITLE:=Babel mesh
  DEPENDS:=+gluon-core +babeld +mmfd +libiwinfo +libgluonutil +firewall +libjson-c +libnl-tiny +libubus +libubox +libblobmsg-json +libbabelhelper
  PROVIDES:=gluon-mesh-provider
endef

define Build/Prepare
        mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	$(call Build/Compile/Default)
	$(call GluonSrcDiet,./luasrc,$(PKG_BUILD_DIR)/luadest/)
endef

define Package/gluon-mesh-babel/install
        $(CP) ./files/* $(1)/
	$(CP) $(PKG_BUILD_DIR)/luadest/* $(1)/
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_DIR) $(1)/lib/gluon/respondd
	$(CP) $(PKG_BUILD_DIR)/babel-respondd.so $(1)/lib/gluon/respondd/mesh-babel.so
	$(INSTALL_DIR) $(1)/lib/gluon/status-page/providers
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/neighbours-babel $(1)/lib/gluon/status-page/providers/
endef

define Package/gluon-mesh-babel/postinst
#!/bin/sh
$(call GluonCheckSite,check_site.lua)
endef

$(eval $(call BuildPackage,gluon-mesh-babel))


include $(TOPDIR)/rules.mk

PKG_NAME:=gluon-iptables-clamp-mss-to-pmtu

GLUON_VERSION = $(shell git describe --always --dirty=+ 2>/dev/null || echo unknown)
PKG_VERSION:=$(if $(DUMP),x,$(GLUON_VERSION))


PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include ../gluon.mk


define Package/$(PKG_NAME)
  SECTION:=gluon
  CATEGORY:=net
  TITLE:=This will establish a firewall rule to clamp the mss to pmtu on the mesh-vpn interface when the connection is towards 64:ff9b::/96
  DEPENDS:= +ip6tables
endef

define Package/$(PKG_NAME)/description
	Package working around icmp blackholes in the internet.
endef

define Build/Configure
endef

define Build/Compile
	$(call GluonSrcDiet,./luasrc,$(PKG_BUILD_DIR)/luadest/)
endef

define Package/$(PKG_NAME)/install
	$(CP) ./files/* $(1)/
	$(CP) $(PKG_BUILD_DIR)/luadest/* $(1)/

	$(INSTALL_DIR) $(1)/lib/gluon
endef

define Package/$(PKG_NAME)/postinst
endef

$(eval $(call BuildPackage,$(PKG_NAME)))

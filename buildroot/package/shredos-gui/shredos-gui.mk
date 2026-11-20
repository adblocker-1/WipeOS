################################################################################
#
# shredos-gui
#
################################################################################

SHREDOS_GUI_VERSION = 1.0
SHREDOS_GUI_SITE = $(TOPDIR)/package/shredos-gui/src
SHREDOS_GUI_SITE_METHOD = local
SHREDOS_GUI_LICENSE = MIT (LVGL), Public Domain (PDFGen, stb_image), OFL-1.1 (IBM Plex)
SHREDOS_GUI_DEPENDENCIES = nwipe

define SHREDOS_GUI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS) -O2" LDFLAGS="$(TARGET_LDFLAGS)"
endef

define SHREDOS_GUI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/shredos-gui $(TARGET_DIR)/usr/bin/shredos-gui
	$(INSTALL) -D -m 0644 $(@D)/fonts/WipeOS-Sans-Regular.ttf $(TARGET_DIR)/usr/share/wipeos/fonts/WipeOS-Sans-Regular.ttf
	$(INSTALL) -D -m 0644 $(@D)/fonts/WipeOS-Sans-Bold.ttf $(TARGET_DIR)/usr/share/wipeos/fonts/WipeOS-Sans-Bold.ttf
endef

$(eval $(generic-package))

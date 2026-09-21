# SPDX-License-Identifier: Apache-2.0 OR MIT
OMS_VERSION = 0.3.0
OMS_SITE = $(BR2_EXTERNAL_OMS_PATH)/../.image-cache/source
OMS_SITE_METHOD = local
OMS_LICENSE = Apache-2.0 or MIT
OMS_LICENSE_FILES = LICENSE LICENSE-APACHE LICENSE-MIT
OMS_DEPENDENCIES = libgtk3 json-glib host-pkgconf
define OMS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) all
endef
define OMS_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) DESTDIR=$(TARGET_DIR) PREFIX=/usr install
	$(INSTALL) -D -m 0644 $(@D)/LICENSE $(TARGET_DIR)/usr/share/licenses/oms/LICENSE
	$(INSTALL) -D -m 0644 $(@D)/LICENSE-APACHE $(TARGET_DIR)/usr/share/licenses/oms/LICENSE-APACHE
	$(INSTALL) -D -m 0644 $(@D)/LICENSE-MIT $(TARGET_DIR)/usr/share/licenses/oms/LICENSE-MIT
endef
$(eval $(generic-package))

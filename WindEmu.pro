TEMPLATE = subdirs

SUBDIRS += \
    WindCore \
    WindQt

WindQt.depends = WindCore

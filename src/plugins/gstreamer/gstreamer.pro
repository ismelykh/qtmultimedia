TEMPLATE = subdirs

SUBDIRS += \
    audiodecoder \
    mediaplayer

config_gstreamer_encodingprofiles {
#    SUBDIRS += camerabin
}

OTHER_FILES += \
    gstreamer.json

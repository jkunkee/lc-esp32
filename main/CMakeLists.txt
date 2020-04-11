# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "main.c http.c led.c settings_storage.c alarm.c color.c")
set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()

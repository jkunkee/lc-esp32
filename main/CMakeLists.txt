# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "main.c http.c led.c settings_storage.c alarm.c $ENV{IDF_PATH}/examples/peripherals/rmt/led_strip/components/led_strip/src/led_strip_rmt_ws2812.c")
set(COMPONENT_ADD_INCLUDEDIRS "$ENV{IDF_PATH}/examples/peripherals/rmt/led_strip/components/led_strip/include")

register_component()

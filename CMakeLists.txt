# Star Wars (STARWARS.CVP): Atari's 1983 Star Wars arcade game on the
# ColecoVision. The cart runs the arcade board (two 6809s, the matrix
# processor, the vector generator, POKEYs, TMS5220: src/sw.c) from the
# user's own ROMs on the card (/STARWARS), and swcv sends the vector
# picture and the sound to the console as the Game Boy kernel's lists.
# RESET goes back to the menu.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                      -> build/STARWARS.CVP
#
# Copy STARWARS.CVP into the card's /PLUGINS folder.
cmake_minimum_required(VERSION 3.13)

# the cart's SDK: the copy in sdk/ (sdk/README.md)
set(PARROTCART_SDK ${CMAKE_CURRENT_LIST_DIR}/sdk/firmware/revc)
set(PICO_BOARD_HEADER_DIRS ${PARROTCART_SDK}/cvcart_revc/boards)
set(PICO_BOARD cvcart_revc)
set(PICO_PLATFORM rp2350-arm-s)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# the USB host: sdk/tinyusb (TinyUSB 0.21 and its fixes since), not the pico-sdk's 0.18
include(${PARROTCART_SDK}/sdk/cvpad_tinyusb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/pico_sdk_import.cmake)

project(starwars C CXX ASM)
pico_sdk_init()
include(${PARROTCART_SDK}/sdk/cvplugin.cmake)

# The same source makes the same file, whenever it's built: the compiler's
# __DATE__ and __TIME__ (the build date in the binary info) come from
# SOURCE_DATE_EPOCH, fixed here at the tested build's, 2026-11-15 19:28:57 UTC.
set(BUILD_EPOCH 1794770937)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

set(OURS src/starwars.c src/swcard.c src/swroms.c src/swzip.c src/sw.c src/swcv.c src/tms5220.c)
set_source_files_properties(${OURS} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

add_executable(starwars ${OURS} ${PARROTCART_SDK}/midihello/vdp.c)
target_include_directories(starwars PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src ${PARROTCART_SDK}/midihello)
target_compile_definitions(starwars PRIVATE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0)
cvframes_use(starwars)
cvsd_use(starwars)
cvpad_use(starwars)                         # USB game pads: the port is a host
pico_enable_stdio_usb(starwars 0)
pico_enable_stdio_uart(starwars 0)
cvcart_plugin(starwars ID parrotcart.starwars NAME "Star Wars" VERSION 0.1.4 BY "Parrot Cart"
              HOME /STARWARS USES usb-host ICON src/starwars_icon.txt FILE STARWARS.CVP README CART-README.md)

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(taptalk_band)

# Flash the default board into the "storage" partition when it has been generated:
#   python ../tools/make_default_board.py storage_image/live
#   python ../tools/make_default_board.py storage_image/factory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/storage_image/live/board.json")
    fatfs_create_spiflash_image(storage "${CMAKE_CURRENT_SOURCE_DIR}/storage_image" FLASH_IN_PROJECT)
else()
    message(WARNING "No default board in firmware/storage_image. The band will show a 'set me up' screen until a board is sent.")
endif()

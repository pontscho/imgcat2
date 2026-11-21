# Standalone consumer of libimg2 (driven by cmake/scripts/img2_consumer_smoke.cmake)
#   -DIMG2_CONSUMER_MODE=package : find_package(img2 1.0 REQUIRED CONFIG) (CMAKE_PREFIX_PATH = install prefix)
#   -DIMG2_CONSUMER_MODE=subdir  : add_subdirectory(${IMG2_SOURCE_DIR}) of the img2 source tree
cmake_minimum_required(VERSION 3.21)
project(img2_consumer LANGUAGES C)

set(IMG2_CONSUMER_MODE "package" CACHE STRING "How img2 is consumed: package or subdir")

if(IMG2_CONSUMER_MODE STREQUAL "package")
	find_package(img2 1.0 REQUIRED CONFIG)
	message(STATUS "img2 package: ${img2_DIR} (features: ${img2_FEATURES})")
elseif(IMG2_CONSUMER_MODE STREQUAL "subdir")
	if(NOT IMG2_SOURCE_DIR)
		message(FATAL_ERROR "img2_consumer: -DIMG2_SOURCE_DIR=<img2 source tree> is required in subdir mode")
	endif()
	set(IMG2_BUILD_CLI OFF)
	set(IMG2_BUILD_TESTS OFF)
	set(IMG2_INSTALL OFF)
	add_subdirectory(${IMG2_SOURCE_DIR} img2 EXCLUDE_FROM_ALL)
else()
	message(FATAL_ERROR "img2_consumer: unknown IMG2_CONSUMER_MODE '${IMG2_CONSUMER_MODE}' (package|subdir)")
endif()

add_executable(img2_consumer consumer.c)   # default (PIE) link
target_link_libraries(img2_consumer PRIVATE img2::img2)

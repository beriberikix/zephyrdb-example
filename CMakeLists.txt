cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_ZEPHYR_MODULES
	${CMAKE_CURRENT_SOURCE_DIR}/../modules/lib/flatcc-zephyr/modules/lib/flatcc-zephyr
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyrdb_example)

target_sources(app PRIVATE src/main.c)

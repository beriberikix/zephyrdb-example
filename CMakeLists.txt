cmake_minimum_required(VERSION 3.20.0)

# The flatcc-zephyr overlay module provides the FlatCC runtime library.
# West clones beriberikix/flatcc-zephyr at <topdir>/modules/lib/flatcc-zephyr,
# but the Zephyr module.yml lives inside its nested modules/lib/flatcc-zephyr dir.
set(FLATCC_ZEPHYR_MODULE_DIR
    ${CMAKE_CURRENT_LIST_DIR}/../modules/lib/flatcc-zephyr/modules/lib/flatcc-zephyr)
if(EXISTS ${FLATCC_ZEPHYR_MODULE_DIR}/zephyr/module.yml)
  list(APPEND EXTRA_ZEPHYR_MODULES ${FLATCC_ZEPHYR_MODULE_DIR})
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyrdb_example)

target_sources(app PRIVATE src/main.c)

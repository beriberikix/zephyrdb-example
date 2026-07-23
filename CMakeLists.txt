cmake_minimum_required(VERSION 3.20.0)

# flatcc-zephyr is a west project with a repo-root zephyr/module.yml,
# so Zephyr discovers it automatically — no EXTRA_ZEPHYR_MODULES needed.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyrdb_example)

target_sources(app PRIVATE src/main.c)

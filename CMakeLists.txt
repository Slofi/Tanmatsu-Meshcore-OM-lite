cmake_minimum_required(VERSION 3.10)
# Our host-tested core (core/: allocator, registry, router) is its own component, so the device build and
# test/host/run.sh (plain g++) compile the same sources.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(application)

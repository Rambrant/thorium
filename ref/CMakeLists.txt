cmake_minimum_required(VERSION 3.30)

# A DUT repository's whole top-level build, and the template for one: this
# directory is laid out as one (rig/, dut/, suite/, acceptance/ beside this
# file), and everything else comes from a pinned copy of the framework.
#
# Inside the Thorium repository this file is not part of any build -- the
# repository's own top level builds this directory's contents as its default
# deployment (see ../CMakeLists.txt). It is here so that a new DUT repository can
# start as a copy of this directory, and so that the path it takes can be run for
# real: configure it against the checkout it sits in with
#
#     cmake -S ref -B build/ref -DFETCHCONTENT_SOURCE_DIR_THORIUM=$PWD
#
# which is what a fetched tag looks like to the build, minus the download.
project(Reference LANGUAGES CXX)

include(FetchContent)

# The framework and its instrument drivers share one version, and this is where a
# DUT repository states which one it is built against. Moving it is the upgrade --
# on this DUT's schedule, not on every other DUT's.
#
# SOURCE_SUBDIR names a directory that does not exist, on purpose: it makes
# FetchContent download the framework and nothing else. Without it,
# FetchContent_MakeAvailable() would add_subdirectory() the framework's own
# top-level CMakeLists.txt, which is that repository's own deployment and not
# something to configure inside this one.
#
# THORIUM_REPOSITORY is a variable so a mirror can be swapped in without editing
# this file (-DTHORIUM_REPOSITORY=...). Replace the default with where the
# framework actually lives.
set(THORIUM_REPOSITORY "https://github.com/Rambrant/thorium.git" CACHE STRING "Where the framework is fetched from")

FetchContent_Declare(thorium
    GIT_REPOSITORY ${THORIUM_REPOSITORY}
    GIT_TAG        v0.1.1
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  no-such-directory
)
FetchContent_MakeAvailable(thorium)

include(${thorium_SOURCE_DIR}/cmake/ThoriumDeployment.cmake)
thorium_deployment()

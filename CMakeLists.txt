cmake_minimum_required(VERSION 3.30)

# The framework repository's own entry point. Everything below project() is
# cmake/ThoriumDeployment.cmake, which is also what a DUT repository calls after
# fetching a pinned copy of this one -- so this file is a deployment of its own,
# not a special case. Which deployment is a preset's business (cmake
# --list-presets): the default is this repo's bench in rig/, dut/ and suite/, and
# dev/ is the desk bench.
project(Thorium LANGUAGES CXX VERSION 0.1.0)

include(cmake/ThoriumDeployment.cmake)

# This repository's default deployment is the reference one in ref/: a complete,
# simulated rig, DUT and suite laid out exactly as a DUT repository lays out its
# own (rig/, dut/, suite/, acceptance/ directly under its root), so the framework's
# tests and examples have something real to run against. THORIUM_DEPLOYMENT_ROOT is
# what makes that layout work without a copy of the paths: the reference's code
# says "rig/wiring.inc" and means ref/rig/wiring.inc, as a DUT repo's says the same
# thing and means its own. dev/ is the other deployment here and keeps this
# repository's root, so its includes read "dev/rig/wiring.inc"; a preset says which.
set(THORIUM_DEPLOYMENT_ROOT "${CMAKE_SOURCE_DIR}/ref" CACHE PATH
    "The repository root the deployment's rig/, dut/, suite/ and its root-relative #includes resolve against")

thorium_deployment()

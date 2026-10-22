cmake_minimum_required(VERSION 3.30)

# The framework repository's own entry point. Everything below project() is
# cmake/ThoriumDeployment.cmake, which is also what a DUT repository calls after
# fetching a pinned copy of this one -- so this file is a deployment of its own,
# not a special case. Which deployment is a preset's business (cmake
# --list-presets): the default is this repo's bench in rig/, dut/ and suite/, and
# dev/ is the desk bench.
project(Thorium LANGUAGES CXX VERSION 0.1.0)

include(cmake/ThoriumDeployment.cmake)

thorium_deployment()

# Proyecto de test independiente (no afecta el build de la app principal).
# Reusa los componentes reales del proyecto vía EXTRA_COMPONENT_DIRS.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lora_unit_tests)

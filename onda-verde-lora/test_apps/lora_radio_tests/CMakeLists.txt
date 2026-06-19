# Proyecto de test independiente para validar la comunicación LoRa real
# entre 2 placas (master + slave). No afecta el build de la app principal.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lora_radio_tests)

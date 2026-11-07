cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# THE TAB5 (ESP32-P4): its own lock file of the components (else each build of one target writes the lock of
# the other: the P4 has esp_hosted and esp_wifi_remote, the S3 the camera)
if(IDF_TARGET STREQUAL "esp32p4")
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.esp32p4")
endif()
project(tfn_os)

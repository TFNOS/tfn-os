cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# THE TAB5 (ESP32-P4): its own lock file of the components (else each build of one target writes the lock of
# the other: the P4 has esp_hosted and esp_wifi_remote, the S3 the camera)
if(IDF_TARGET STREQUAL "esp32p4")
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.esp32p4")
endif()
# WINDOWS: the compile line of main/ has an include folder for each app and lib (about 400): with the paths of a PC it
# is near the limit of a command line of Windows (32767 characters; "CreateProcess: The parameter is incorrect").
# Response files keep each line short. The builds on other systems do not change.
if(CMAKE_HOST_WIN32)
    set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)
endif()
project(tfn_os)

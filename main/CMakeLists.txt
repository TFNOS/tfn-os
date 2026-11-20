# THE APP LIST: the sources of the firmware come from main/apps.json, the ONE list of the apps (a new app
# = one entry there). THE FOLDERS: main/ is TFN OS; each app has its folder apps/<app>/, the shared code of
# the apps is in libs/<module>/, the code of other projects in third_party/<name>/ (NOTICE.md); apps.json names the
# files, and apps_gen.py finds each name in its folder (ARCADE_SRCS
# has the paths, ARCADE_INCS all the source folders). At each CMake configure main/apps_gen.py reads it and writes the lists of this build into
# <build folder>/apps/: apps_gen.h (ARCADE_APP_<ID> 1/0), apps_list.inc (the apps in the menu order: app_menu.c,
# main.c), apps.cmake (ARCADE_SRCS, ARCADE_DEFS, ARCADE_LDFRAG), apps.lf (the PSRAM .bss lines) and apps_built.json.
#
# THE SELECTION: the "on" flags of main/apps.json, or a file: idf.py -D ARCADE_SELECT=<file> (relative to the project
# folder, or a full path). The file has the same form as apps.json (for example, the main set + TOUCH LAB).
# The old switch still works: -D ARCADE_SIDEB=1
# adds SIDE B. A -D value stays in the CMake cache of
# the build folder: -D ARCADE_SELECT= (empty) goes back to the "on" flags. Use your own build folder for a test set.
#
# The web pages in the firmware (EMBED_TXTFILES); apps.cmake names the pages of the extras that are off.
set(ARCADE_EMBED "web/ha.html" "web/apps.html" "web/comp.html")
# (the early pass of ESP-IDF reads only the requirements of idf_component_register: no generator there)
if(CMAKE_BUILD_EARLY_EXPANSION)
    set(ARCADE_SRCS)
    set(ARCADE_LDFRAG)
else()
    idf_build_get_property(arcade_python PYTHON)
    idf_build_get_property(arcade_ver PROJECT_VER)
    set(arcade_gen "${CMAKE_BINARY_DIR}/apps")
    set(arcade_args --cmake "${arcade_gen}")
    set(arcade_deps "${CMAKE_CURRENT_SOURCE_DIR}/apps.json" "${CMAKE_CURRENT_SOURCE_DIR}/apps_gen.py")
    if(ARCADE_SELECT)
        file(TO_CMAKE_PATH "${ARCADE_SELECT}" arcade_sel)
        if(NOT IS_ABSOLUTE "${arcade_sel}")
            set(arcade_sel "${CMAKE_SOURCE_DIR}/${arcade_sel}")
        endif()
        if(NOT EXISTS "${arcade_sel}")
            message(FATAL_ERROR "Arcade: the selection file ${arcade_sel} (-D ARCADE_SELECT) is not there")
        endif()
        list(APPEND arcade_args --select "${arcade_sel}")
        list(APPEND arcade_deps "${arcade_sel}")
    endif()
    if(ARCADE_SIDEB)
        list(APPEND arcade_args --add "SIDE B")
    endif()
    if(CONFIG_TFN_TARGET_CORES3SE)
        set(arcade_kind cores3)
    elseif(CONFIG_TFN_TARGET_AMOLED18)
        set(arcade_kind am18)
    elseif(CONFIG_TFN_TARGET_WATCHER)
        set(arcade_kind watcher)
    elseif(CONFIG_TFN_TARGET_AMOLED175)
        set(arcade_kind am175)
    elseif(CONFIG_TFN_TARGET_TAB5)
        set(arcade_kind tab5)
    else()
        set(arcade_kind lcd349)
    endif()
    list(APPEND arcade_args --kind ${arcade_kind} "--project-ver=${arcade_ver}")
    # THE EXTRAS (main/Kconfig.projbuild, menu "TFN OS extras"; main/apps.json core.extras): apps_gen.py leaves out
    # the files, the pages and the apps of each extra whose option is off. (An option that the sdkconfig does not
    # have is not in the list: the extra stays in.)
    get_cmake_property(arcade_vars VARIABLES)
    foreach(arcade_v ${arcade_vars})
        if(arcade_v MATCHES "^CONFIG_TFN_EXTRA_" AND NOT "${${arcade_v}}")
            list(APPEND arcade_args "--extra-off=${arcade_v}")
        endif()
    endforeach()
    execute_process(COMMAND "${arcade_python}" "${CMAKE_CURRENT_SOURCE_DIR}/apps_gen.py" ${arcade_args}
                    RESULT_VARIABLE arcade_rc OUTPUT_VARIABLE arcade_out ERROR_VARIABLE arcade_err
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT arcade_rc EQUAL 0)
        message(FATAL_ERROR "Arcade: main/apps_gen.py stopped (${arcade_rc}):\n${arcade_err}\n${arcade_out}")
    endif()
    message(STATUS "${arcade_out}")
    # a change of the app list, of the generator or of the selection file runs the configure again
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${arcade_deps})
    include("${arcade_gen}/apps.cmake")
    if(ARCADE_EMBED_OFF)
        list(REMOVE_ITEM ARCADE_EMBED ${ARCADE_EMBED_OFF})
    endif()
    if(NOT CONFIG_TFN_DEVICE_PROTECTION)
        list(FILTER ARCADE_SRCS EXCLUDE REGEX "(^|/)device_(guard|vault)\\.c$")
    endif()
endif()

# linker.lf has the scheme (the .bss to PSRAM); the generated apps.lf maps the objects of this build to it.
# (ESP-IDF splits the LDFRAGMENTS paths at spaces: the build folder must have no space in its path)
# THE TAB5 (ESP32-P4): no Bluetooth (bt, esp_hid), the DSI panel and the PPA (esp_driver_ppa), the LDO
# of the DSI PHY and of the card, the temperature sensor of the chip (esp_driver_tsens: SETUP).
# (IDF_TARGET: the Kconfig values are not known in the early pass)
if(IDF_TARGET STREQUAL "esp32p4")
    set(arcade_reqs esp_driver_ppa esp_driver_tsens)
else()
    set(arcade_reqs bt esp_hid esp_driver_tsens)
endif()
if(CONFIG_TFN_DEVICE_PROTECTION)
    list(APPEND arcade_reqs esp_security)
endif()
idf_component_register(
    SRCS ${ARCADE_SRCS}
    INCLUDE_DIRS "."
    EMBED_TXTFILES ${ARCADE_EMBED}
    LDFRAGMENTS "linker.lf" ${ARCADE_LDFRAG}
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_ledc esp_driver_i2s esp_driver_rmt
                  esp_lcd esp_timer nvs_flash esp_hw_support fatfs vfs wear_levelling
                  esp_wifi esp_netif lwip esp_event esp_http_server esp_driver_usb_serial_jtag esp_driver_uart esp_driver_pcnt esp_hal_usb esp_adc
                  sdmmc esp_driver_sdmmc esp_driver_sdspi esp_wireguard esp_http_client tcp_transport mbedtls app_update esp_app_format
                  elf_loader esp_mm spi_flash esp_partition bootloader_support esp_rom efuse ${arcade_reqs}
)
# USB AUDIO (main/usb_audio.c): the audio class of TinyUSB, one IN endpoint of 98 bytes (48 kHz, mono,
# 16 bit, one more sample for the drift), a FIFO of 24 ms. PUBLIC: the driver and usb_audio.c see the same numbers.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(arcade_tusb espressif__tinyusb COMPONENT_LIB)
    target_compile_definitions(${arcade_tusb} PUBLIC CFG_TUD_AUDIO=1 CFG_TUD_AUDIO_ENABLE_EP_IN=1
                               CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX=98 CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ=2352)
endif()
# TFN OS: main/tfn_app.c puts its fix of the relocations of components/elf_loader in front of the
# loader's function (the addend of R_XTENSA_GLOB_DAT / JMP_SLOT)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_elf_arch_relocate" APPEND)
# THE WEB PASSWORD: main/web_auth.c puts its gate in front of each handler of the board web server, also
# the handlers of the installed apps (they reach httpd_register_uri_handler through the ABI table of this firmware)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=httpd_register_uri_handler" APPEND)
# app.h finds apps_gen.h and apps_list.inc here; SCN_HAS_<KEY>=1 for each finished scene of SCENES (scn_ready).
# libs/ and third_party/ themselves: an app includes a lib in the SDK form ("tfdraw/tfdraw.h", "zstd/zstd.h"), as out
# of the repo (sdk/docs/app_repo.md)
get_filename_component(arcade_libs "${CMAKE_CURRENT_SOURCE_DIR}/../libs" ABSOLUTE)
get_filename_component(arcade_tp "${CMAKE_CURRENT_SOURCE_DIR}/../third_party" ABSOLUTE)
target_include_directories(${COMPONENT_LIB} PRIVATE "${arcade_gen}" ${ARCADE_INCS} "${arcade_libs}" "${arcade_tp}")
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${ARCADE_DEFS})
if(CONFIG_TFN_DEVICE_PROTECTION AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT EXISTS "${ARCADE_DEVICE_PUBLIC_HEADER}")
        message(FATAL_ERROR "Protected build requires ARCADE_DEVICE_PUBLIC_HEADER with the owner's public keys")
    endif()
    file(TO_CMAKE_PATH "${ARCADE_DEVICE_PUBLIC_HEADER}" guard_public_header)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEVICE_GUARD_PUBLIC_HEADER="${guard_public_header}")
endif()

# THE NATIVE RADIO (main/rf_native.c): no static RAM in the fixed block of the dump engine (0x3FCD0000)
if(CONFIG_IDF_TARGET_ESP32S3 AND CONFIG_TFN_EXTRA_RF_NATIVE AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/rf_guard.ld")
endif()

# Every wrapper supplies the digest of the captured source. Keep it in the image for audit.
if(ARCADE_SOURCE_SHA)
    string(LENGTH "${ARCADE_SOURCE_SHA}" arcade_sha_length)
    if(NOT arcade_sha_length EQUAL 64 OR NOT ARCADE_SOURCE_SHA MATCHES "^[a-f0-9]+$")
        message(FATAL_ERROR "ARCADE_SOURCE_SHA must be a SHA-256 digest")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/arcade_source.c"
         "const char arcade_source_identity[] = \"ARCADE_SOURCE=${ARCADE_SOURCE_SHA}\";\n")
    target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}/arcade_source.c")
    target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=arcade_source_identity")
endif()

# The build does not use -Os on the scenes and the 15 big games. -Os gives 445 KB more flash room, but on a 3.49 the
# games lose 10-22% of their fps (CRUISE, ROOM, KEEPER, WRONG ROOM, STAND-IN, FRAME, ROPE, SKY) and the scene builds
# take 13% longer.

# Capture backend uses a build-local archive; never changes the installed IDF.
if(CONFIG_IDF_TARGET_ESP32S3 AND CONFIG_TFN_EXTRA_WIFI_CAPTURE AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT IDF_VERSION_MAJOR EQUAL 6 OR NOT IDF_VERSION_MINOR EQUAL 1)
        message(FATAL_ERROR "Capture backend requires reviewed ESP-IDF 6.1")
    endif()
    idf_build_get_property(capture_python PYTHON)
    set(capture_archive "${CMAKE_BINARY_DIR}/capture_driver/libnet80211.a")
    execute_process(COMMAND "${capture_python}" "${CMAKE_CURRENT_SOURCE_DIR}/capture_driver.py"
        "$ENV{IDF_PATH}/components/esp_wifi/lib/esp32s3/libnet80211.a"
        "${capture_archive}" "${CMAKE_C_COMPILER}"
        RESULT_VARIABLE capture_rc ERROR_VARIABLE capture_error)
    if(NOT capture_rc EQUAL 0)
        message(FATAL_ERROR "Capture driver preparation failed: ${capture_error}")
    endif()
    set_target_properties(esp_wifi_net80211 PROPERTIES IMPORTED_LOCATION "${capture_archive}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/capture_driver.py")
endif()

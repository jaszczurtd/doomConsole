# Project-specific recipe included by JaszczurHAL's native RP dispatcher.

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Generate compile_commands.json for VS Code" FORCE)

if(JH_PICOTOOL_EXECUTABLE AND NOT TARGET picotool)
    if(NOT EXISTS "${JH_PICOTOOL_EXECUTABLE}")
        message(FATAL_ERROR
            "JH_PICOTOOL_EXECUTABLE does not exist: "
            "${JH_PICOTOOL_EXECUTABLE}")
    endif()
    add_executable(picotool IMPORTED GLOBAL)
    set_target_properties(picotool PROPERTIES
        IMPORTED_LOCATION "${JH_PICOTOOL_EXECUTABLE}")
endif()

# The Doom build options live in hal_project_config.h; read them through the
# project configuration the dispatcher evaluated for this target and variant.
foreach(_option DOOM_BOOT_PROBE_ONLY DOOM_DUAL_CORE_COLUMNS
        DOOM_RENDER_ASYNC_PLANES DOOM_VIDEO_SYNC_FLUSH DOOM_HIGHRES_SCENE)
    jh_project_value(${_option} ${_option})
endforeach()
jh_project_defined(DOOM_TFT_PANEL_IS_ST7796S DOOM_TFT_PANEL_ST7796S)
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(PACKAGE_NAME "Chocolate Doom")
set(PACKAGE_TARNAME "chocolate-doom")
set(PACKAGE_VERSION "3.0.0")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PROGRAM_PREFIX "chocolate-")
set(HAVE_DIRENT_H 1)
set(HAVE_MMAP 1)
set(HAVE_DECL_STRCASECMP 1)
set(HAVE_DECL_STRNCASECMP 1)
configure_file("${JH_PROJECT_DIR}/cmake/config.h.cin" "${GENERATED_DIR}/config.h")

if(DOOM_BOOT_PROBE_ONLY)
    set(DOOM_PORT_SOURCES
        src/jaszczurhal/doom_boot_probe.cpp
    )
else()
    file(GLOB DOOM_GAME_SOURCES CONFIGURE_DEPENDS
        "${JH_PROJECT_DIR}/src/doom/*.c"
    )

    set(DOOM_PORT_SOURCES
        src/jaszczurhal/doom_app.cpp
        src/jaszczurhal/doom_backend_stub.c
        src/jaszczurhal/doom_gamepad_input.c
        src/jaszczurhal/doom_input_hal.c
        src/jaszczurhal/doom_storage_hal.c
        src/jaszczurhal/doom_string_hal.c
        src/jaszczurhal/doom_timer_hal.c
        src/jaszczurhal/doom_video_hal.c

        src/aes_prng.c
        src/d_event.c
        src/d_iwad.c
        src/d_loop.c
        src/d_mode.c
        src/deh_str.c
        src/image_decoder.c
        src/i_sound.c
        src/i_main.c
        src/m_argv.c
        src/m_bbox.c
        src/m_cheat.c
        src/m_controls.c
        src/m_fixed.c
        src/m_misc.c
        src/memio.c
        src/musx_decoder.c
        src/sha1.c
        src/tables.c
        src/tiny_huff.c
        src/v_diskicon.c
        src/v_video.c
        src/w_checksum.c
        src/w_file.c
        src/w_file_memory.c
        src/w_main.c
        src/w_merge.c
        src/w_wad.c
        src/z_zone.c

        src/jaszczurhal/doom_glob_hal.c
        src/jaszczurhal/doom_sound_hal.c
        src/jaszczurhal/doom_system_hal.c
    )

    foreach(_src IN LISTS DOOM_GAME_SOURCES)
        file(RELATIVE_PATH _rel "${JH_PROJECT_DIR}" "${_src}")
        list(APPEND DOOM_PORT_SOURCES "${_rel}")
    endforeach()
endif()

list(REMOVE_DUPLICATES DOOM_PORT_SOURCES)

foreach(_flag DOOM_BOOT_PROBE_ONLY DOOM_DUAL_CORE_COLUMNS DOOM_RENDER_ASYNC_PLANES
        DOOM_VIDEO_SYNC_FLUSH DOOM_HIGHRES_SCENE)
    if(NOT ${_flag} MATCHES "^[01]$")
        message(FATAL_ERROR "${_flag} must be 0 or 1 (got '${${_flag}}')")
    endif()
endforeach()

set(DOOM_SCREEN_DEFINES)
if(DOOM_HIGHRES_SCENE STREQUAL "1")
    if(DOOM_TFT_PANEL_IS_ST7796S)
        list(APPEND DOOM_SCREEN_DEFINES
            DOOM_HIGHRES_SCENE=1
            SCREENWIDTH=480
            SCREENHEIGHT=320
            FIXED_SCREENWIDTH=0
        )
    else()
        list(APPEND DOOM_SCREEN_DEFINES
            DOOM_HIGHRES_SCENE=1
            SCREENWIDTH=320
            SCREENHEIGHT=240
            FIXED_SCREENWIDTH=1
        )
    endif()
else()
    list(APPEND DOOM_SCREEN_DEFINES
        DOOM_HIGHRES_SCENE=0
        SCREENWIDTH=320
        SCREENHEIGHT=200
        FIXED_SCREENWIDTH=1
    )
endif()

if(NOT DOOM_TFT_PANEL_IS_ST7796S)
    set(DOOM_TFT_DEFINES DOOM_TFT_PANEL_ILI9341=1)
endif()

set(DOOM_PORT_DEFINES
    ${DOOM_TFT_DEFINES}
    ${DOOM_SCREEN_DEFINES}
    DOOM_BOOT_PROBE_ONLY=${DOOM_BOOT_PROBE_ONLY}
    DOOM_ONLY=1
    DOOM_SMALL=1
    DOOM_TINY=1
    DOOM_CONST=1
    PICO_BUILD=1
    PICO_DOOM=1
    LIB_PICO_STDLIB=1
    JASZCZURHAL_PORT=1
    JASZCZURHAL_ZONE_BYTES=65536
    JASZCZURHAL_ZONE_MIN_BYTES=49152
    DOOM_RENDER_ASYNC_PLANES=${DOOM_RENDER_ASYNC_PLANES}
    DOOM_VIDEO_SYNC_FLUSH=${DOOM_VIDEO_SYNC_FLUSH}
    DOOM_DUAL_CORE_COLUMNS=${DOOM_DUAL_CORE_COLUMNS}
    DOOM_SPRITE_OCCLUSION=0
    SHRINK_MOBJ=1
    FLOOR_CEILING_CLIP_8BIT=0
    LOAD_COMPRESSED=1
    MUSX_COMPRESSED=1
    NO_DEMO_RECORDING=1
    NO_DRAW_BOTTOM=1
    NO_DRAW_MASKED=1
    NO_DRAW_MID=1
    NO_DRAW_PSPRITES=1
    NO_DRAWSEGS=0
    NO_DRAW_SKY=1
    NO_DRAW_SPRITES=0
    NO_DRAW_TOP=1
    NO_INTERCEPTS_OVERRUN=1
    NO_IERROR=1
    NO_MASKED_FLOOR_CLIP=0
    NO_FILE_ACCESS=1
    NO_RDRAW=1
    NO_SCREENSHOT=1
    NO_USE_ARGS=1
    NO_USE_BOUND_CONFIG=1
    NO_USE_CHECKSUM=1
    NO_USE_DEH=1
    NO_USE_EXIT=1
    NO_USE_FLOAT=1
    NO_USE_GUS=1
    NO_USE_JOYSTICK=1
    NO_USE_LIBSAMPLERATE=1
    NO_USE_LOAD=1
    NO_USE_LOADING_DISK=1
    NO_USE_MUSIC_PACKS=1
    NO_USE_MOUSE=1
    NO_USE_NET=1
    NO_USE_RELOAD=1
    NO_USE_SAVE=1
    NO_USE_SAVE_CONFIG=1
    NO_USE_STATE_MISC=1
    NO_USE_TIMIDITY=1
    NO_USE_WIPE=1
    NO_USE_ZLIGHT=1
    NO_VISPLANE_CACHES=1
    NO_VISPLANE_GUTS=1
    NO_VISSPRITES=0
    MAXVISSPRITES=32
    NO_Z_MALLOC_USER_PTR=1
    NO_Z_ZONE_ID=1
    PD_CLIP_WALLS=1
    PD_COLUMNS=1
    PD_DRAW_COLUMNS=1
    PD_DRAW_MARKERS=1
    PD_DRAW_PLANES=1
    PD_QUANTIZE=1
    PD_SCALE_SORT=1
    PD_SANITY=1
    PICO_NO_TIMING_DEMO=1
    SAVE_COMPRESSED=1
    TEMP_IMMUTABLE_DISABLED=1
    DOOM_FLASH_XIP_BASE=0x10000000
    DOOM_FLASH_SIZE_BYTES=0x400000
    DOOM_WHD_FLASH_ADDR=0x10200000
    TINY_WAD_ADDR=DOOM_WHD_FLASH_ADDR
    USE_ERASE_FRAME=1
    USE_CONST_MUSIC=1
    USE_CONST_SFX=1
    USE_FLAT_MAX_256=1
    USE_INDEX_LINEBUFFER=1
    USE_LIGHTMAP_INDEXES=1
    USE_MEMMAP_ONLY=1
    USE_MEMORY_WAD=1
    USE_MUSX=1
    USE_RAW_MAPLINEDEF=1
    USE_RAW_MAPNODE=1
    USE_RAW_MAPSEG=1
    USE_RAW_MAPTHING=1
    USE_RAW_MAPVERTEX=1
    USE_READONLY_MMAP=1
    USE_SINGLE_IWAD=1
    USE_THINKER_POOL=1
    USE_WHD=1
    WHD_SUPER_TINY=1
    USE_ZONE_FOR_MALLOC=1
    Z_MALOOC_EXTRA_DATA=1
    NUM_SOUND_CHANNELS=8
    SOUND_LOW_PASS=1
)

set(DOOM_PORT_WARNING_FLAGS
    "-Wno-unused-variable"
    "-Wno-unused-function"
    "-Wno-unused-but-set-variable"
    "-Wno-unused-but-set-parameter"
    "-Wno-implicit-fallthrough"
    "-Wno-sign-compare"
    "-Wno-format-truncation"
    "-Wno-cpp"
    "-Wno-cast-function-type"
    "-Wno-maybe-uninitialized"
)

set(DOOM_PORT_C_WARNING_FLAGS
    ${DOOM_PORT_WARNING_FLAGS}
    "-Wno-discarded-qualifiers"
)

set(HAL_PROJECT_CONFIG_DIR "${JH_PROJECT_DIR}")
# The variant's definitions reach the HAL and, through it, the Doom sources.
set(EXTRA_HAL_DEFINES ${JH_PROJECT_DEFINITIONS})
set(JH_RP_BOARD_DEFINES ${JH_BOARD_COMPILE_DEFINITIONS})
include("${JH_ROOT}/cmake/jh_rp_pico_sdk.cmake")

set(DOOM_PORT_SOURCE_PATHS)
foreach(_src IN LISTS DOOM_PORT_SOURCES)
    list(APPEND DOOM_PORT_SOURCE_PATHS "${JH_PROJECT_DIR}/${_src}")
endforeach()

add_executable(firmware ${DOOM_PORT_SOURCE_PATHS})
target_sources(firmware PRIVATE
    "${JH_BOARD_GENERATED_DIR}/jh_link_contract_reference.c")
target_include_directories(firmware PRIVATE
    "${JH_PROJECT_DIR}"
    "${GENERATED_DIR}"
    "${JH_PROJECT_DIR}/src"
    "${JH_PROJECT_DIR}/src/doom"
    "${JH_PROJECT_DIR}/src/jaszczurhal"
    "${JH_PROJECT_DIR}/src/adpcm-xq"
    "${JH_BOARD_GENERATED_DIR}"
)
target_compile_definitions(firmware PRIVATE ${DOOM_PORT_DEFINES})
target_compile_options(firmware PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:${DOOM_PORT_C_WARNING_FLAGS}>"
    "$<$<COMPILE_LANGUAGE:CXX>:${DOOM_PORT_WARNING_FLAGS}>"
)
jh_add_rp_pico_firmware(firmware)
target_sources(JaszczurHAL PRIVATE
    "${JH_BOARD_GENERATED_DIR}/jh_link_contract_definition.c")
target_include_directories(JaszczurHAL PUBLIC "${JH_BOARD_GENERATED_DIR}")

add_custom_command(TARGET firmware POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${JH_ARTIFACT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:firmware>" "${JH_ARTIFACT_DIR}/firmware.elf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/firmware.bin"
            "${JH_ARTIFACT_DIR}/firmware.bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/firmware.uf2"
            "${JH_ARTIFACT_DIR}/firmware.uf2"
    VERBATIM
)

add_custom_target(firmware_debug DEPENDS firmware)
add_custom_target(firmware_upload DEPENDS firmware)
add_custom_target(firmware_compile_db DEPENDS firmware)

message(STATUS
    "Doom native recipe: target=${JH_TARGET} board=${JH_BOARD} "
    "sources=${DOOM_PORT_SOURCES}")

include(CMakeDependentOption)

include(FixInterfaceIncludes)

set(SOURCES_QT_SDL
    main.cpp
    main_shaders.h
    Screen.cpp
    Window.cpp
    EmuInstance.cpp
    EmuInstanceAudio.cpp
    EmuInstanceInput.cpp
    EmuThread.cpp
    NsmbNetplayPoC.cpp
    NsmbNetplayPoC.h
    NsmbNetplayConfig.cpp
    NsmbNetplayConfig.h
    NsmbInputTimeline.cpp
    NsmbInputTimeline.h
    NsmbInputProtocol.cpp
    NsmbInputProtocol.h
    NsmbInputDelivery.cpp
    NsmbInputDelivery.h
    NsmbNetplayProtocol.cpp
    NsmbNetplayProtocol.h
    NsmbNetplayTransport.cpp
    NsmbNetplayTransport.h
    NsmbNetplayDiagnostics.cpp
    NsmbNetplayDiagnostics.h
    NsmbNetplayCoordinator.cpp
    NsmbNetplayCoordinator.h
    NsmbGameState.cpp
    NsmbGameStateReader.cpp
    NsmbGameStateWriter.cpp
    NsmbGameState.h
    NsmbGameStateWriter.h
    NsmbRollbackStore.cpp
    NsmbRollbackStore.h
    NsmbAiObservation.cpp
    NsmbAiObservation.h
    NsmbImitationAI.cpp
    NsmbImitationAI.h
    NsmbRuleAI.cpp
    NsmbRuleAI.h
    CheatImportDialog.cpp
    CheatsDialog.cpp
    Config.cpp
    DateTimeDialog.cpp
    EmuSettingsDialog.cpp
    PowerManagement/PowerManagementDialog.cpp
    PowerManagement/resources/battery.qrc
    InputConfig/InputConfigDialog.cpp
    InputConfig/MapButton.h
    InputConfig/resources/ds.qrc
    VideoSettingsDialog.cpp
    CameraSettingsDialog.cpp
    AudioSettingsDialog.cpp
    FirmwareSettingsDialog.cpp
    PathSettingsDialog.cpp
    MPSettingsDialog.cpp
    WifiSettingsDialog.cpp
    InterfaceSettingsDialog.cpp
    ROMInfoDialog.cpp
    RAMInfoDialog.cpp
    TitleManagerDialog.cpp
    OSD_shaders.h
    font.h
    Platform.cpp
    Platform_AAC.cpp
    QPathInput.h
    SaveManager.cpp
    CameraManager.cpp
    AboutDialog.cpp
    AboutDialog.h
    AboutDialog.ui
    ARCodeQDecl.h

    ArchiveUtil.h
    ArchiveUtil.cpp

    ../ScreenLayout.cpp
    ../mic_blow.h

    ../glad/glad.c
    ../duckstation/gl/context.cpp

    ${CMAKE_SOURCE_DIR}/res/melon.qrc

    CLI.h
    CLI.cpp

    LANDialog.cpp
    NetplayDialog.cpp
)

set_source_files_properties(NsmbAiObservation.cpp PROPERTIES HEADER_FILE_ONLY ON)

option(USE_QT6 "Use Qt 6 instead of Qt 5" ON)

if (USE_QT6)
    find_package(Qt6 COMPONENTS Core Gui Widgets Network Multimedia OpenGL OpenGLWidgets Svg REQUIRED)
    set(QT_LINK_LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::OpenGL Qt6::OpenGLWidgets)
else()
    find_package(Qt5 COMPONENTS Core Gui Widgets Network Multimedia Svg REQUIRED)
    set(QT_LINK_LIBS Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Multimedia)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

if (BUILD_STATIC)
    list(APPEND PKG_CONFIG_EXECUTABLE "--static")
endif()

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(LibArchive REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(Zstd REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(Faad REQUIRED IMPORTED_TARGET faad2)

fix_interface_includes(PkgConfig::SDL2 PkgConfig::LibArchive)

add_compile_definitions(ARCHIVE_SUPPORT_ENABLED)

add_executable(melonDS ${SOURCES_QT_SDL})

add_executable(nsmb_mvl_imitation_cpp_predict
    NsmbImitationAI.cpp
    NsmbImitationAI.h
    ${CMAKE_SOURCE_DIR}/tools/nsmb_mvl_imitation_cpp_predict.cpp
)
target_include_directories(nsmb_mvl_imitation_cpp_predict PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(nsmb_mvl_compact_cpp_predict
    NsmbImitationAI.cpp
    NsmbImitationAI.h
    ${CMAKE_SOURCE_DIR}/tools/nsmb_mvl_compact_cpp_predict.cpp
)
target_include_directories(nsmb_mvl_compact_cpp_predict PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if (BUILD_TESTING)
    add_executable(nsmb_netplay_config_tests
        NsmbNetplayConfig.cpp
        NsmbNetplayConfig.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_netplay_config_tests.cpp
    )
    target_include_directories(nsmb_netplay_config_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set_target_properties(nsmb_netplay_config_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-netplay-config COMMAND nsmb_netplay_config_tests)

    add_executable(nsmb_input_timeline_tests
        NsmbInputTimeline.cpp
        NsmbInputTimeline.h
        NsmbNetplayPoC.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_input_timeline_tests.cpp
    )
    target_include_directories(nsmb_input_timeline_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_input_timeline_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-input-timeline COMMAND nsmb_input_timeline_tests)

    add_executable(nsmb_input_protocol_tests
        NsmbInputProtocol.cpp
        NsmbInputProtocol.h
        NsmbNetplayPoC.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_input_protocol_tests.cpp
    )
    target_include_directories(nsmb_input_protocol_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_input_protocol_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-input-protocol COMMAND nsmb_input_protocol_tests)

    add_executable(nsmb_input_delivery_tests
        NsmbInputDelivery.cpp
        NsmbInputDelivery.h
        NsmbInputProtocol.cpp
        NsmbInputProtocol.h
        NsmbNetplayPoC.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_input_delivery_tests.cpp
    )
    target_include_directories(nsmb_input_delivery_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_input_delivery_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-input-delivery COMMAND nsmb_input_delivery_tests)

    add_executable(nsmb_session_protocol_tests
        NsmbNetplayProtocol.cpp
        NsmbNetplayProtocol.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_session_protocol_tests.cpp
    )
    target_include_directories(nsmb_session_protocol_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_session_protocol_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-session-protocol COMMAND nsmb_session_protocol_tests)

    add_executable(nsmb_session_policy_tests
        NsmbNetplayProtocol.cpp
        NsmbNetplayProtocol.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_session_policy_tests.cpp
    )
    target_include_directories(nsmb_session_policy_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_session_policy_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-session-policy COMMAND nsmb_session_policy_tests)

    add_executable(nsmb_packet_classifier_tests
        NsmbNetplayProtocol.cpp
        NsmbNetplayProtocol.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_packet_classifier_tests.cpp
    )
    target_include_directories(nsmb_packet_classifier_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_packet_classifier_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-packet-classifier COMMAND nsmb_packet_classifier_tests)

    add_executable(nsmb_game_state_tests
        NsmbGameState.cpp
        NsmbGameState.h
        NsmbGameStateWriter.h
        NsmbNetplayProtocol.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_game_state_tests.cpp
    )
    target_include_directories(nsmb_game_state_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_game_state_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-game-state COMMAND nsmb_game_state_tests)

    add_executable(nsmb_rule_ai_tests
        NsmbRuleAI.cpp
        NsmbRuleAI.h
        NsmbGameState.h
        NsmbGameStateReader.h
        NsmbNetplayPoC.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_rule_ai_tests.cpp
    )
    target_include_directories(nsmb_rule_ai_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_rule_ai_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-rule-ai COMMAND nsmb_rule_ai_tests)

    add_executable(nsmb_rollback_store_tests
        NsmbRollbackStore.cpp
        NsmbRollbackStore.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_rollback_store_tests.cpp
    )
    target_include_directories(nsmb_rollback_store_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_rollback_store_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-rollback-store COMMAND nsmb_rollback_store_tests)
endif()

add_subdirectory("../../net"
    ${CMAKE_BINARY_DIR}/net)

if (BUILD_TESTING)
    add_executable(nsmb_netplay_diagnostics_tests
        NsmbNetplayDiagnostics.cpp
        NsmbNetplayDiagnostics.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_netplay_diagnostics_tests.cpp
    )
    target_include_directories(nsmb_netplay_diagnostics_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_netplay_diagnostics_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-netplay-diagnostics COMMAND nsmb_netplay_diagnostics_tests)

    add_executable(nsmb_netplay_coordinator_tests
        NsmbNetplayCoordinator.cpp
        NsmbNetplayCoordinator.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_netplay_coordinator_tests.cpp
    )
    target_include_directories(nsmb_netplay_coordinator_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    set_target_properties(nsmb_netplay_coordinator_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-netplay-coordinator COMMAND nsmb_netplay_coordinator_tests)

    add_executable(nsmb_netplay_transport_tests
        NsmbNetplayTransport.cpp
        NsmbNetplayTransport.h
        ${CMAKE_SOURCE_DIR}/tests/nsmb_netplay_transport_tests.cpp
    )
    target_include_directories(nsmb_netplay_transport_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    target_link_libraries(nsmb_netplay_transport_tests PRIVATE net-utils)
    if (WIN32)
        target_link_libraries(nsmb_netplay_transport_tests PRIVATE winmm)
    endif()
    set_target_properties(nsmb_netplay_transport_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME nsmb-netplay-transport COMMAND nsmb_netplay_transport_tests)
endif()

target_link_libraries(melonDS PRIVATE net-utils)

if (WIN32)
    target_link_libraries(melonDS PUBLIC opengl32)

    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_wgl.cpp

        ../glad/glad_wgl.c
    )

    if (MINGW AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET melonDS PROPERTY AUTORCC_OPTIONS "--no-zstd")
    endif()

    if (BUILD_STATIC)
        find_package(SDL2 CONFIG COMPONENTS SDL2main)
        if (TARGET SDL2::SDL2main)
            target_link_libraries(melonDS PRIVATE SDL2::SDL2main)
        endif()
    endif()
elseif (APPLE)
    if (NOT USE_QT6)
        find_library(COCOA_LIB Cocoa)
        target_link_libraries(melonDS PRIVATE ${COCOA_LIB})
    endif()
    find_library(OPENGL_LIB OpenGL)
    target_link_libraries(melonDS PRIVATE ${OPENGL_LIB})
    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_agl.mm
    )
    set_source_files_properties(
        ../duckstation/gl/context_agl.mm
        PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations"
    )
else()
    find_package(X11 REQUIRED)
    find_package(EGL REQUIRED)

    option(ENABLE_WAYLAND "Enable Wayland support" ON)

    if (ENABLE_WAYLAND)
        # we only need ECM for Wayland
        # so we only require it from here
        find_package(ECM REQUIRED NO_MODULE)
        list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")

        find_package(Wayland REQUIRED Client)

        target_compile_definitions(melonDS PRIVATE WAYLAND_ENABLED)

        target_sources(melonDS PRIVATE
            ../duckstation/gl/context_egl_wayland.cpp
        )
    endif()

    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_egl.cpp
        ../duckstation/gl/context_egl_x11.cpp
        ../duckstation/gl/context_glx.cpp
        ../duckstation/gl/x11_window.cpp

        ../glad/glad_egl.c
        ../glad/glad_glx.c
    )
    target_link_libraries(melonDS PRIVATE "${X11_LIBRARIES}" "${EGL_LIBRARIES}")
    target_include_directories(melonDS PRIVATE "${X11_INCLUDE_DIR}")
    add_compile_definitions(QAPPLICATION_CLASS=QApplication)
endif()


if (BUILD_STATIC)
    qt_import_plugins(melonDS INCLUDE Qt::QSvgPlugin)
    if (WIN32 AND USE_QT6)
        qt_import_plugins(melonDS INCLUDE Qt::QModernWindowsStylePlugin)
    endif()
    if (USE_VCPKG AND UNIX AND NOT APPLE)
        pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
        target_link_libraries(melonDS PRIVATE PkgConfig::ALSA)
    elseif (NOT APPLE)
        target_link_options(melonDS PRIVATE -static)
    endif()
endif()

target_include_directories(melonDS PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/..")

if (USE_QT6)
    target_include_directories(melonDS PUBLIC ${Qt6Gui_PRIVATE_INCLUDE_DIRS})
else()
    target_include_directories(melonDS PUBLIC ${Qt5Gui_PRIVATE_INCLUDE_DIRS})
endif()
target_link_libraries(melonDS PRIVATE core)
target_link_libraries(melonDS PRIVATE PkgConfig::SDL2 PkgConfig::LibArchive PkgConfig::Zstd PkgConfig::Faad)
target_link_libraries(melonDS PRIVATE ${QT_LINK_LIBS} ${CMAKE_DL_LIBS})

if (WIN32)
    option(PORTABLE "Make a portable build that looks for its configuration in the current directory" ON)

    if (PORTABLE)
        target_compile_definitions(melonDS PRIVATE WIN32_PORTABLE)
    endif()

    string(REPLACE . , MELON_RC_VERSION ${melonDS_VERSION})
    configure_file("${CMAKE_SOURCE_DIR}/res/melon.rc.in" "${CMAKE_BINARY_DIR}/res/melon.rc")
    target_sources(melonDS PUBLIC "${CMAKE_BINARY_DIR}/res/melon.rc")
    target_include_directories(melonDS PRIVATE "${CMAKE_BINARY_DIR}/res")

    if (${ARCHITECTURE} STREQUAL x86_64)
        set(WIN32_ARCHITECTURE amd64)
    elseif (${ARCHITECTURE} STREQUAL x86)
        set(WIN32_ARCHITECTURE x86)
    elseif (${ARCHITECTURE} MATCHES "ARM(64)?")
        string(TOLOWER "${ARCHITECTURE}" WIN32_ARCHITECTURE)
    endif()


    target_link_libraries(melonDS PRIVATE ws2_32 iphlpapi)
    set_target_properties(melonDS PROPERTIES WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,OFF,ON>)

    if (MINGW)
        configure_file("${CMAKE_SOURCE_DIR}/res/xp.manifest.in" "${CMAKE_BINARY_DIR}/res/xp.manifest")
    else()
        # Work around faad2 unconditionally linking libm even on Windows
        if (USE_VCPKG)
            add_library(m STATIC)
            target_link_libraries(melonDS PUBLIC m)
        endif()
    endif()
endif()

if (APPLE)
    # Copy icon into the bundle
    set(RESOURCE_FILES "${CMAKE_SOURCE_DIR}/res/melon.icns")
    target_sources(melonDS PUBLIC "${RESOURCE_FILES}")

    set_target_properties(melonDS PROPERTIES
        MACOSX_BUNDLE true
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/res/melon.plist.in
        OUTPUT_NAME melonDS
        RESOURCE "${RESOURCE_FILES}")


    option(MACOS_BUNDLE_LIBS "Bundle libraries with the app on macOS" OFF)
    option(MACOS_BUILD_DMG "Build DMG image of the macOS application bundle" OFF)

    if (MACOS_BUNDLE_LIBS)
        if (MACOS_BUILD_DMG)
            set(DMGARG "--dmg")
        endif()
        add_custom_command(TARGET melonDS POST_BUILD
            COMMAND ${CMAKE_SOURCE_DIR}/tools/mac-libs.rb ${DMGARG} ${CMAKE_BINARY_DIR}
            COMMENT "Bundling macOS libraries...")
    endif()
endif()

if (UNIX AND NOT APPLE)
    foreach(SIZE 16 32 48 64 128 256)
        install(FILES ${CMAKE_SOURCE_DIR}/res/icon/melon_${SIZE}x${SIZE}.png
            DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/${SIZE}x${SIZE}/apps
            RENAME net.kuribo64.melonDS.png)
    endforeach()

    install(FILES ${CMAKE_SOURCE_DIR}/res/net.kuribo64.melonDS.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
    install(TARGETS melonDS BUNDLE DESTINATION ${CMAKE_BINARY_DIR} RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

    if (NOT USE_QT6)
        set_target_properties(melonDS PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION OFF
            INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
    endif()
elseif(APPLE)
    install(TARGETS melonDS BUNDLE DESTINATION "${CMAKE_INSTALL_PREFIX}/Applications")
endif()

if (ENABLE_OGLRENDERER)
    set(MELONDS_GL_HEADER \"frontend/glad/glad.h\" CACHE STRING "Path to a header that contains OpenGL function and type declarations.")

    target_compile_definitions(melonDS PUBLIC OGLRENDERER_ENABLED)
    target_compile_definitions(melonDS PUBLIC MELONDS_GL_HEADER=${MELONDS_GL_HEADER})
    target_compile_definitions(core PUBLIC MELONDS_GL_HEADER=${MELONDS_GL_HEADER})
endif()

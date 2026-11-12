include_directories(
  ${CMAKE_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/backend
  ${TDE_INCLUDE_DIR}
  ${TQT_INCLUDE_DIRS}
  ${PIPEWIRE_INCLUDE_DIRS}
  ${PULSE_INCLUDE_DIRS}
)

link_directories(
  ${TQT_LIBRARY_DIRS}
  ${TDE_LIB_DIR}
  ${PIPEWIRE_LIBRARY_DIRS}
  ${PULSE_LIBRARY_DIRS}
)


##### mixercommon (static) - backends and widgets, shared by everything

set( backend_SRCS backend/audiobackend.cpp )
if( HAVE_PIPEWIRE )
  list( APPEND backend_SRCS backend/pwbackend.cpp backend/pwspa.c )
endif( )
if( HAVE_PULSE )
  list( APPEND backend_SRCS backend/pulsebackend.cpp )
endif( )

tde_add_library( mixercommon STATIC_PIC AUTOMOC
  SOURCES
    ${backend_SRCS}
    mixersettings.cpp
    channelstrip.cpp
    mixerwidget.cpp
    settingspage.cpp
    listing.cpp
  LINK
    tdeui-shared
    tdecore-shared
    ${PIPEWIRE_LIBRARIES}
    ${PULSE_LIBRARIES}
)


##### kcm_mixer (Control Center module)

tde_add_kpart( kcm_mixer AUTOMOC
  SOURCES
    kcm_mixer.cpp
  LINK
    mixercommon-static
    tdeui-shared
    tdecore-shared
  DESTINATION ${PLUGIN_INSTALL_DIR}
)


##### tde-mixer (standalone application)

tde_add_executable( tde-mixer AUTOMOC
  SOURCES
    main.cpp
    mainwindow.cpp
  LINK
    mixercommon-static
    tdeio-shared
    tdeui-shared
    tdecore-shared
  DESTINATION ${BIN_INSTALL_DIR}
)

install( FILES tde-mixerui.rc DESTINATION ${DATA_INSTALL_DIR}/tde-mixer )


##### tde-mixer-tray (volume control in kicker's system tray)

tde_add_executable( tde-mixer-tray AUTOMOC
  SOURCES
    traymain.cpp
    mixertray.cpp
    traypopup.cpp
  LINK
    mixercommon-static
    tdeio-shared
    tdeui-shared
    tdecore-shared
  DESTINATION ${BIN_INSTALL_DIR}
)


##### desktop files

# Control Center: Sound & Multimedia; System Settings: Computer Administration
tde_create_translated_desktop(
  SOURCE mixer.desktop
  DESTINATION ${XDG_APPS_INSTALL_DIR}
)

tde_create_translated_desktop(
  SOURCE tde-mixer.desktop
  DESTINATION ${XDG_APPS_INSTALL_DIR}
)

tde_create_translated_desktop(
  SOURCE tde-mixer-tray.desktop
  DESTINATION ${XDG_APPS_INSTALL_DIR}
)

# started at login unless the user turns it off (see the
# X-TDE-autostart-condition in the file)
tde_create_translated_desktop(
  SOURCE tde-mixer-tray-autostart.desktop
  DESTINATION ${AUTOSTART_INSTALL_DIR}
)

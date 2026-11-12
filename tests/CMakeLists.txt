include_directories(
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/backend
  ${TDE_INCLUDE_DIR}
  ${TQT_INCLUDE_DIRS}
)

link_directories(
  ${TQT_LIBRARY_DIRS}
  ${TDE_LIB_DIR}
)

# not installed: run by tests/run-backend-tests.sh against a test sound server
tde_add_executable( backendtest AUTOMOC
  SOURCES backendtest.cpp
  LINK mixercommon-static tdecore-shared
)

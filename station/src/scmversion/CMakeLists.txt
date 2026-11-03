# _scmversion.cpp is a dummy file that will never be created, so the command will always be run
# Reader fork: the Windows build made on Linux runs the shell script.
if(CMAKE_HOST_WIN32)
  add_custom_command(OUTPUT scmversion.cpp _scmversion.cpp
                     COMMAND cmd /k "${CMAKE_CURRENT_SOURCE_DIR}/gen_scmversion.bat"
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
else()
  add_custom_command(OUTPUT scmversion.cpp _scmversion.cpp
                     COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/gen_scmversion.sh")
endif()

add_library(scmversion
  scmversion.cpp
  scmversion.h
)


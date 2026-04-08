cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.0.0/build_x64/libobs)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.0.0/build_x64/deps/w32-pthreads)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.0.0/build_x64/UI/obs-frontend-api)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# --- obs-multi-rtmp: clean rewrite layout ---
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  # core
  ./src/Plugin.cpp
  ./src/Logging.h
  ./src/ObsPtr.h
  ./src/Config.h
  ./src/Config.cpp
  ./src/Protocols.h
  ./src/Protocols.cpp
  ./src/OutputSession.h
  ./src/OutputSession.cpp
  # ui
  ./src/ui/Dock.h
  ./src/ui/Dock.cpp
  ./src/ui/TargetRow.h
  ./src/ui/TargetRow.cpp
  ./src/ui/PropertiesView.h
  ./src/ui/PropertiesView.cpp
  ./src/ui/EditDialog.h
  ./src/ui/EditDialog.cpp
)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ./dep/nlohmann-json
  .deps/obs-studio-31.0.0/libobs
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

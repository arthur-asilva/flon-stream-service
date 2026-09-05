cmake_minimum_required(VERSION 3.16)
project(zsafe_service LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Configuracao geral
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Gera o compile_commands.json que o clangd consome. Sem isso o autocomplete
# dos headers da Daheng nao funciona.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
# Dependencias
# -----------------------------------------------------------------------------
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)
find_package(Threads REQUIRED)

# --- nlohmann/json -----------------------------------------------------------
# Unica dependencia que nao veio via apt. Tenta achar um pacote instalado
# (ex: apt install nlohmann-json3-dev) e, se nao encontrar, baixa o header-only
# automaticamente via FetchContent. Nenhum passo manual e necessario nos dois
# cenarios.
find_package(nlohmann_json 3.11 QUIET)

if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json nao encontrado via find_package, baixando via FetchContent.")

    include(FetchContent)

    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )

    FetchContent_MakeAvailable(nlohmann_json)
else()
    message(STATUS "nlohmann_json encontrado via find_package (pacote do sistema).")
endif()

# --- Galaxy SDK (Daheng Imaging) ---------------------------------------------
# O instalador .run deixa as libs em /usr/lib e os headers na pasta extraida.
# Os HINTS cobrem varios cenarios: headers copiados para /usr/local/include
# ou ainda dentro de /opt/Galaxy_camera/inc, e a lib tanto em x86_64 (maquina
# de desenvolvimento) quanto nas variantes ARM do Raspberry Pi (nome exato do
# subdiretorio varia por versao do SDK - arm64, aarch64 e armv8 cobrem os
# nomes mais comuns, mas se nenhum bater, confira com
# 'find /opt/Galaxy_camera -name "libgxiapi*"' e passe o caminho certo via
# -DGALAXY_GXIAPI_LIBRARY=/caminho/exato/libgxiapi.so no cmake).
find_path(GALAXY_INCLUDE_DIR
    NAMES GxIAPI.h
    HINTS /usr/local/include
          /usr/include
          /opt/Galaxy_camera/inc
)

find_library(GALAXY_GXIAPI_LIBRARY
    NAMES gxiapi
    HINTS /usr/lib
          /usr/local/lib
          /opt/Galaxy_camera/lib/x86_64
          /opt/Galaxy_camera/lib/arm64
          /opt/Galaxy_camera/lib/aarch64
          /opt/Galaxy_camera/lib/armv8
)

if(NOT GALAXY_INCLUDE_DIR OR NOT GALAXY_GXIAPI_LIBRARY)
    message(FATAL_ERROR
        "Galaxy SDK nao encontrado.\n"
        "  Header (GxIAPI.h): ${GALAXY_INCLUDE_DIR}\n"
        "  Lib (libgxiapi):   ${GALAXY_GXIAPI_LIBRARY}\n"
        "Rode o Galaxy_camera.run e copie os headers:\n"
        "  sudo cp /opt/Galaxy_camera/inc/*.h /usr/local/include/\n"
        "Se a lib estiver em um caminho que nao bate com nenhum HINT\n"
        "(comum em variantes ARM/Raspberry Pi), confira o caminho real com\n"
        "  find /opt/Galaxy_camera -name 'libgxiapi*'\n"
        "e passe explicitamente:\n"
        "  cmake .. -DGALAXY_GXIAPI_LIBRARY=/caminho/exato/libgxiapi.so")
endif()

message(STATUS "Galaxy headers: ${GALAXY_INCLUDE_DIR}")
message(STATUS "Galaxy gxiapi:  ${GALAXY_GXIAPI_LIBRARY}")

add_library(Galaxy::gxiapi UNKNOWN IMPORTED)
set_target_properties(Galaxy::gxiapi PROPERTIES
    IMPORTED_LOCATION "${GALAXY_GXIAPI_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GALAXY_INCLUDE_DIR}"
)

# -----------------------------------------------------------------------------
# Fontes
# -----------------------------------------------------------------------------
# Lista explicita (em vez de file(GLOB...)) para que o CMake sempre reconfigure
# corretamente quando um arquivo novo for adicionado.
set(ZSAFE_SOURCES
    src/main.cpp
    src/libs/Camera/CameraCapture.cpp
    src/libs/Camera/GalaxyCamera.cpp
    src/libs/Frame/FrameProcessor.cpp
    src/libs/Frame/FrameStore.cpp
    src/libs/Image/ImageProcessor.cpp
    src/libs/Image/WellMasker.cpp
    src/libs/Logger/Logger.cpp
    src/libs/Record/RecordingManager.cpp
    src/libs/Server/MjpegServer.cpp
)

add_executable(zsafe_service ${ZSAFE_SOURCES})

# -----------------------------------------------------------------------------
# Includes
# -----------------------------------------------------------------------------
# Cada subpasta de libs/ entra no include path, assim os #include "Xyz.hpp"
# existentes (sem qualificar subpasta) continuam funcionando sem precisar
# reescrever nenhum arquivo de codigo.
target_include_directories(zsafe_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/Camera
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/Frame
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/Image
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/Logger
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/Record
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/Server
)

# -----------------------------------------------------------------------------
# Caminhos de dados (config/ e records/)
# -----------------------------------------------------------------------------
# PROJECT_ROOT_DIR aponta sempre para src/, independente de onde o binario
# for executado (build/, systemd, VSCode, terminal). WellMasker, CameraCapture
# e RecordingManager usam essa macro para montar os caminhos de config/wells_*.json
# e records/.
target_compile_definitions(zsafe_service PRIVATE
    PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# -----------------------------------------------------------------------------
# Link
# -----------------------------------------------------------------------------
target_link_libraries(zsafe_service PRIVATE
    Galaxy::gxiapi
    ${OpenCV_LIBS}
    Threads::Threads
    nlohmann_json::nlohmann_json
)

if(EXISTS "${GALAXY_INCLUDE_DIR}/DxImageProc.h")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_DXIMAGEPROC)
endif()

target_compile_options(zsafe_service PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release,RelWithDebInfo>:-O2>
)

# -----------------------------------------------------------------------------
# Instalacao (util quando virar servico systemd)
# -----------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS zsafe_service RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# -----------------------------------------------------------------------------
# Ferramentas de calibracao/diagnostico (nao fazem parte do servico ZSafe,
# mas reaproveitam a mesma deteccao de OpenCV/Galaxy SDK ja feita acima).
# -----------------------------------------------------------------------------

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Utils/TuneCam/tune_camera.cpp")
    add_executable(tune_camera Utils/TuneCam/tune_camera.cpp)

    target_link_libraries(tune_camera PRIVATE
        Galaxy::gxiapi
        ${OpenCV_LIBS}
        Threads::Threads
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Utils/DiagCameras/diag_cameras.cpp")
    add_executable(diag_cameras Utils/DiagCameras/diag_cameras.cpp)

    target_link_libraries(diag_cameras PRIVATE
        Galaxy::gxiapi
        Threads::Threads
    )
endif()

# Verovio (motor de gravação de partituras) como biblioteca estática.
set (VRV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/verovio")

file (GLOB VRV_SRC
    "${VRV_DIR}/src/*.cpp"
    "${VRV_DIR}/libmei/dist/*.cpp"
    "${VRV_DIR}/libmei/addons/*.cpp"
    "${VRV_DIR}/src/midi/*.cpp"
    "${VRV_DIR}/src/crc/*.cpp")

list (APPEND VRV_SRC
    "${VRV_DIR}/src/json/jsonxx.cc"
    "${VRV_DIR}/src/pugi/pugixml.cpp")

add_library (verovio_static STATIC ${VRV_SRC})

target_include_directories (verovio_static PUBLIC
    "${VRV_DIR}/include"
    "${VRV_DIR}/include/crc"
    "${VRV_DIR}/include/midi"
    "${VRV_DIR}/include/hum"
    "${VRV_DIR}/include/json"
    "${VRV_DIR}/include/pugi"
    "${VRV_DIR}/include/tuning-library"
    "${VRV_DIR}/include/zip"
    "${VRV_DIR}/include/vrv"
    "${VRV_DIR}/libmei/dist"
    "${VRV_DIR}/libmei/addons")

if (WIN32)
    target_include_directories (verovio_static PUBLIC "${VRV_DIR}/include/win32")
endif()

target_compile_definitions (verovio_static PUBLIC
    NO_HUMDRUM_SUPPORT NO_DARMS_SUPPORT NO_PAE_SUPPORT NO_ABC_SUPPORT NO_GABC_SUPPORT NO_RUNTIME
    RESOURCE_DIR="")

set_target_properties (verovio_static PROPERTIES CXX_STANDARD 20 POSITION_INDEPENDENT_CODE ON)

if (MSVC)
    target_compile_options (verovio_static PRIVATE /bigobj /W0 /utf-8)
    target_compile_definitions (verovio_static PUBLIC NOMINMAX _USE_MATH_DEFINES)
else()
    target_compile_options (verovio_static PRIVATE -w)
endif()

# Dados (fontes SMuFL) empacotados num zip e embutidos no app
set (VRV_DATA_ZIP "${CMAKE_CURRENT_BINARY_DIR}/verovio-data.zip")
add_custom_command (OUTPUT "${VRV_DATA_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E tar cf "${VRV_DATA_ZIP}" --format=zip
            Bravura Bravura.xml Leipzig Leipzig.xml text
    WORKING_DIRECTORY "${VRV_DIR}/data"
    DEPENDS "${VRV_DIR}/data/Bravura.xml" "${VRV_DIR}/data/Leipzig.xml"
    COMMENT "Empacotando fontes do Verovio")
add_custom_target (verovio_data DEPENDS "${VRV_DATA_ZIP}")

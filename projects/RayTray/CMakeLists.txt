cmake_minimum_required( VERSION 3.18.3 ) # Latest version of CMake when this file was created.

set( TARGET_NAME RayTray )

set( HEADER_FILES
    inc/DummyGame.h
)

set( SRC_FILES
    src/main.cpp
    src/DummyGame.cpp
)

set( COMPUTER_SHADER_FILES
    shaders/RayTracer.hlsl
)

set( VERTEX_SHADER_FILES 
    shaders/Vertex.hlsl
)

set( PIXEL_SHADER_FILES
    shaders/Pixel.hlsl
)

set( SHADER_FILES
    ${VERTEX_SHADER_FILES}
    ${PIXEL_SHADER_FILES}
    ${COMPUTER_SHADER_FILES}
)

source_group( "Resources\\Shaders" FILES ${SHADER_FILES} )

set_source_files_properties( ${SHADER_FILES}
    PROPERTIES
        VS_SHADER_OBJECT_FILE_NAME "${CMAKE_SOURCE_DIR}/Data/Shaders/${TARGET_NAME}/%(Filename).cso"
        VS_SHADER_MODEL 6.0
        VS_SHADER_DISABLE_OPTIMIZATIONS $<$<CONFIG:Debug>:ON>
        VS_SHADER_ENABLE_DEBUG $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:ON>
        VS_SHADER_FLAGS $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-Qembed_debug>
)

set_source_files_properties( ${VERTEX_SHADER_FILES}
    PROPERTIES 
        VS_SHADER_TYPE Vertex
)

set_source_files_properties( ${PIXEL_SHADER_FILES}
    PROPERTIES 
        VS_SHADER_TYPE Pixel
)

set_source_files_properties( ${COMPUTER_SHADER_FILES}
    PROPERTIES 
        VS_SHADER_TYPE Compute
)

add_executable( ${TARGET_NAME} WIN32
    ${HEADER_FILES} 
    ${SRC_FILES}
    ${SHADER_FILES}
)

target_include_directories( ${TARGET_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries( ${TARGET_NAME}
    GameFramework
    DX12Lib
    d3dcompiler.lib
    Shlwapi.lib
)

# Set debug database to enable the "Edit and Continue" feature.
target_compile_options( ${TARGET_NAME}
    PRIVATE $<$<CONFIG:Debug>:/ZI>
)

# Set Local Debugger Settings (Command Arguments and Environment Variables)
set( COMMAND_ARGUMENTS "-wd \"${CMAKE_SOURCE_DIR}\"" )
configure_file( ${TARGET_NAME}.vcxproj.user.in ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.vcxproj.user @ONLY )


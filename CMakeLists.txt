cmake_minimum_required(VERSION 3.20)
project(DriverBuilder C ASM_NASM)

enable_language(ASM_NASM)

set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
set(CMAKE_ASM_NASM_FLAGS_RELEASE "-O2")
set(CMAKE_ASM_NASM_FLAGS_DEBUG "-g -F cv8")

# ===== WDK =====
set(WDK_PATH "C:/Program Files (x86)/Windows Kits/10")
set(WDK_VERSION "10.0.26100.0")
set(WDK_INCLUDE_PATH "${WDK_PATH}/Include/${WDK_VERSION}")
set(WDK_LIB_PATH "${WDK_PATH}/Lib/${WDK_VERSION}/km/x64")

# ===== ASM OBJECTS =====
set(ASM_SOURCES
    virt2phys.asm
    memops.asm
)

set(ASM_OBJS "")
foreach(f ${ASM_SOURCES})
    get_filename_component(fname ${f} NAME_WE)
    set(obj "${CMAKE_CURRENT_BINARY_DIR}/${fname}.obj")
    add_custom_command(
        OUTPUT ${obj}
        COMMAND ${CMAKE_ASM_NASM_COMPILER} -f win64 ${CMAKE_ASM_NASM_FLAGS_RELEASE} -o ${obj} ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        DEPENDS ${f}
        COMMENT "Assembling ${f}"
    )
    list(APPEND ASM_OBJS ${obj})
endforeach()

# ===== DRIVER C =====
add_executable(driver driver.c ${ASM_OBJS})

target_compile_options(driver PRIVATE
    /kernel /GS- /W3 /O2 /Oi /Oy- /Zp8
    /D_WIN64 /D_AMD64_ /DAMD64 /D_KERNEL_MODE
)

target_include_directories(driver PRIVATE
    "${WDK_INCLUDE_PATH}/km"
    "${WDK_INCLUDE_PATH}/km/crt"
    "${WDK_INCLUDE_PATH}/shared"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(driver PROPERTIES
    SUFFIX ".sys"
    LINK_FLAGS "/DRIVER /ENTRY:DriverEntry /SUBSYSTEM:NATIVE /NODEFAULTLIB /MANIFEST:NO /INTEGRITYCHECK"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/output/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/output/Release"
)

target_link_libraries(driver
    "${WDK_LIB_PATH}/ntoskrnl.lib"
    "${WDK_LIB_PATH}/hal.lib"
)

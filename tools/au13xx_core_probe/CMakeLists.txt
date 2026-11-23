cmake_minimum_required(VERSION 3.25)
project(au13xx_core_probe LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This development host is validated on Linux only")
endif()

set(CERFCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../libCerfCore" core)

foreach(name IN ITEMS au1374_core_host au1374_core_integration_test
                      au1374_core_display_test au1374_core_sd_test au1374_core_audio_test
                      au1374_core_sdhc_test au1374_core_sensor_test)
    add_executable(${name} "${name}.cpp")
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(${name} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,cerfcore::cerfcore>")
endforeach()

foreach(name IN ITEMS db.json huds.json)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../libCerfCore/${name}"
                   "${CMAKE_CURRENT_BINARY_DIR}/${name}" COPYONLY)
endforeach()

enable_testing()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME synthetic_nand_paths COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../au13xx_startup_probe/synthetic_nand_test.py")
foreach(name IN ITEMS integration display sd audio sensor)
    add_test(NAME "au1374_core_${name}" COMMAND "au1374_core_${name}_test"
        --board-id=inavi_gn2010 --disable-network)
endforeach()

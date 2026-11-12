include(ExternalProject)
set(EGOTOUCH_DRIVER_JOBS 2 CACHE STRING "Parallel compiler jobs for EGoTouchRev")
find_program(EGOTOUCH_CLANG_CL NAMES clang-cl.exe clang-cl
    HINTS "$ENV{VSINSTALLDIR}/VC/Tools/Llvm/ARM64/bin"
          "$ENV{VSINSTALLDIR}/VC/Tools/Llvm/bin"
          "C:/Program Files/LLVM/bin" REQUIRED)
set(EGOTOUCH_DRIVER_DIR "${CMAKE_BINARY_DIR}/egotouch-driver")
set(_ego_core_test_target "")
set(_ego_core_test_source "")
if(EGO_BUILD_TESTS)
    set(_ego_core_test_target EGoTouchCoreTapTest EGoTouchAdapterTest)
    set(_ego_core_test_source "${CMAKE_SOURCE_DIR}/EGoTouchService/tests/unit/TsaOutputTapIntegrationTest.cpp")
endif()
ExternalProject_Add(EGoTouchRevDriver
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/EGoTouchRev"
    BINARY_DIR "${EGOTOUCH_DRIVER_DIR}"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER:FILEPATH=${EGOTOUCH_CLANG_CL}
        -DCMAKE_CXX_COMPILER:FILEPATH=${EGOTOUCH_CLANG_CL}
        -DCMAKE_C_COMPILER_TARGET=aarch64-pc-windows-msvc
        -DCMAKE_CXX_COMPILER_TARGET=aarch64-pc-windows-msvc
        -DEGO_GENERATE_PDB:BOOL=${EGO_GENERATE_PDB}
        -DEGO_BUILD_TOOLS=OFF -DHIMAX_ENABLE_NEON=ON -DEGO_HEADLESS_SERVICE=ON
        -DEGO_HUB_TEST_SOURCE:FILEPATH=${_ego_core_test_source}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target EGoTouchService ${_ego_core_test_target} --parallel ${EGOTOUCH_DRIVER_JOBS}
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different
        <BINARY_DIR>/EGoTouchService.exe ${CMAKE_BINARY_DIR}/EGoTouchDriver.exe
    BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/EGoTouchDriver.exe")
add_dependencies(EGoTouchService EGoTouchRevDriver)
install(FILES "${CMAKE_BINARY_DIR}/EGoTouchDriver.exe" DESTINATION "." COMPONENT Runtime)
install(FILES "${CMAKE_SOURCE_DIR}/third_party/EGoTouchRev/LICENSE"
    DESTINATION "licenses/EGoTouchRev" COMPONENT Runtime)

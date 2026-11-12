# Deterministic instruction-count probe (ADR-0011). See tests/perf_probe.cpp.
#
# There is no library target: CORE_SOURCES are compiled into each executable.
# Keep the include/link setup in sync with CellEvoXTests.
#
# Deliberately does not use cellevox_apply_release_optimizations(): that helper
# adds -march=native when CELLEVOX_NATIVE_OPTIMIZATIONS is ON. Callgrind may
# reject native-only opcodes, and retired-instruction totals are only
# comparable within one compiler/valgrind pair, so the probe stays portable.

add_executable(CellEvoXPerfProbe
    ${CORE_SOURCES}
    tests/perf_probe.cpp
)

target_include_directories(CellEvoXPerfProbe
    PRIVATE
    include
    include/spatial
)
target_include_directories(CellEvoXPerfProbe SYSTEM
    PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${NUMPY_INCLUDE_DIR}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(CellEvoXPerfProbe PRIVATE -O3)
    if(CELLEVOX_FAST_MATH)
        target_compile_options(CellEvoXPerfProbe PRIVATE -ffast-math)
    endif()
elseif(MSVC)
    target_compile_options(CellEvoXPerfProbe PRIVATE /O2)
endif()

cellevox_apply_project_warnings(CellEvoXPerfProbe)
target_compile_definitions(CellEvoXPerfProbe PRIVATE ${CELLEVOX_RUNTIME_DEFINITIONS})

target_link_libraries(CellEvoXPerfProbe
    PRIVATE
    spdlog::spdlog
    Eigen3::Eigen
    TBB::tbb
    nlohmann_json::nlohmann_json
    ${Python3_LIBRARIES}
)

if(WIN32)
    target_link_libraries(CellEvoXPerfProbe PRIVATE Psapi)
endif()

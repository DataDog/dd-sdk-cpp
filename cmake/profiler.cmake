include(FetchContent)

set(DD_WIN_PROF_SOURCE_DIR "" CACHE PATH
    "Local path to dd-win-prof source tree (overrides GitHub fetch)"
)

if(DD_WIN_PROF_SOURCE_DIR)
    FetchContent_Declare(dd-win-prof SOURCE_DIR "${DD_WIN_PROF_SOURCE_DIR}")
else()
    FetchContent_Declare(
        dd-win-prof
        GIT_REPOSITORY https://github.com/DataDog/dd-win-prof.git
        GIT_TAG 5f8884c614125306bb402d7e408c5bdf53144b4b
    )
endif()

FetchContent_MakeAvailable(dd-win-prof)
target_link_libraries(ddsdkcpp PUBLIC dd-win-prof)

if(DD_BUILD_INSTALL)
    install(TARGETS dd-win-prof
        EXPORT DatadogTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES
        "${dd-win-prof_SOURCE_DIR}/src/dd-win-prof/dd-win-prof.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES
        "$<TARGET_FILE:libdatadog_dynamic>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

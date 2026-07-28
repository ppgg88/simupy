
include(GNUInstallDirs)

install(TARGETS simupy-cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT runtime)

if(SIMUPY_BUILD_GUI)
    install(TARGETS simupy
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            BUNDLE DESTINATION .
            COMPONENT runtime)
endif()

install(DIRECTORY ${PROJECT_SOURCE_DIR}/examples/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/simupy/examples
        COMPONENT runtime
        FILES_MATCHING PATTERN "*.spy")

install(DIRECTORY ${PROJECT_SOURCE_DIR}/libraries/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/simupy/libraries
        COMPONENT runtime
        FILES_MATCHING PATTERN "*.spylib")

install(DIRECTORY ${PROJECT_SOURCE_DIR}/firmware/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/simupy/firmware
        COMPONENT runtime)

install(PROGRAMS ${PROJECT_SOURCE_DIR}/tools/fake_arduino.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/simupy/tools
        COMPONENT runtime)

install(FILES ${PROJECT_SOURCE_DIR}/README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        COMPONENT runtime)

# Flatpak refuses to export a file not named after the application ID.
set(SIMUPY_APP_ID "io.github.ppgg88.SimuPy")

if(UNIX AND NOT APPLE AND SIMUPY_BUILD_GUI)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/${SIMUPY_APP_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
            COMPONENT runtime)

    install(FILES ${PROJECT_SOURCE_DIR}/packaging/${SIMUPY_APP_ID}.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
            COMPONENT runtime)

    # shared-mime-info expects the bare ID with no second extension.
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/${SIMUPY_APP_ID}.mime.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
            RENAME ${SIMUPY_APP_ID}.xml
            COMPONENT runtime)

    install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/${SIMUPY_APP_ID}.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            COMPONENT runtime)
endif()

# windeployqt has to run on the installed exe: CPack packages the install tree.
if(WIN32)
    if(SIMUPY_BUILD_GUI)
        find_program(SIMUPY_WINDEPLOYQT windeployqt
                     HINTS "${Qt6_DIR}/../../../bin" "$ENV{QTDIR}/bin")
        # Missing windeployqt breaks packaging only, so it is fatal at install time.
        if(NOT SIMUPY_WINDEPLOYQT)
            message(WARNING "windeployqt not found: packaging will fail")
            install(CODE [[message(FATAL_ERROR
                "windeployqt not found: the package would ship simupy.exe "
                "without the Qt runtime and fail to start")]]
                    COMPONENT runtime)
        endif()
        install(CODE "
            set(_windeployqt \"${SIMUPY_WINDEPLOYQT}\")
            set(_exe \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/simupy.exe\")"
                COMPONENT runtime)
        install(CODE [[
            execute_process(
                COMMAND "${_windeployqt}" --release --no-translations
                        --no-system-d3d-compiler "${_exe}"
                RESULT_VARIABLE _status)
            if(NOT _status EQUAL 0)
                message(FATAL_ERROR "windeployqt failed on ${_exe}")
            endif()]]
                COMPONENT runtime)
    endif()

    # Both executables embed CPython, so its DLL travels with them.
    get_filename_component(_python_dir "${Python3_EXECUTABLE}" DIRECTORY)
    file(GLOB SIMUPY_PYTHON_DLL "${_python_dir}/python3?.dll"
                                "${_python_dir}/python3??.dll")
    if(SIMUPY_PYTHON_DLL)
        install(FILES ${SIMUPY_PYTHON_DLL}
                DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    else()
        message(WARNING "no pythonXY.dll beside ${Python3_EXECUTABLE}")
        install(CODE [[message(FATAL_ERROR
            "no pythonXY.dll found: the package would ship without an "
            "interpreter and fail to start")]] COMPONENT runtime)
    endif()
endif()

set(CPACK_PACKAGE_NAME "simupy")
set(CPACK_PACKAGE_VENDOR "SimuPy")
set(CPACK_PACKAGE_VERSION ${SIMUPY_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Block-diagram simulation with custom blocks written in Python")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SimuPy")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_FILE_NAME
    "simupy-${SIMUPY_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(NOT WIN32)
    set(CPACK_STRIP_FILES ON)
endif()
set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "SimuPy")
    set(CPACK_NSIS_DISPLAY_NAME "SimuPy ${SIMUPY_VERSION}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON)
    if(SIMUPY_BUILD_GUI)
        set(CPACK_NSIS_MENU_LINKS "bin/simupy.exe" "SimuPy")
    endif()
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "SimuPy")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    # Declared rather than shipped: the application embeds CPython.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "python3 (>= 3.8), python3-numpy")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)

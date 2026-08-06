
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

install(FILES ${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        COMPONENT runtime)

# Flatpak refuses to export a file not named after the application ID.

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
        else()
            # Collected once at build time into a directory of its own, rather
            # than run against the installed exe. Installing is then a plain
            # copy, which is the same for every generator: driving windeployqt
            # from install(CODE) meant it also ran once per CPack component,
            # against a prefix that only holds the exe on some of those passes.
            set(SIMUPY_QT_RUNTIME "${CMAKE_BINARY_DIR}/qt-runtime")
            set(SIMUPY_QT_STAMP "${CMAKE_BINARY_DIR}/qt-runtime.stamp")

            add_custom_command(
                OUTPUT "${SIMUPY_QT_STAMP}"
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${SIMUPY_QT_RUNTIME}"
                COMMAND "${SIMUPY_WINDEPLOYQT}" --release --no-translations
                        --no-system-d3d-compiler
                        --dir "${SIMUPY_QT_RUNTIME}" "$<TARGET_FILE:simupy>"
                COMMAND ${CMAKE_COMMAND} -E touch "${SIMUPY_QT_STAMP}"
                DEPENDS simupy
                COMMENT "Collecting the Qt runtime for packaging")

            add_custom_target(simupy-qt-runtime ALL
                              DEPENDS "${SIMUPY_QT_STAMP}")

            install(DIRECTORY "${SIMUPY_QT_RUNTIME}/"
                    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
        endif()
    endif()

    # Windows hands these back with backslashes, which are escape characters
    # to CMake: left raw they turn the generated install script into a syntax
    # error the moment a path holds something like \h or \x.
    file(TO_CMAKE_PATH "${Python3_STDLIB}" Python3_STDLIB)
    file(TO_CMAKE_PATH "${Python3_SITELIB}" Python3_SITELIB)

    # Both executables embed CPython, so its DLL travels with them.
    get_filename_component(_python_dir "${Python3_EXECUTABLE}" DIRECTORY)
    file(TO_CMAKE_PATH "${_python_dir}" _python_dir)
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

    # The DLL alone is an interpreter with nothing to run. Linux packages lean
    # on the distribution's python3; Windows has nothing to lean on, so the
    # standard library, its extension modules and NumPy all ship with us.
    option(SIMUPY_BUNDLE_PYTHON
           "Ship the Python runtime inside the Windows package" ON)

    if(SIMUPY_BUNDLE_PYTHON)
        if(NOT Python3_STDLIB OR NOT EXISTS "${Python3_STDLIB}")
            message(FATAL_ERROR
                "Python3_STDLIB (${Python3_STDLIB}) does not exist, so the "
                "package would ship an interpreter with no standard library")
        endif()

        # Cuts roughly half the payload and none of it is reachable from here.
        install(DIRECTORY "${Python3_STDLIB}/"
                DESTINATION ${CMAKE_INSTALL_BINDIR}/Lib
                COMPONENT runtime
                PATTERN "__pycache__" EXCLUDE
                PATTERN "site-packages" EXCLUDE
                PATTERN "test" EXCLUDE
                PATTERN "tests" EXCLUDE
                PATTERN "idlelib" EXCLUDE
                PATTERN "tkinter" EXCLUDE
                PATTERN "turtledemo" EXCLUDE
                PATTERN "ensurepip" EXCLUDE)

        # _socket.pyd and friends: without these the UDP and serial blocks have
        # no transport, and several standard modules fail to import at all.
        if(EXISTS "${_python_dir}/DLLs")
            install(DIRECTORY "${_python_dir}/DLLs/"
                    DESTINATION ${CMAKE_INSTALL_BINDIR}/DLLs
                    COMPONENT runtime
                    PATTERN "__pycache__" EXCLUDE
                    PATTERN "tcl*" EXCLUDE
                    PATTERN "tk*" EXCLUDE
                    PATTERN "_tkinter.pyd" EXCLUDE
                    PATTERN "_test*" EXCLUDE)
        else()
            message(FATAL_ERROR
                "no DLLs directory beside ${Python3_EXECUTABLE}: the package "
                "would ship without the standard extension modules")
        endif()

        # NumPy is required to start; pyserial only by the hardware blocks, so
        # it is taken when present rather than demanded.
        foreach(_package numpy serial)
            if(EXISTS "${Python3_SITELIB}/${_package}")
                install(DIRECTORY "${Python3_SITELIB}/${_package}"
                        DESTINATION ${CMAKE_INSTALL_BINDIR}/Lib/site-packages
                        COMPONENT runtime
                        PATTERN "__pycache__" EXCLUDE
                        PATTERN "tests" EXCLUDE)

                # Wheels repaired by delvewheel keep their native dependencies
                # in a sibling directory; the package will not import without.
                if(EXISTS "${Python3_SITELIB}/${_package}.libs")
                    install(DIRECTORY "${Python3_SITELIB}/${_package}.libs"
                            DESTINATION
                                ${CMAKE_INSTALL_BINDIR}/Lib/site-packages
                            COMPONENT runtime)
                endif()
            elseif(_package STREQUAL "numpy")
                message(FATAL_ERROR
                    "NumPy is not in ${Python3_SITELIB}: the interpreter would "
                    "fail on startup. pip install numpy, or configure with "
                    "-DSIMUPY_BUNDLE_PYTHON=OFF")
            else()
                message(STATUS "  pyserial: absent, serial blocks will not run")
            endif()
        endforeach()

        # Beside the DLL, this pins sys.path to what we ship. Without it an
        # embedded interpreter searches the registry and PATH, and starts
        # against whatever unrelated Python the machine happens to have.
        list(GET SIMUPY_PYTHON_DLL 0 _python_dll)
        get_filename_component(_dll_name "${_python_dll}" NAME_WE)
        set(SIMUPY_PTH "${CMAKE_CURRENT_BINARY_DIR}/${_dll_name}._pth")
        file(WRITE "${SIMUPY_PTH}"
             "Lib\nDLLs\nLib/site-packages\n.\nimport site\n")
        install(FILES "${SIMUPY_PTH}"
                DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    endif()

    # vcruntime and friends, or a clean machine refuses to start the exe.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT runtime)
    include(InstallRequiredSystemLibraries)
endif()

set(CPACK_PACKAGE_NAME "simupy")
set(CPACK_PACKAGE_VENDOR "SimuPy")
set(CPACK_PACKAGE_VERSION ${SIMUPY_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Block-diagram simulation with custom blocks written in Python")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SimuPy")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME
    "simupy-${SIMUPY_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(NOT WIN32)
    set(CPACK_STRIP_FILES ON)
endif()
set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
    # One product, one component: the split only gave NSIS a component chooser
    # nobody wants and a staging layout to get wrong. Windows-only, so the
    # Debian and tarball packages keep the behaviour they already had.
    set(CPACK_MONOLITHIC_INSTALL ON)

    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "SimuPy")
    set(CPACK_NSIS_DISPLAY_NAME "SimuPy ${SIMUPY_VERSION}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON)
    # NSIS wants a native path, and the same icon in three places: the
    # installer and uninstaller windows, and the Add/Remove Programs entry.
    file(TO_NATIVE_PATH
         "${PROJECT_SOURCE_DIR}/resources/icons/io.github.ppgg88.SimuPy.ico"
         SIMUPY_NSIS_ICON)
    set(CPACK_NSIS_MUI_ICON "${SIMUPY_NSIS_ICON}")
    set(CPACK_NSIS_MUI_UNIICON "${SIMUPY_NSIS_ICON}")

    if(SIMUPY_BUILD_GUI)
        set(CPACK_NSIS_MENU_LINKS "bin/simupy.exe" "SimuPy")
        set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\simupy.exe")
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

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Charts)

find_package(Qt6 6.2 QUIET COMPONENTS Svg)

if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
endif()

qt_add_library(simupy-gui STATIC
    MainWindow.cpp
    NumberInput.cpp
    SimulationController.cpp
    UndoHistory.cpp

    style/Theme.cpp
    style/AppIcons.cpp
    style/BlockIconRenderer.cpp

    canvas/DiagramScene.cpp
    canvas/DiagramView.cpp
    canvas/BlockItem.cpp
    canvas/WireItem.cpp
    canvas/WireRouter.cpp
    canvas/QuickAddPopup.cpp

    panels/BlockLibraryPanel.cpp
    panels/PropertyPanel.cpp
    panels/ControlPanel.cpp
    panels/ConsoleDock.cpp

    dialogs/BlockInspector.cpp
    dialogs/CustomBlockDialog.cpp
    dialogs/LibraryManagerDialog.cpp
    dialogs/SolverSettingsDialog.cpp
    dialogs/ScopeWindow.cpp
    dialogs/PackageRequirementsDialog.cpp
    dialogs/PythonEditor.cpp
    dialogs/PythonHighlighter.cpp
)
add_library(SimuPy::simupy-gui ALIAS simupy-gui)

target_include_directories(simupy-gui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(simupy-gui PUBLIC
    SimuPy::simupy-engine
    SimuPy::simupy-io
    SimuPy::simupy-blocks
    SimuPy::simupy-scripting
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Charts)

if(Qt6Svg_FOUND)
    target_link_libraries(simupy-gui PUBLIC Qt6::Svg)
    target_compile_definitions(simupy-gui PUBLIC SIMUPY_HAVE_SVG)
endif()

target_compile_options(simupy-gui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>)

qt_add_executable(simupy main.cpp)
target_link_libraries(simupy PRIVATE SimuPy::simupy-gui)
set_target_properties(simupy PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

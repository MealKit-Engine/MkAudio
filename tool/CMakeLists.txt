# MkAudio authoring tool (MkAudio): SDL3 window + Dear ImGui (docking) + the mkaudio runtime. Build output goes to <build folder>/bin/
# with the executable and the manifest mealkit-tool.txt together → put that folder in %LOCALAPPDATA%\MealKit\tools\audio\, or
# open the MealKit editor with --tool-path <build folder>/bin and it appears in the [Tools] menu. cmake --install makes the same folder shape.

# SDL3 (static, without tests/examples)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL ${CMAKE_CURRENT_BINARY_DIR}/SDL EXCLUDE_FROM_ALL)

set(imgui_dir ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/imgui)
add_library(mkaudio_imgui STATIC
    ${imgui_dir}/imgui.cpp ${imgui_dir}/imgui_draw.cpp ${imgui_dir}/imgui_tables.cpp ${imgui_dir}/imgui_widgets.cpp
    ${imgui_dir}/backends/imgui_impl_sdl3.cpp ${imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(mkaudio_imgui PUBLIC ${imgui_dir} ${imgui_dir}/backends)
target_link_libraries(mkaudio_imgui PUBLIC SDL3::SDL3-static)

# Tool contract (MealKit-Contract, copied into third_party/mkcontract with that repo's tools/vendor.ps1): live connection info + client
set(MKCONTRACT_BUILD_TESTS OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/mkcontract ${CMAKE_CURRENT_BINARY_DIR}/mkcontract)

add_executable(MkAudio WIN32 main.cpp App.h App.cpp Strings.h Strings.cpp)
target_link_libraries(MkAudio PRIVATE mkaudio mkaudio_imgui mkcontract_client SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(MkAudio PRIVATE dwmapi) # editor-colored title bar
endif()
if(MSVC)
    target_compile_options(MkAudio PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MkAudio PRIVATE -Wall -Wextra)
endif()
set_target_properties(MkAudio PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(MKAUDIO_BUILD_TESTS)
    # Every tr() text of the app has its Korean line (engine docs/TOOL_DESIGN.md section 4)
    add_executable(mkaudio_strings_test ../tests/StringsTest.cpp Strings.cpp)
    target_include_directories(mkaudio_strings_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(mkaudio_strings_test PRIVATE mkcontract)
    target_compile_definitions(mkaudio_strings_test PRIVATE TOOL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(mkaudio_strings_test PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME mkaudio_strings_test COMMAND mkaudio_strings_test)
endif()
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} upper)
    set_target_properties(MkAudio PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} ${CMAKE_BINARY_DIR}/bin)
endforeach()

# Manifest (version from project(mkaudio VERSION ...)). exe is written without an extension, .exe is appended on Windows
set(MKAUDIO_VERSION ${PROJECT_VERSION})
configure_file(mealkit-tool.txt ${CMAKE_BINARY_DIR}/bin/mealkit-tool.txt @ONLY)
if(WIN32)
    file(READ ${CMAKE_BINARY_DIR}/bin/mealkit-tool.txt manifest)
    string(REPLACE "exe MkAudio\n" "exe MkAudio.exe\n" manifest "${manifest}")
    file(WRITE ${CMAKE_BINARY_DIR}/bin/mealkit-tool.txt "${manifest}")
endif()

install(TARGETS MkAudio RUNTIME DESTINATION .)
# Icon font (engine docs/TOOL_DESIGN.md section 2): read from beside the executable, so it is copied there and installed with it
add_custom_command(TARGET MkAudio POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/fluent-icons/FluentSystemIcons-Regular.ttf $<TARGET_FILE_DIR:MkAudio>)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/fluent-icons/FluentSystemIcons-Regular.ttf DESTINATION .)
install(FILES ${CMAKE_BINARY_DIR}/bin/mealkit-tool.txt DESTINATION .)

if(WIN32)
    target_sources(MkAudio PRIVATE res/MkAudio.rc) # app icon (engine docs/TOOL_DESIGN.md section 5)
    # rc.exe reports no dependencies: without this a regenerated icon never reaches the exe
    set_source_files_properties(res/MkAudio.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/MkAudio.ico")
endif()

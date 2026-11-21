# imgcat2 command line tool: imgcat2_cli (everything but main.c; also linked by the CLI tests) + imgcat2

set(_cli_src cli.c pipeline.c metadata.c base64.c ansi/ansi.c ansi/escape.c terminal/file.c)
if(WIN32)
	list(APPEND _cli_src terminal/terminal_windows.c)
else()
	list(APPEND _cli_src terminal/terminal_unix.c terminal/iterm2.c terminal/kitty.c)
endif()
add_library(imgcat2_cli STATIC ${_cli_src})
target_include_directories(imgcat2_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imgcat2_cli PUBLIC img2)   # public <img2/...> API only (was img2_testing)
target_compile_definitions(imgcat2_cli PRIVATE IMGCAT2_VERSION_STRING="${PROJECT_VERSION}")
img2_apply_project_settings(imgcat2_cli)

if(IMG2_BUILD_CLI)
	add_executable(imgcat2 main.c)
	target_link_libraries(imgcat2 PRIVATE imgcat2_cli)
	set_target_properties(imgcat2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")   # keeps build/imgcat2
	img2_apply_project_settings(imgcat2)
	img2_static_executable(imgcat2)
	# [SL2] Full RELRO (exe only; the static non-PIE link keeps working)
	target_link_options(imgcat2 PRIVATE $<$<PLATFORM_ID:Linux>:-Wl,-z,relro,-z,now>)
	install(TARGETS imgcat2 RUNTIME DESTINATION bin)   # replaced in S9
endif()

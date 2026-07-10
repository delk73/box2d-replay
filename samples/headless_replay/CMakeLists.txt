add_executable(headless_replay
	main.c
	replay_parse.c
	replay_parse.h
	scenarios/falling_box_v0.replay
	scenarios/sliding_box_v0.replay
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set_target_properties(headless_replay PROPERTIES
	C_STANDARD 17
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO
)

if(BOX2D_COMPILE_WARNING_AS_ERROR)
	set_target_properties(headless_replay PROPERTIES COMPILE_WARNING_AS_ERROR ON)
endif()

target_link_libraries(headless_replay PRIVATE box2d)

function(add_retained_replay_check case_name)
	set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
	set(generated_witness "${generated_dir}/${case_name}.generated.witness")
	set(expected_witness "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/${case_name}.expected.witness")
	set(scenario_file "${CMAKE_CURRENT_SOURCE_DIR}/scenarios/${case_name}.replay")

	add_custom_target(replay_check_${case_name}
		DEPENDS
			headless_replay
			"${scenario_file}"
			"${CMAKE_CURRENT_SOURCE_DIR}/check_witness.py"
			"${expected_witness}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${generated_dir}"
		COMMAND "$<TARGET_FILE:headless_replay>" "${scenario_file}" "${generated_witness}"
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_witness.py" "${generated_witness}"
		COMMAND ${CMAKE_COMMAND} -E compare_files "${generated_witness}" "${expected_witness}"
		VERBATIM
	)
endfunction()

add_retained_replay_check(falling_box_v0)
add_retained_replay_check(sliding_box_v0)

add_custom_target(replay_check
	DEPENDS
		replay_check_falling_box_v0
		replay_check_sliding_box_v0
)

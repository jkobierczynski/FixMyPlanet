set(FIXMYPLANET_TESTS
  test_time
  test_ephem
  test_geometry
  test_imageio
  test_json_session
  test_measure
  test_derotate)

foreach(t ${FIXMYPLANET_TESTS})
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE fixmyplanet_core)
  target_compile_definitions(${t} PRIVATE FIXMYPLANET_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# End-to-end run of the command-line tool on synthetic data.
add_test(NAME cli_end_to_end
  COMMAND ${CMAKE_COMMAND}
    -DFIXMYPLANET=$<TARGET_FILE:fixmyplanet>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_end_to_end.cmake)

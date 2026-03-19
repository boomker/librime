aux_source_directory(. rime_test_src)
set(predict_test_src ${CMAKE_CURRENT_SOURCE_DIR}/predict_test.cc)
list(FILTER rime_test_src EXCLUDE REGEX "(^|.*/)predict_test\\.cc$")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/test)
add_executable(rime_test ${rime_test_src})

if(TARGET rime-predict-objs)
  target_sources(rime_test PRIVATE
    ${predict_test_src}
    $<TARGET_OBJECTS:rime-predict-objs>)
  target_include_directories(rime_test PRIVATE
    ${PROJECT_SOURCE_DIR}/plugins/predict/src
    ${PROJECT_SOURCE_DIR}/plugins/predict/deps/msgpack-c/include)
endif()

target_link_libraries(rime_test
  ${rime_library}
  ${rime_dict_library}
  ${rime_gears_library}
  ${rime_plugins_library}
  ${GTEST_LIBRARIES})

if(TARGET rime-predict-objs)
  target_link_libraries(rime_test ${rime_plugins_deps})
endif()

if(BUILD_SHARED_LIBS)
  target_compile_definitions(rime_test PRIVATE RIME_IMPORTS)
endif(BUILD_SHARED_LIBS)

file(GLOB test_data_files ${PROJECT_SOURCE_DIR}/data/test/*.yaml)
file(COPY ${test_data_files} DESTINATION ${EXECUTABLE_OUTPUT_PATH})

add_test(NAME rime_test
  COMMAND rime_test
  WORKING_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

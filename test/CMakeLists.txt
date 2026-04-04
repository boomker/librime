aux_source_directory(. rime_test_src)
set(predict_test_src ${CMAKE_CURRENT_SOURCE_DIR}/predict_test.cc)
list(FILTER rime_test_src EXCLUDE REGEX "(^|.*/)predict_test\\.cc$")
set(enable_predict_tests FALSE)
if(TARGET rime-predict-objs AND NOT WIN32)
  set(enable_predict_tests TRUE)
endif()

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/test)
add_executable(rime_test ${rime_test_src})

if(enable_predict_tests)
  find_library(SQLITE3_LIBRARY sqlite3)
  target_sources(rime_test PRIVATE
    ${predict_test_src}
    $<TARGET_OBJECTS:rime-predict-objs>)
  target_include_directories(rime_test PRIVATE
    ${PROJECT_SOURCE_DIR}/plugins/predict/src
    ${PROJECT_SOURCE_DIR}/plugins/predict/deps/msgpack-c/include)
  if(SQLITE3_LIBRARY)
    target_link_libraries(rime_test ${SQLITE3_LIBRARY})
  endif()
endif()

target_link_libraries(rime_test
  ${rime_library}
  ${rime_dict_library}
  ${rime_gears_library}
  ${rime_plugins_library}
  ${GTEST_LIBRARIES})

if(enable_predict_tests)
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

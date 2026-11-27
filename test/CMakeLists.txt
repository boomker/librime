file(GLOB rime_test_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

# predict_test.cc needs the predict plugin's msgpack headers, so keep it out of
# the glob and only add it when the plugin objects are available. Upstream's
# build refactor dropped this setup while leaving the `enable_predict_tests`
# block below, which made the glob compile predict_test.cc unconditionally and
# fail on a missing <msgpack.hpp>.
set(predict_test_src ${CMAKE_CURRENT_SOURCE_DIR}/predict_test.cc)
list(FILTER rime_test_src EXCLUDE REGEX "(^|.*/)predict_test\\.cc$")
set(enable_predict_tests FALSE)
if(TARGET rime-predict-objs AND NOT WIN32)
  set(enable_predict_tests TRUE)
endif()

set(rime_test_output_dir ${PROJECT_BINARY_DIR}/test)

if(TARGET GTest::gtest)
  set(rime_gtest_target GTest::gtest)
else()
  set(rime_gtest_target ${GTEST_LIBRARIES})
endif()

add_executable(rime_test ${rime_test_src})

if(enable_predict_tests)
  target_sources(rime_test PRIVATE
    ${predict_test_src}
    $<TARGET_OBJECTS:rime-predict-objs>)
  target_include_directories(rime_test PRIVATE
    ${PROJECT_SOURCE_DIR}/plugins/predict/src
    ${PROJECT_SOURCE_DIR}/plugins/predict/deps/msgpack-c/include)
  # External plugin builds leave rime_plugins_deps empty, so tests that link
  # predict object files directly must keep SQLite on the link line themselves.
  # Merged builds instead get SQLite through the rime target's link interface,
  # where linking it again would only pass a duplicate -lsqlite3 to the linker.
  if(NOT rime_plugins_deps)
    if(TARGET SQLite3::SQLite3)
      target_link_libraries(rime_test PRIVATE SQLite3::SQLite3)
    elseif(TARGET SQLite::SQLite3)
      target_link_libraries(rime_test PRIVATE SQLite::SQLite3)
    elseif(SQLite3_LIBRARY)
      target_link_libraries(rime_test PRIVATE ${SQLite3_LIBRARY})
    elseif(SQLite3_LIBRARIES)
      target_link_libraries(rime_test PRIVATE ${SQLite3_LIBRARIES})
    endif()
  endif()
endif()

target_link_libraries(rime_test
  PRIVATE ${rime_library}
  ${rime_dict_library}
  ${rime_gears_library}
  ${rime_levers_library}
  ${rime_gtest_target})
rime_setup_executable(rime_test ${rime_test_output_dir})
if(BUILD_SHARED_LIBS)
  target_compile_definitions(rime_test PRIVATE RIME_IMPORTS)
endif()
if(NOT Opencc_DICT_DIR)
  message(FATAL_ERROR "Could not locate the installed opencc dict directory "
                       "(e.g. t2s.json); required to build rime_test.")
endif()
target_compile_definitions(rime_test PRIVATE
  RIME_OPENCC_DICT_DIR="${Opencc_DICT_DIR}")

file(GLOB test_data_files ${PROJECT_SOURCE_DIR}/data/test/*.yaml)
add_custom_target(rime_test_data ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${rime_test_output_dir}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${test_data_files} "${rime_test_output_dir}")
add_dependencies(rime_test rime_test_data)

add_test(NAME rime_test
  COMMAND rime_test
  WORKING_DIRECTORY ${rime_test_output_dir})

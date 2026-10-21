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
      target_link_libraries(rime_test SQLite3::SQLite3)
    elseif(TARGET SQLite::SQLite3)
      target_link_libraries(rime_test SQLite::SQLite3)
    elseif(SQLite3_LIBRARY)
      target_link_libraries(rime_test ${SQLite3_LIBRARY})
    elseif(SQLite3_LIBRARIES)
      target_link_libraries(rime_test ${SQLite3_LIBRARIES})
    endif()
  endif()
endif()

target_link_libraries(rime_test
  ${rime_library}
  ${rime_dict_library}
  ${rime_gears_library}
  ${rime_plugins_library}
  ${GTEST_LIBRARIES})

# Merged builds link the plugin dependencies (leveldb, sqlite3, ...) into the
# rime target itself, which exports them to its dependents. Linking
# rime_plugins_deps here as well would duplicate those -l flags, so the test
# target relies on that exported interface instead.

if(BUILD_SHARED_LIBS)
  target_compile_definitions(rime_test PRIVATE RIME_IMPORTS)
endif(BUILD_SHARED_LIBS)
if(NOT Opencc_DICT_DIR)
  message(FATAL_ERROR "Could not locate the installed opencc dict directory "
                       "(e.g. t2s.json); required to build rime_test.")
endif()
target_compile_definitions(rime_test PRIVATE
  RIME_OPENCC_DICT_DIR="${Opencc_DICT_DIR}")

file(GLOB test_data_files ${PROJECT_SOURCE_DIR}/data/test/*.yaml)
file(COPY ${test_data_files} DESTINATION ${EXECUTABLE_OUTPUT_PATH})

add_test(NAME rime_test
  COMMAND rime_test
  WORKING_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

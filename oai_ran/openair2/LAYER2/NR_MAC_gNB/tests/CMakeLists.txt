if(NOT ENABLE_TESTS)
  return()
endif()

add_executable(nr_mac_slice_cfg_test
  nr_mac_slice_cfg_test.cpp
  ${NR_GNB_MAC_DIR}/nr_mac_slice_cfg.c
)

target_include_directories(nr_mac_slice_cfg_test PRIVATE
  ${OPENAIR_DIR}
  ${JSON_C_INCLUDE_DIRS}
)

target_link_libraries(nr_mac_slice_cfg_test PRIVATE
  GTest::gtest_main
  UTIL
  ${JSON_C_LIBRARIES}
  pthread
)

add_dependencies(tests nr_mac_slice_cfg_test)
add_test(NAME nr_mac_slice_cfg COMMAND nr_mac_slice_cfg_test)

add_executable(gNB_scheduler_slice_test
  gNB_scheduler_slice_test.cpp
  ${NR_GNB_MAC_DIR}/nr_mac_slice_sched_utils.c
)

target_include_directories(gNB_scheduler_slice_test PRIVATE
  ${OPENAIR_DIR}
)

target_link_libraries(gNB_scheduler_slice_test PRIVATE
  GTest::gtest_main
)

add_dependencies(tests gNB_scheduler_slice_test)
add_test(NAME gNB_scheduler_slice COMMAND gNB_scheduler_slice_test)

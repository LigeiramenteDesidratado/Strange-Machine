message(STATUS "C flags: ${CMAKE_C_FLAGS}")

add_executable(renderer main.c scene01.c common.c)

target_include_directories(renderer PUBLIC ${CMAKE_SOURCE_DIR}/StrangeMachine/)
target_link_libraries(renderer LINK_PUBLIC StrangeMachine)


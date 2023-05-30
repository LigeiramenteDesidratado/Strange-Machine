message(STATUS "C flags: ${CMAKE_C_FLAGS}")

add_executable(TerrorEmSL main.c)

target_include_directories(TerrorEmSL PUBLIC ${CMAKE_SOURCE_DIR}/StrangeMachine/)
target_link_libraries(TerrorEmSL LINK_PUBLIC StrangeMachine)


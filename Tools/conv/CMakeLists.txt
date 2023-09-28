message(STATUS "C flags: ${CMAKE_C_FLAGS}")

add_executable(conv main.c
	../../StrangeMachine/core/smMM.c
        ../../StrangeMachine/core/smString.c
        ../../StrangeMachine/core/smArray.c
        ../../StrangeMachine/core/smResource.c
	../../StrangeMachine/core/smBaseMemory.c
	../../StrangeMachine/core/smLog.c
	../../StrangeMachine/core/smArena.c
	../../StrangeMachine/animation/smPose.c
	../../StrangeMachine/animation/smAnimation.c

	../../StrangeMachine/vendor/tlsf/tlsf.c
	)

target_compile_definitions(conv PRIVATE "$<$<CONFIG:Debug>:SM_DEBUG>")
target_include_directories(conv PUBLIC ${CMAKE_SOURCE_DIR}/StrangeMachine/)
target_link_libraries(conv LINK_PUBLIC cglm physfs-static m)

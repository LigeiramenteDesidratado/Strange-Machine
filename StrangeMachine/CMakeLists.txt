set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/glfw)

option(CGLM_SHARED "Shared build" OFF)
option(CGLM_STATIC "Static build" ON)
add_subdirectory(vendor/cglm)

option(PHYSFS_BUILD_STATIC "Build static library" TRUE)
option(PHYSFS_BUILD_SHARED "Build shared library" FALSE)
option(PHYSFS_BUILD_TEST "Build stdio test program." FALSE)
option(PHYSFS_BUILD_DOCS "Build doxygen based documentation" FALSE)
set(PHYSFS_TARGETNAME_UNINSTALL "physfsuninstall" CACHE STRING "Name of 'uninstall' build target")
add_subdirectory(vendor/physfs)

if (MSVC)
    # warning level 4
 	add_compile_options(/W4)
else()
	# additional warnings
	add_compile_options(-ggdb -Wall -Wextra -Wshadow -Wno-unused-function -Wjump-misses-init -Wmissing-prototypes)
endif()

add_library(${PROJECT_NAME}
        core/smMM.c
	core/smThread.c
        core/smString.c
        core/smArray.c
        core/smCore.c
        core/smResource.c
	core/smPRNG.c
	core/smHandlePool.c
	core/smLog.c
	core/smLog.c
	core/smBaseMemory.c
	core/smArena.c

	renderer/smRenderer.c
	animation/smAnimation.c
	animation/smPose.c
	audio/smAudio.c

	ecs/smECS.c
	ecs/smScene.c
	ecs/smStage.c

	effects/smShake.c

	math/smShape.c
	math/smCollision.c

	vendor/smVendorObject.c
	vendor/glad/glad.c
	vendor/tlsf/tlsf.c
	)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw physfs-static PUBLIC cglm dl m pthread)

target_compile_definitions(${PROJECT_NAME} PUBLIC "$<$<CONFIG:Debug>:SM_DEBUG>")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/StrangeMachine/vendor/physfs/src/")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/StrangeMachine/vendor/glfw/include/")
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/cglm/include/")
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

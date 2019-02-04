function(add_gamesdk_target)

    set(GAMESDK_PACKAGE_ROOT "${GAMESDK_ROOT}/package/gamesdk")
    set(GAMESDK_GEN_TASK sdkBuild)
    set(NDK_VERSION ${ANDROID_NDK_REVISION})
    string(REGEX REPLACE "^([^.]+).*" "\\1" NDK_VERSION ${ANDROID_NDK_REVISION} )
    set(GAMESDK_LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/${GAMESDK_PACKAGE_ROOT}/libs/${ANDROID_ABI}_r${NDK_VERSION}_${ANDROID_STL}")

    include_directories( "${GAMESDK_PACKAGE_ROOT}/include" ) # Games SDK Public Includes
    get_filename_component(DEP_LIB "${GAMESDK_LIB_ROOT}/libgamesdk.a" REALPATH)
    set(GAMESDK_LIB ${DEP_LIB} PARENT_SCOPE)

    add_custom_command(OUTPUT ${DEP_LIB}
        COMMAND ./gradlew ${GAMESDK_GEN_TASK} VERBATIM
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${GAMESDK_ROOT}/gamesdk"
        )
    add_custom_target(gamesdk_lib DEPENDS ${DEP_LIB})
    add_library( gamesdk STATIC IMPORTED GLOBAL)
    add_dependencies(gamesdk gamesdk_lib)
    set_target_properties(gamesdk PROPERTIES IMPORTED_LOCATION ${DEP_LIB})

endfunction()

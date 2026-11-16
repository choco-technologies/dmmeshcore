set(DMOD_MODULE_NAME        test_meshcore)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         4096)
set(DMOD_PRIORITY           0)

# The texts of the screen know no hardware: they are tested on a PC
dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    meshcore_test.c
    ../src/format.c
)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(${DMOD_MODULE_NAME} libmeshcore_if)

find_program(DMF_GET_EXECUTABLE dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)

if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
    add_test(
        NAME ${DMOD_MODULE_NAME}
        COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
            bash -c "${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}-local.dmd -y && ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}.dmf"
    )
endif()

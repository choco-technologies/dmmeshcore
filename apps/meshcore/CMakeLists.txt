# ======================================================================
#               meshcore - the screen of the MeshCore mesh
# ======================================================================
# The application of the launcher (it has an icon): the state of the radio, the nodes that were heard, the texts. All the mesh is in libmeshcore.
set(DMOD_MODULE_NAME        meshcore)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         6144)
set(DMOD_PRIORITY           1)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/meshcore.dmr)
set(DMOD_MAL_IMPLS          )

# The page (views/meshcore.html, with views/meshcore.html.ini for the options of its conversion) becomes a view with the fonts it draws: dmod converts
# it at build time. The views of every module of this project that has some are converted into one directory by default: this one's are its own.
set(DMOD_ASSETS_PATHS       ${CMAKE_CURRENT_SOURCE_DIR}/views)
set(DMOD_VIEWS_DIR          ${CMAKE_BINARY_DIR}/views-meshcore)

# The icon of the application in the launcher: icons/icon.svg (the default one), icons/<screen>/icon.svg for a kind of screen
set(DMOD_ICON_PATH          ${CMAKE_CURRENT_SOURCE_DIR}/icons)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
    src/format.c
)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# libdmview: the display (or the rent of it) claimed, the channel of the claim. dmosi: the waits. libmeshcore: everything about the mesh.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.17
    dmosi
    libdmview@>=0.20
)
target_link_libraries(${DMOD_MODULE_NAME} libmeshcore_if)

add_subdirectory(tests)

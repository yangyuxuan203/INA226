cmake_minimum_required(VERSION 3.22)

# User include paths
set(User_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LCD
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
)

# User sources
set(User_Sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/ina226_pv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/ina226_3.c
)

# Interface library for user code
add_library(user_code INTERFACE)
target_include_directories(user_code INTERFACE ${User_Include_Dirs})

# Add user sources to the project executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${User_Sources})

# Link user_code interface to project
target_link_libraries(${CMAKE_PROJECT_NAME} user_code)

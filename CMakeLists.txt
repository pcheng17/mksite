cmake_minimum_required(VERSION 3.20)
project(mksite)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/styles.h
    COMMAND xxd -i styles.css > ${CMAKE_CURRENT_BINARY_DIR}/styles.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/styles.css
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating styles.h from styles.css"
)

add_executable(${PROJECT_NAME} main.c ${CMAKE_CURRENT_BINARY_DIR}/styles.h)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

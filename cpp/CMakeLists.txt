cmake_minimum_required(VERSION 3.13)
project(atgeo-cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(aws-lambda-runtime REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS core)
find_package(ZLIB REQUIRED)   # needed by AWSSDK interface deps

# Use pkg-config to get the full GDAL link flags (includes all transitive deps)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GDAL REQUIRED gdal)

add_executable(${PROJECT_NAME} lambda.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${GDAL_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PRIVATE
    AWS::aws-lambda-runtime
    ${AWSSDK_LINK_LIBRARIES}
    ${GDAL_LINK_LIBRARIES}
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME bootstrap)

aws_lambda_package_target(${PROJECT_NAME})

cmake_minimum_required(VERSION 3.10)

# set the project name
project(feeddelayer VERSION 0.1)

IF (WIN32)
    set(OPENSSL_ROOT_DIR "C:\\SAPDevelop\\works\\openssl-1.1.0f-vs2017")
    set(BOOST_ROOT "C:\\SAPDevelop\\boost_1_77_0")
ENDIF()
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(SYSTEM ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIR})

add_executable(feeddelayer feeddelayer.cpp)

target_link_libraries(feeddelayer Boost OpenSSL::SSL)

target_include_directories(feeddelayer 
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR})


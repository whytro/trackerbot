cmake_minimum_required (VERSION 3.6)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(BOT_NAME "trackerbot")
project(${BOT_NAME})
aux_source_directory("src" coresrc)
add_executable(${BOT_NAME} ${coresrc})

set_target_properties(${BOT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)

find_package(DPP)
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(RapidJSON REQUIRED MODULE)
find_package(SpdLog REQUIRED MODULE)
find_package(Threads REQUIRED)
find_library(PQXX_LIB pqxx REQUIRED)
find_library(PQ_LIB pq REQUIRED)

target_include_directories(${BOT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DPP_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${PostgreSQL_INCLUDE_DIRS}
)
target_link_libraries(${BOT_NAME}
    dl
    redditcpp
    RapidJSON::RapidJSON
    SpdLog::SpdLog
    ${DPP_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    ${OPENSSL_CRYPTO_LIBRARY} 
    ${OPENSSL_SSL_LIBRARY}
    ${PostgreSQL_LIBRARIES}
    ${PQXX_LIB}
    ${PQ_LIB}
)

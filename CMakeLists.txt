cmake_minimum_required (VERSION 2.6)
project(hwnet)

#add_definitions(-DGATHER_RECV)

#find_library(LIB_OPENSSL NAMES crypto ssl)
#if(LIB_OPENSSL)
#  message("found openssl")
#  add_definitions(-DUSE_SSL)
  #SET( CMAKE_EXE_LINKER_FLAGS  "${CMAKE_SHARED_LINKER_FLAGS} -lssl -lcrypto")
#  SET(LIBSSL "ssl")
#  SET(LIBCRYPTO "crypto")
#elseif()
#  message("not found openssl")
#endif()

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
CHECK_CXX_COMPILER_FLAG("-std=c++14" COMPILER_SUPPORTS_CXX14)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if(COMPILER_SUPPORTS_CXX17)
  #add_definitions(-D_STD_ANY)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
elseif(COMPILER_SUPPORTS_CXX11) 
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11") 
elseif(COMPILER_SUPPORTS_CXX14) 
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")    
else()
  message(STATUS "c++ must >= c++11") 
endif()


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -Wall")
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")
include_directories("${PROJECT_SOURCE_DIR}/src/")
include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("/usr/local/include")
LINK_DIRECTORIES("/usr/local/lib")
LINK_DIRECTORIES("${PROJECT_SOURCE_DIR}/src/redis/hiredis")

set(src_list
  ${PROJECT_SOURCE_DIR}/src/net/TCPSocket.cpp
  ${PROJECT_SOURCE_DIR}/src/net/ThreadPool.cpp
  ${PROJECT_SOURCE_DIR}/src/net/Poller.cpp
  ${PROJECT_SOURCE_DIR}/src/net/TCPListener.cpp
  ${PROJECT_SOURCE_DIR}/src/net/TCPConnector.cpp
  ${PROJECT_SOURCE_DIR}/src/net/SocketHelper.cpp
  ${PROJECT_SOURCE_DIR}/src/net/http/http_parser.c
  ${PROJECT_SOURCE_DIR}/src/net/http/Http.cpp 
  ${PROJECT_SOURCE_DIR}/src/util/Timer.cpp      
)

set(src_list_redis
  ${PROJECT_SOURCE_DIR}/src/redis/RedisCli.cpp
)

if(LIB_OPENSSL)
  set(src_list 
    ${src_list}
    ${PROJECT_SOURCE_DIR}/src/net/SSLHandshake.cpp
  )
endif()

if(APPLE) 
  add_definitions(-D_MACOS)
  set(src_list 
    ${src_list}
    ${PROJECT_SOURCE_DIR}/src/net/mac/Kqueue.cpp
  )
elseif(UNIX)
  add_definitions(-D_LINUX)
  set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_SHARED_LINKER_FLAGS} -lrt -ldl")
  set(src_list 
    ${src_list}
    ${PROJECT_SOURCE_DIR}/src/net/linux/Epoll.cpp
  )  
endif()

add_library(hwnet STATIC ${src_list})
add_library(hwredis STATIC ${src_list_redis})

find_package(Threads REQUIRED)

add_executable(benmark test/benmark.cpp)
target_link_libraries(benmark hwnet pthread ${LIBSSL} ${LIBCRYPTO})

add_executable(benmark2 test/benmark2.cpp)
target_link_libraries(benmark2 hwnet pthread ${LIBSSL} ${LIBCRYPTO})

add_executable(benmark3 test/benmark3.cpp)
target_link_libraries(benmark3 hwnet pthread ${LIBSSL} ${LIBCRYPTO})

add_executable(broadcast test/broadcast.cpp)
target_link_libraries(broadcast hwnet pthread ${LIBSSL} ${LIBCRYPTO})

add_executable(httpserver example/httpserver.cpp)
target_link_libraries(httpserver hwnet pthread ${LIBSSL} ${LIBCRYPTO})

add_executable(httpclient example/httpclient.cpp)
target_link_libraries(httpclient hwnet pthread ${LIBSSL} ${LIBCRYPTO})

add_executable(echo example/echo.cpp)
target_link_libraries(echo hwnet pthread ${LIBSSL} ${LIBCRYPTO})

add_executable(testtimer test/testtimer.cpp)
target_link_libraries(testtimer hwnet pthread ${LIBSSL} ${LIBCRYPTO})


if(LIB_OPENSSL)

add_executable(sslserver example/sslserver.cpp)
target_link_libraries(sslserver hwnet pthread ${LIBSSL} ${LIBCRYPTO})

add_executable(sslclient example/sslclient.cpp)
target_link_libraries(sslclient hwnet pthread ${LIBSSL} ${LIBCRYPTO})

endif()

option(BUILD_DNS "Build dns" OFF)

if(BUILD_DNS)
  add_executable(dns example/dns/Resolver.cpp example/dns/dns.cpp)
  target_link_libraries(dns hwnet pthread cares ${LIBSSL} ${LIBCRYPTO})
endif(BUILD_DNS)

if(COMPILER_SUPPORTS_CXX17)
  add_executable(broadcast2 test/broadcast2.cpp)
  target_link_libraries(broadcast2 hwnet pthread ${LIBSSL} ${LIBCRYPTO})
endif()


option(BUILD_REDIS "Build redis test" OFF)

if(BUILD_REDIS)
  add_executable(rediscli example/rediscli.cpp)
  target_link_libraries(rediscli hwredis hwnet pthread hiredis ${LIBSSL} ${LIBCRYPTO})
endif(BUILD_REDIS)





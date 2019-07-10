cmake_minimum_required (VERSION 2.6)
project(hwnet)

#add_definitions(-DGATHER_RECV)

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


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -Wall")
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")
include_directories("${PROJECT_SOURCE_DIR}/src/")

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
find_package(Threads REQUIRED)

add_executable(benmark test/benmark.cpp)
target_link_libraries(benmark hwnet pthread)

add_executable(benmark2 test/benmark2.cpp)
target_link_libraries(benmark2 hwnet pthread)

add_executable(benmark3 test/benmark3.cpp)
target_link_libraries(benmark3 hwnet pthread)

add_executable(broadcast test/broadcast.cpp)
target_link_libraries(broadcast pthread hwnet)

add_executable(httpserver test/httpserver.cpp)
target_link_libraries(httpserver hwnet pthread)

add_executable(httpclient test/httpclient.cpp)
target_link_libraries(httpclient hwnet pthread)

add_executable(testtimer test/testtimer.cpp)
target_link_libraries(testtimer hwnet pthread)

#include_directories("${PROJECT_SOURCE_DIR}/")
#include_directories("/usr/local/include")
#LINK_DIRECTORIES("/usr/local/lib")
#add_executable(dns test/dns/Resolver.cpp test/dns/dns.cpp)
#target_link_libraries(dns hwnet pthread cares)

if(COMPILER_SUPPORTS_CXX17)
  add_executable(broadcast2 test/broadcast2.cpp)
  target_link_libraries(broadcast2 hwnet pthread)
endif()


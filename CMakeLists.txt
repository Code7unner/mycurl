cmake_minimum_required(VERSION 3.10)
project(mycurl)

set(CMAKE_CXX_STANDARD 11)

find_package(Boost REQUIRED COMPONENTS system thread)
if (Boost_FOUND)
    include_directories("${Boost_INCLUDE_DIRS}")
    link_directories("${Boost_LIBRARY_DIRS}")
endif(Boost_FOUND)

add_executable(mycurl main.cpp)

target_link_libraries(mycurl ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY})
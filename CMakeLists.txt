# 设置最低 CMake 版本要求
cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(CacheSystem)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)

# 指定源文件目录下的所有.cpp文件
file(GLOB SOURCES *.cpp)

# 设置目标可执行文件
add_executable(main ${SOURCES})

# 清理中间的.o文件
set_target_properties(main PROPERTIES CLEAN_DIRECT_OUTPUT 1)

# 额外的编译选项
# target_compile_options(main PRIVATE -Wall -Wextra -O2)
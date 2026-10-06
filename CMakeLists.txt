cmake_minimum_required(VERSION 3.20)
project(XAIArchitectureResearch LANGUAGES CXX)

include(CTest)

add_executable(xai_pillar_tests TESTS/cpp/pillar_tests.cpp)
target_compile_features(xai_pillar_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(xai_pillar_tests PRIVATE /W4 /WX)
else()
  target_compile_options(xai_pillar_tests PRIVATE -Wall -Wextra -Werror)
endif()

if(BUILD_TESTING)
  add_test(NAME xai_pillar_tests COMMAND xai_pillar_tests)
endif()

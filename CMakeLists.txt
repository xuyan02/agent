cmake_minimum_required(VERSION 3.20)

project(cpp_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPP_AGENT_BUILD_TESTS "Build tests" ON)

add_library(cpp_agent_lib
  src/agent/agent.cpp
  src/agent/intuitive_agent.cpp
  src/agent/prompt_overrides.cpp
  src/agent/routing_result.cpp
  src/agent/shallow_think_agent.cpp
  src/agent/simple_agent.cpp
  src/agent/smart_agent.cpp
  src/runtime/runtime.cpp
  src/runtime/tool.cpp
  src/infra/json/json.cpp
  src/infra/llm/llm_config_loader.cpp
  src/infra/llm/llm_context.cpp
  src/infra/llm/openai_provider.cpp
  src/infra/llm/openai_provider_factory.cpp
  src/infra/llm/openai_stream_accumulator.cpp
  src/infra/llm/sse_parser.cpp
  src/infra/http/openai_client.cpp
  src/infra/http_async/async_client.cc
)
target_include_directories(cpp_agent_lib PUBLIC src)

# This project is built with C++ exceptions disabled.
# Keep third_party targets untouched; only apply to our own targets.

# Minimal deps for the Agent API.
set(DUST_BUILD_TESTS OFF CACHE BOOL "Build dust tests" FORCE)
add_subdirectory(third_party/dust EXCLUDE_FROM_ALL)

# OpenSSL is vendored as a git submodule under third_party/openssl.
# We still build it using its official Configure+make flow (OpenSSL does not provide
# an official CMake build).
include(ExternalProject)

set(OPENSSL_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl)
set(OPENSSL_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/_deps/openssl-install)

ExternalProject_Add(
  openssl_ep
  SOURCE_DIR ${OPENSSL_SRC_DIR}
  CONFIGURE_COMMAND ${OPENSSL_SRC_DIR}/Configure linux-x86_64 no-shared no-tests --prefix=${OPENSSL_INSTALL_DIR}
  BUILD_COMMAND make -j
  INSTALL_COMMAND make install_sw
  BUILD_IN_SOURCE 1
)

add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::Crypto PROPERTIES
  IMPORTED_LOCATION ${OPENSSL_INSTALL_DIR}/lib64/libcrypto.a
  INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_SRC_DIR}/include
)
add_dependencies(OpenSSL::Crypto openssl_ep)

add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::SSL PROPERTIES
  IMPORTED_LOCATION ${OPENSSL_INSTALL_DIR}/lib64/libssl.a
  INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_SRC_DIR}/include
)
add_dependencies(OpenSSL::SSL openssl_ep)

# Use vendored libcurl (git submodule under third_party/curl).
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(HTTP_ONLY OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_WOLFSSL OFF CACHE BOOL "" FORCE)
# Use vendored zlib (git submodule under third_party/zlib).
set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/zlib EXCLUDE_FROM_ALL)

# Tell curl to use zlib from this build.
set(CURL_ZLIB ON CACHE BOOL "" FORCE)
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib CACHE PATH "" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)

set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/curl EXCLUDE_FROM_ALL)

# Ensure libcurl is built after OpenSSL and links against OpenSSL + zlib.
add_dependencies(libcurl_static openssl_ep)
target_link_libraries(libcurl_static PRIVATE OpenSSL::SSL OpenSSL::Crypto zlibstatic)

target_link_libraries(cpp_agent_lib PRIVATE dust libcurl_static)
target_include_directories(cpp_agent_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dust/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/single_include
)

# Temporarily disable app/examples/tests while redesigning the Agent API.
# (Sources are kept in-tree but are not part of the build.)

add_executable(test_one_step_agent apps/test_one_step_agent.cc)
target_link_libraries(test_one_step_agent PRIVATE cpp_agent_lib)

if(CPP_AGENT_BUILD_TESTS)
  enable_testing()
  message(STATUS "CPP_AGENT_BUILD_TESTS is ON but tests are disabled temporarily")
endif()

cmake_minimum_required(VERSION 3.20)

project(cpp_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPP_AGENT_BUILD_TESTS "Build tests" ON)

add_library(cpp_agent_lib
  src/llm/chat_message.cc
  src/json/json.cc
  src/llm/openai/openai_provider.cc
  src/llm/openai/openai_sender.cc
  src/http/http_client.cc
  src/tool/tool_spec.cc
  src/tool/debug_tool.cc
  src/tool/file_tool.cc
  src/agent/in_memory_history.cc
  src/agent/session.cc
  src/agent/agent_context.cc
  src/agent/agent_runner.cc
  src/agent/llm_agent.cc
  src/agent/tool_call_executor.cc
  src/config/agent_config.cc
  src/console/cli_console.cc
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

add_executable(agent_cli apps/agent_cli.cc)
target_link_libraries(agent_cli PRIVATE cpp_agent_lib)

add_executable(test_watch_stdin apps/test_watch_stdin.cc)
target_link_libraries(test_watch_stdin PRIVATE cpp_agent_lib)

add_executable(test_epoll_raw_stdin apps/test_epoll_raw_stdin.cc)

if(CPP_AGENT_BUILD_TESTS)
  enable_testing()

  add_executable(test_http_client_invalid tests/http/test_http_client_invalid.cc)
  target_link_libraries(test_http_client_invalid PRIVATE cpp_agent_lib)
  add_test(NAME test_http_client_invalid COMMAND test_http_client_invalid)

  add_executable(test_session_defaults tests/test_session_defaults.cc)
  target_link_libraries(test_session_defaults PRIVATE cpp_agent_lib)
  add_test(NAME test_session_defaults COMMAND test_session_defaults)

  add_executable(test_session_tool_store tests/test_session_tool_store.cc)
  target_link_libraries(test_session_tool_store PRIVATE cpp_agent_lib)
  add_test(NAME test_session_tool_store COMMAND test_session_tool_store)

  add_executable(test_session_create_sender tests/test_session_create_sender.cc)
  target_link_libraries(test_session_create_sender PRIVATE cpp_agent_lib)
  add_test(NAME test_session_create_sender COMMAND test_session_create_sender)

  add_executable(test_in_memory_history tests/test_in_memory_history.cc)
  target_link_libraries(test_in_memory_history PRIVATE cpp_agent_lib)
  add_test(NAME test_in_memory_history COMMAND test_in_memory_history)

  add_executable(test_llm_agent_send_once tests/test_llm_agent_send_once.cc)
  target_link_libraries(test_llm_agent_send_once PRIVATE cpp_agent_lib)
  add_test(NAME test_llm_agent_send_once COMMAND test_llm_agent_send_once)

  add_executable(test_tool_call_executor tests/test_tool_call_executor.cc)
  target_link_libraries(test_tool_call_executor PRIVATE cpp_agent_lib)
  add_test(NAME test_tool_call_executor COMMAND test_tool_call_executor)

  add_executable(test_debug_tool tests/test_debug_tool.cc)
  target_link_libraries(test_debug_tool PRIVATE cpp_agent_lib)
  add_test(NAME test_debug_tool COMMAND test_debug_tool)

  add_executable(test_file_tool tests/test_file_tool.cc)
  target_link_libraries(test_file_tool PRIVATE cpp_agent_lib)
  add_test(NAME test_file_tool COMMAND test_file_tool)

  add_executable(test_agent_context_defaults tests/test_agent_context_defaults.cc)
  target_link_libraries(test_agent_context_defaults PRIVATE cpp_agent_lib)
  add_test(NAME test_agent_context_defaults COMMAND test_agent_context_defaults)

  add_executable(test_agent_runner_wiring tests/test_agent_runner_wiring.cc)
  target_link_libraries(test_agent_runner_wiring PRIVATE cpp_agent_lib)

  add_executable(test_agent_config_yaml tests/test_agent_config_yaml.cc)
  target_link_libraries(test_agent_config_yaml PRIVATE cpp_agent_lib)
  add_test(NAME test_agent_runner_wiring COMMAND test_agent_runner_wiring)
  add_test(NAME test_agent_config_yaml COMMAND test_agent_config_yaml)
endif()

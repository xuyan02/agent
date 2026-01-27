cmake_minimum_required(VERSION 3.20)
project(cpp_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CPP_AGENT_BUILD_TESTS "Build tests" ON)

add_library(cpp_agent_lib
  src/core/agent.cpp
  src/core/conversation.cpp
  src/core/policy.cpp
  src/core/tool_log.cpp
  src/app/config.cpp
  src/app/wiring.cpp

  src/runtime/agent.cpp
  src/runtime/general_agent.cpp
  src/runtime/message_codec.cpp
  src/runtime/professional_agent.cpp
  src/runtime/runtime.cpp
  src/runtime/team.cpp
  src/runtime/skill_registry.cpp

  src/infra/http/openai_client.cpp
  src/infra/http_async/async_client.cc
  src/infra/llm/llm_context.cpp
  src/infra/llm/llm_config_loader.cpp
  src/infra/llm/json_min.cpp
  src/infra/llm/sse_parser.cpp
  src/infra/llm/openai_provider.cpp
  src/infra/llm/openai_provider_factory.cpp
  src/infra/plan/plan_model.cpp
  src/infra/plan/plan_store.cpp
  src/infra/tools/file_tools.cpp
  src/infra/tools/plan_toolset.cpp
  src/infra/tools/shell_tool.cpp
  src/infra/storage/json_file_storage.cpp
  src/infra/console/cli_console.cpp

  src/runtime/plan2/plan2_model.cpp
  src/runtime/plan2/uuid_gen.cpp
  src/runtime/plan2/plan2_tool.cpp
  src/runtime/plan2/plan2_functions.cpp
)

target_include_directories(cpp_agent_lib PUBLIC src)

# This project is built with C++ exceptions disabled.
# Keep third_party targets untouched; only apply to our own targets.
target_compile_options(cpp_agent_lib PRIVATE -fno-exceptions -g)

include(ExternalProject)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_STATICLIB ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)

set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)

# Build and vendor OpenSSL (static) for curl TLS.
#
# NOTE: curl's CMake consumes OpenSSL via find_package() during *configure*,
# so we support a two-stage build:
#   1) Configure with -DCPP_AGENT_BOOTSTRAP_OPENSSL=ON and build target openssl_ep
#   2) Re-run CMake (same build dir) without CPP_AGENT_BOOTSTRAP_OPENSSL; curl will
#      find OpenSSL in ${_openssl_install_dir}.
option(CPP_AGENT_BOOTSTRAP_OPENSSL "Bootstrap vendored OpenSSL only (skip curl/app configure-time OpenSSL use)" OFF)

set(_openssl_install_dir "${CMAKE_BINARY_DIR}/third_party/openssl-install")
ExternalProject_Add(openssl_ep
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl"
  CONFIGURE_COMMAND
    ${CMAKE_COMMAND} -E env
      PERL=${PERL_EXECUTABLE}
      ./Configure linux-x86_64
        no-shared
        no-tests
        --prefix=${_openssl_install_dir}
        --openssldir=${_openssl_install_dir}
  BUILD_COMMAND ${CMAKE_COMMAND} -E env make -j
  INSTALL_COMMAND ${CMAKE_COMMAND} -E env make install_sw
  BUILD_IN_SOURCE 1
)

# For stage 2, point curl's find_package(OpenSSL) at the vendored install.
set(OPENSSL_ROOT_DIR "${_openssl_install_dir}" CACHE PATH "" FORCE)
set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
set(CURL_USE_SECTRANSP OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_WOLFSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_BEARSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_RUSTLS OFF CACHE BOOL "" FORCE)

# zlib vendored as a static library; curl will consume it.
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/zlib EXCLUDE_FROM_ALL)

if(CPP_AGENT_BOOTSTRAP_OPENSSL)
  # Stage 1 build: only build vendored OpenSSL.
  return()
endif()

# Ensure curl uses the vendored zlib target rather than system libz.
# curl's configure-time try_compile runs in a fresh project, so ALIAS targets
# like ZLIB::ZLIB are not visible there. Provide an IMPORTED target instead.
add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
set_target_properties(ZLIB::ZLIB PROPERTIES
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/third_party/zlib/libz.a"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib"
)
add_dependencies(ZLIB::ZLIB zlibstatic)

# Fully static build is best-effort; TLS/backends may still require system deps.
add_subdirectory(third_party/curl EXCLUDE_FROM_ALL)

# dust (MessageLoop runtime)
# Allow building dust's own tests in the top-level build tree.
set(DUST_BUILD_TESTS OFF CACHE BOOL "Build dust tests" FORCE)
add_subdirectory(third_party/dust EXCLUDE_FROM_ALL)

target_link_libraries(cpp_agent_lib PRIVATE libcurl dust)
# curl's include directory isn't propagated via libcurl here (it's built as a subdir),
# so add it explicitly to our library.
target_include_directories(cpp_agent_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/curl/include)
# dust headers
target_include_directories(cpp_agent_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dust/include)

add_executable(cpp-agent src/app/main.cpp)
target_link_libraries(cpp-agent PRIVATE cpp_agent_lib)
target_compile_options(cpp-agent PRIVATE -fno-exceptions -g)

add_executable(cpp-agent-http-async-baidu-smoke apps/http_async_baidu_smoke.cc)
target_link_libraries(cpp-agent-http-async-baidu-smoke PRIVATE cpp_agent_lib)
target_compile_options(cpp-agent-http-async-baidu-smoke PRIVATE -fno-exceptions -g)

if(CPP_AGENT_BUILD_TESTS)
  enable_testing()

  add_executable(cpp-agent-tests-policy
    tests/test_policy.cpp
  )
  target_link_libraries(cpp-agent-tests-policy PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-policy PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-policy COMMAND cpp-agent-tests-policy)

  add_executable(cpp-agent-tests-config-path
    tests/test_config_path.cpp
  )
  target_link_libraries(cpp-agent-tests-config-path PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-config-path PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-config-path COMMAND cpp-agent-tests-config-path)

  add_executable(cpp-agent-tests-tool-calls-parsing
    tests/test_tool_calls_parsing.cpp
  )
  target_link_libraries(cpp-agent-tests-tool-calls-parsing PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-tool-calls-parsing PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-tool-calls-parsing COMMAND cpp-agent-tests-tool-calls-parsing)

  add_executable(cpp-agent-tests-message-codec
    tests/test_message_codec.cpp
  )
  target_link_libraries(cpp-agent-tests-message-codec PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-message-codec PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-message-codec COMMAND cpp-agent-tests-message-codec)

  add_executable(cpp-agent-tests-professional-agent
    tests/test_professional_agent.cpp
  )

  add_executable(cpp-agent-tests-plan2-model
    tests/test_plan2_model.cpp
  )
  target_link_libraries(cpp-agent-tests-plan2-model PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-plan2-model PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-plan2-model COMMAND cpp-agent-tests-plan2-model)

  add_executable(cpp-agent-tests-skill-registry
    tests/test_skill_registry.cpp
  )
  target_link_libraries(cpp-agent-tests-skill-registry PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-skill-registry PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-skill-registry COMMAND cpp-agent-tests-skill-registry)

  add_executable(cpp-agent-tests-skill-tools
    tests/test_skill_tools.cpp
  )
  target_link_libraries(cpp-agent-tests-skill-tools PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-skill-tools PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-skill-tools COMMAND cpp-agent-tests-skill-tools)
  target_link_libraries(cpp-agent-tests-professional-agent PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-professional-agent PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-professional-agent COMMAND cpp-agent-tests-professional-agent)

  # Tool calling disabled in streaming-only mode for now.
  # add_executable(cpp-agent-tests-agent-tool-loop
  #   tests/test_agent_tool_loop.cpp
  # )
  # target_link_libraries(cpp-agent-tests-agent-tool-loop PRIVATE cpp_agent_lib)
  # add_test(NAME cpp-agent-tests-agent-tool-loop COMMAND cpp-agent-tests-agent-tool-loop)

  add_executable(cpp-agent-tests-plan-replan
    tests/test_plan_replan.cpp
  )
  target_link_libraries(cpp-agent-tests-plan-replan PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-plan-replan PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-plan-replan COMMAND cpp-agent-tests-plan-replan)

  add_executable(cpp-agent-tests-plan-complete-render
    tests/test_plan_complete_render.cpp
  )
  target_link_libraries(cpp-agent-tests-plan-complete-render PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-plan-complete-render PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-plan-complete-render COMMAND cpp-agent-tests-plan-complete-render)

  add_executable(cpp-agent-tests-plan-active-migration
    tests/test_plan_active_migration.cpp
  )
  target_link_libraries(cpp-agent-tests-plan-active-migration PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-plan-active-migration PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-plan-active-migration COMMAND cpp-agent-tests-plan-active-migration)

  add_executable(cpp-agent-tests-http-async-baidu
    tests/test_http_async_baidu.cpp
  )
  target_link_libraries(cpp-agent-tests-http-async-baidu PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-http-async-baidu PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-http-async-baidu COMMAND cpp-agent-tests-http-async-baidu)

  add_executable(cpp-agent-tests-llm-context
    tests/test_llm_context.cpp
  )
  target_link_libraries(cpp-agent-tests-llm-context PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-llm-context PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-llm-context COMMAND cpp-agent-tests-llm-context)

  add_executable(cpp-agent-tests-llm-config-loader
    tests/test_llm_config_loader.cpp
  )
  target_link_libraries(cpp-agent-tests-llm-config-loader PRIVATE cpp_agent_lib)
  target_compile_options(cpp-agent-tests-llm-config-loader PRIVATE -fno-exceptions -g)
  add_test(NAME cpp-agent-tests-llm-config-loader COMMAND cpp-agent-tests-llm-config-loader)

endif()

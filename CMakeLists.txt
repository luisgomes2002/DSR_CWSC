cmake_minimum_required(VERSION 3.20)
project(dsr_network_lab VERSION 0.4.0 LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "Este prototipo usa Winsock e requer Windows.")
endif()
add_library(dsr_network_core STATIC
  src/app/options.cpp
  src/net/socket.cpp
  src/net/tls.cpp
  src/net/openssl_transport.cpp
  src/net/dtls.cpp
  src/protocol/protocol.cpp
  src/protocol/entity_messages.cpp
  src/protocol/movement_packet.cpp
  src/protocol/movement_stream.cpp
  src/world/world.cpp
  src/session/host.cpp
  src/session/host_movement.cpp
  src/session/movement_link.cpp
  src/client/client.cpp
  src/client/client_movement.cpp
  src/client/player_replica.cpp
  src/ui/console_input.cpp
  src/ui/client_commands.cpp
)
target_include_directories(dsr_network_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(dsr_network_core PUBLIC cxx_std_17)
target_compile_definitions(dsr_network_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
target_link_libraries(dsr_network_core PUBLIC ws2_32 bcrypt crypt32)

add_executable(dsr_net src/main.cpp)
target_link_libraries(dsr_net PRIVATE dsr_network_core)

foreach(target IN ITEMS dsr_network_core dsr_net)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
endforeach()
if(MINGW)
  target_link_options(dsr_net PRIVATE -static -static-libgcc -static-libstdc++)
endif()
include(CTest)
if(BUILD_TESTING)
  # Real host/client with only endpoint routing redirected to a loopback relay.
  # The ordinary application contains no relay configuration or fault hook.
  add_executable(dsr_net_fault_lab src/main.cpp src/net/dtls.cpp)
  target_compile_definitions(dsr_net_fault_lab PRIVATE DSR_ROOM_TESTING)
  target_link_libraries(dsr_net_fault_lab PRIVATE dsr_network_core)
  if(MSVC)
    target_compile_options(dsr_net_fault_lab PRIVATE /W4 /EHsc)
  else()
    target_compile_options(dsr_net_fault_lab PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
  if(MINGW)
    target_link_options(dsr_net_fault_lab PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_executable(entities_unit tests/entities_unit.cpp)
  target_link_libraries(entities_unit PRIVATE dsr_network_core)
  if(MSVC)
    target_compile_options(entities_unit PRIVATE /W4 /EHsc)
  else()
    target_compile_options(entities_unit PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
  if(MINGW)
    target_link_options(entities_unit PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME entities_unit COMMAND entities_unit)
  add_executable(movement_unit tests/movement_unit.cpp)
  target_link_libraries(movement_unit PRIVATE dsr_network_core)
  if(MSVC)
    target_compile_options(movement_unit PRIVATE /W4 /EHsc)
  else()
    target_compile_options(movement_unit PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
  if(MINGW)
    target_link_options(movement_unit PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME movement_unit COMMAND movement_unit)
  # Compile the same transport with a BIO fault hook available only in this test.
  add_executable(dtls_unit tests/dtls_unit.cpp src/net/dtls.cpp)
  target_compile_definitions(dtls_unit PRIVATE DSR_DTLS_TESTING)
  target_link_libraries(dtls_unit PRIVATE dsr_network_core)
  if(MSVC)
    target_compile_options(dtls_unit PRIVATE /W4 /EHsc)
  else()
    target_compile_options(dtls_unit PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
  if(MINGW)
    target_link_options(dtls_unit PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME dtls_unit COMMAND dtls_unit)
  set_tests_properties(dtls_unit PROPERTIES TIMEOUT 60)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME player_reader_unit COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/player_reader_unit.py")
  set_tests_properties(player_reader_unit PROPERTIES TIMEOUT 45)
  add_test(NAME loopback_integration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py" "$<TARGET_FILE:dsr_net>")
  set_tests_properties(loopback_integration PROPERTIES TIMEOUT 60)
  add_test(NAME entities_integration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/entities_integration.py" "$<TARGET_FILE:dsr_net>")
  set_tests_properties(entities_integration PROPERTIES TIMEOUT 60)
  find_program(DSR_OPENSSL_EXECUTABLE NAMES openssl
    HINTS "C:/Program Files/Git/usr/bin" "C:/Program Files/Git/mingw64/bin")
  if(DSR_OPENSSL_EXECUTABLE)
    add_test(NAME tls_integration COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/tls_integration.py" "$<TARGET_FILE:dsr_net>" "${DSR_OPENSSL_EXECUTABLE}")
    set_tests_properties(tls_integration PROPERTIES TIMEOUT 90)
    add_test(NAME udp_integration COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/udp_integration.py" "$<TARGET_FILE:dsr_net>" "${DSR_OPENSSL_EXECUTABLE}")
    set_tests_properties(udp_integration PROPERTIES TIMEOUT 120)
    add_test(NAME benchmark_integration COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_integration.py" "$<TARGET_FILE:dsr_net>" "${DSR_OPENSSL_EXECUTABLE}")
    set_tests_properties(benchmark_integration PROPERTIES TIMEOUT 90)
    add_test(NAME room_fault_integration COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/room_fault_integration.py"
      "$<TARGET_FILE:dsr_net_fault_lab>" "${DSR_OPENSSL_EXECUTABLE}" "$<TARGET_FILE:dsr_net>")
    set_tests_properties(room_fault_integration PROPERTIES TIMEOUT 240)
  else()
    message(WARNING "OpenSSL CLI not found: TLS/UDP integration will not be registered. Set DSR_OPENSSL_EXECUTABLE.")
  endif()
endif()

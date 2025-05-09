cmake_minimum_required(VERSION 3.15)
project(DistributedEncryption)

set(CMAKE_CXX_STANDARD 17)

# Find packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Modern Protobuf configuration
find_package(protobuf CONFIG REQUIRED)
message(STATUS "Found Protobuf ${protobuf_VERSION}")

# Modern gRPC configuration
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Found gRPC ${gRPC_VERSION}")

# Set proto file path
set(PROTO_FILE "${CMAKE_SOURCE_DIR}/include/encryption.proto")

# Generate protobuf sources (modern way)
get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
set(PROTO_SRCS "${CMAKE_BINARY_DIR}/${PROTO_NAME}.pb.cc")
set(PROTO_HDRS "${CMAKE_BINARY_DIR}/${PROTO_NAME}.pb.h")
set(GRPC_SRCS "${CMAKE_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h")

# Force clean of generated protobuf files to ensure regeneration
file(REMOVE ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS})

# Custom command to generate protobuf and grpc files
add_custom_command(
    OUTPUT "${PROTO_SRCS}" "${PROTO_HDRS}" "${GRPC_SRCS}" "${GRPC_HDRS}"
    COMMAND protobuf::protoc
    ARGS --grpc_out "${CMAKE_BINARY_DIR}"
        --cpp_out "${CMAKE_BINARY_DIR}"
        -I "${PROTO_DIR}"
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}"
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}  # For generated headers
    ${OpenSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
)

# Create common library with shared code
add_library(common_lib STATIC
    src/chunk.cpp
    src/crypto.cpp
    src/utilities.cpp
    src/dropbox_client.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_link_libraries(common_lib
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    ${CURL_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# Worker executable
add_executable(worker 
    src/worker.cpp 
    worker_main.cpp)

target_link_libraries(worker common_lib)

# Master executable
add_executable(master 
    src/master.cpp 
    src/worker.cpp  # Add worker.cpp to include EncryptionWorker implementation
    main.cpp)

target_link_libraries(master common_lib)

# Combined executable
add_executable(distributed_encryption
    src/master.cpp
    src/worker.cpp
    main.cpp)

target_link_libraries(distributed_encryption common_lib)

# Test encryption executable
add_executable(test_encryption
    test_encryption.cpp)

target_link_libraries(test_encryption common_lib)

# Console test worker
add_executable(test_worker_console
    test_worker_console.cpp
    src/worker.cpp)

target_link_libraries(test_worker_console common_lib)

# Console test master
add_executable(test_master_console
    test_master_console.cpp
    src/master.cpp)

target_link_libraries(test_master_console common_lib)
cmake_minimum_required(VERSION 3.15)
project(DistributedEncryption)

set(CMAKE_CXX_STANDARD 17)

#Find packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

#Protobuf configuration
find_package(protobuf CONFIG REQUIRED)
message(STATUS "Found Protobuf ${protobuf_VERSION}")

#gRPC configuration
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Found gRPC ${gRPC_VERSION}")

#Set proto file path
set(PROTO_FILE "${CMAKE_SOURCE_DIR}/include/encryption.proto")

#Generate protobuf sources
get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
set(PROTO_SRCS "${CMAKE_BINARY_DIR}/${PROTO_NAME}.pb.cc")
set(PROTO_HDRS "${CMAKE_BINARY_DIR}/${PROTO_NAME}.pb.h")
set(GRPC_SRCS "${CMAKE_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h")

#Custom command to generate protobuf and grpc files
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

#Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
    ${OpenSSL_INCLUDE_DIR}
)

#Source files
set(SOURCE_FILES
    src/chunk.cpp
    src/crypto.cpp
    src/master.cpp
    src/worker.cpp
    main.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

#Executable
add_executable(distributed_encryption ${SOURCE_FILES})

#Link libraries
target_link_libraries(distributed_encryption
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
#ifndef WORKER_H
#define WORKER_H

#include <grpcpp/grpcpp.h>
#include "encryption.pb.h"
#include "encryption.grpc.pb.h"

class EncryptionWorker final : public encryption::EncryptionService::Service {
public:
    grpc::Status EncryptChunk(grpc::ServerContext* context, 
                             const encryption::ChunkRequest* request, 
                             encryption::ChunkResponse* response) override;
    
    grpc::Status DecryptChunk(grpc::ServerContext* context,
                             const encryption::ChunkRequest* request,
                             encryption::ChunkResponse* response) override;
    
    void runServer(const std::string& serverAddress, bool useTLS = false);

    grpc::Status TestConnection(
        grpc::ServerContext* context,
        const encryption::TestRequest* request,
        encryption::TestResponse* response);
};

#endif // WORKER_H
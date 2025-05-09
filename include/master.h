// master.h
#ifndef MASTER_H
#define MASTER_H

#include <grpcpp/grpcpp.h>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include "encryption.pb.h"
#include "encryption.grpc.pb.h"
#include "chunk.h"

class EncryptionMaster {
public:
    EncryptionMaster(const std::vector<std::string>& workerAddresses, bool useTLS = false);    
    
    std::vector<FileChunk> encryptFile(const std::string& filePath, 
                                     size_t chunkSize, 
                                     const std::string& key, 
                                     const std::string& iv);

    std::vector<FileChunk> decryptFile(const std::string& filePath,
                                        size_t chunkSize,
                                        const std::string& key,
                                        const std::string& iv);
       
    bool testWorkerConnections();
    
    // New method for writing processed data to files
    bool writeProcessedDataToFile(const std::string& outputPath, const std::vector<FileChunk>& chunks);

private:
    std::vector<std::unique_ptr<encryption::EncryptionService::Stub>> stubs_;
    bool useTLS_;
    std::mutex mutex_; // For thread-safe operations

    // Helper methods
    std::shared_ptr<grpc::Channel> createChannel(const std::string& address);
    void initializeStubs(const std::vector<std::string>& workerAddresses);
};

#endif // MASTER_H
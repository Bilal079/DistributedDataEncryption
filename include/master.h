#ifndef MASTER_H
#define MASTER_H

#include <grpcpp/grpcpp.h>
#include <vector>
#include <memory>
#include "encryption.pb.h"
#include "encryption.grpc.pb.h"
#include "chunk.h"
using namespace std;

class EncryptionMaster {
public:
    EncryptionMaster(const vector<string>& workerAddresses);
    
    vector<FileChunk> encryptFile(const string& filePath, 
                                      size_t chunkSize, 
                                      const string& key, 
                                      const string& iv);


private:
    vector<unique_ptr<encryption::EncryptionService::Stub>> stubs_;
};

#endif // MASTER_H
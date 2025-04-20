#include "master.h"
#include <thread>
#include <future>
using namespace std;

EncryptionMaster::EncryptionMaster(const vector<string>& workerAddresses) {
    for (const auto& address : workerAddresses) {
        auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stubs_.push_back(encryption::EncryptionService::NewStub(channel));
    }
}

vector<FileChunk> EncryptionMaster::encryptFile(const string& filePath, 
                                                   size_t chunkSize, 
                                                   const string& key, 
                                                   const string& iv) {
    //Split file into chunks
    auto chunks = FileChunker::chunkFile(filePath, chunkSize);
    vector<FileChunk> encryptedChunks(chunks.size());
    vector<future<void>> futures;
    
    //Process chunks in parallel
    for (size_t i = 0; i < chunks.size(); i++) {
        size_t workerIndex = i % stubs_.size();
        futures.push_back(async(launch::async, [&, i, workerIndex]() {
            encryption::ChunkRequest request;
            request.set_data(chunks[i].data.data(), chunks[i].data.size());
            request.set_chunk_id(chunks[i].id);
            request.set_key(key.data(), key.size());
            request.set_iv(iv.data(), iv.size());
            
            encryption::ChunkResponse response;
            grpc::ClientContext context;
            
            grpc::Status status = stubs_[workerIndex]->EncryptChunk(&context, request, &response);
            
            if (!status.ok() || !response.success()) {
                throw runtime_error("Encryption failed for chunk " + to_string(i) + 
                                       ": " + response.error_message());
            }
            
            FileChunk encryptedChunk;
            encryptedChunk.id = response.chunk_id();
            encryptedChunk.data.assign(response.encrypted_data().begin(), 
                                     response.encrypted_data().end());
            encryptedChunks[i] = encryptedChunk;
        }));
    }
    
    //Wait for all chunks to be processed
    for (auto& future : futures) {
        future.get();
    }
    
    return encryptedChunks;
}
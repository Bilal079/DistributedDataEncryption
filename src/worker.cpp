#include "worker.h"
#include "crypto.h"
#include <iostream>
using namespace std;

grpc::Status EncryptionWorker::EncryptChunk(grpc::ServerContext* context, 
    const encryption::ChunkRequest* request, 
    encryption::ChunkResponse* response) {
try {
//Convert protobuf bytes to vectors
vector<char> data(request->data().begin(), request->data().end());
string key(request->key().begin(), request->key().end());
string iv(request->iv().begin(), request->iv().end());

//Encrypt the data
auto encrypted = AESCrypto::encrypt(data, key, iv);

//Set the response
response->set_encrypted_data(encrypted.data(), encrypted.size());
response->set_chunk_id(request->chunk_id());
response->set_success(true);
} catch (const exception& e) {
response->set_success(false);
response->set_error_message(e.what());
}

return grpc::Status::OK;
}

void EncryptionWorker::runServer(const string& serverAddress) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(this);
    
    unique_ptr<grpc::Server> server(builder.BuildAndStart());
    cout << "Worker server listening on " << serverAddress << endl;
    server->Wait();
}
#include "worker.h"
#include "crypto.h"
#include "utilities.h"
#include <iostream>
#include <openssl/err.h>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <windows.h>
#include <psapi.h>

// Global log file for worker
static std::ofstream workerLogFile;

// Helper function to get a formatted timestamp
std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    struct tm timeinfo;
    localtime_s(&timeinfo, &time);
    ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Helper logging function
void log(const std::string& message, bool isError = false) {
    std::string timestamp = getTimestamp();
    std::string prefix = isError ? "ERROR" : "INFO";
    
    if (isError) {
        std::cerr << "[" << timestamp << "] [" << prefix << "] " << message << std::endl;
    } else {
        std::cout << "[" << timestamp << "] [" << prefix << "] " << message << std::endl;
    }
    
    // Additionally log to file for persistence
    if (workerLogFile.is_open()) {
        workerLogFile << "[" << timestamp << "] [" << prefix << "] " << message << std::endl;
        workerLogFile.flush();
    }
}

grpc::Status EncryptionWorker::EncryptChunk(grpc::ServerContext* context, 
    const encryption::ChunkRequest* request, 
    encryption::ChunkResponse* response) {
try {
    log("Worker received EncryptChunk request for chunk " + std::to_string(request->chunk_id()) +
        " (" + std::to_string(request->data().size()) + " bytes)");
    
    // Convert protobuf bytes to vectors
    std::vector<char> data(request->data().begin(), request->data().end());
    log("Data size: " + std::to_string(data.size()) + " bytes");
    
    std::string key(request->key().begin(), request->key().end());
    log("Key size: " + std::to_string(key.size()) + " bytes");
    
    std::string iv(request->iv().begin(), request->iv().end());
    log("IV size: " + std::to_string(iv.size()) + " bytes");

    // Clear any previous OpenSSL errors
    ERR_clear_error();

    // Encrypt the data
    log("Starting encryption...");
    auto startTime = std::chrono::high_resolution_clock::now();
    auto encrypted = AESCrypto::encrypt(data, key, iv);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    log("Encryption completed (" + std::to_string(encrypted.size()) + " bytes) in " + 
        std::to_string(duration) + " ms");

    // Set the response
    log("Setting response...");
    response->set_processed_data(encrypted.data(), encrypted.size());
    response->set_chunk_id(request->chunk_id());
    response->set_success(true);
    log("EncryptChunk successful for chunk " + std::to_string(request->chunk_id()));
    
    // Verify the response data size matches what was encrypted
    if (response->processed_data().size() != encrypted.size()) {
        log("Warning: Response data size (" + std::to_string(response->processed_data().size()) + 
            ") differs from encrypted data size (" + std::to_string(encrypted.size()) + ")", true);
    }
} catch (const std::exception& e) {
    std::string errorMsg = "Encryption error: " + std::string(e.what());
    log(errorMsg, true);
    
    // Get more detailed OpenSSL error information if available
    char ssl_err_buf[256];
    unsigned long err = ERR_get_error();
    
    if (err != 0) {
        ERR_error_string_n(err, ssl_err_buf, sizeof(ssl_err_buf));
        std::string error_msg = e.what();
        error_msg += " (OpenSSL: ";
        error_msg += ssl_err_buf;
        error_msg += ")";
        response->set_error_message(error_msg);
        log("Encryption error details: " + error_msg, true);
    } else {
        response->set_error_message(e.what());
    }
    
    response->set_success(false);
}

return grpc::Status::OK;
}

grpc::Status EncryptionWorker::DecryptChunk(grpc::ServerContext* context, 
    const encryption::ChunkRequest* request, 
    encryption::ChunkResponse* response) {
try {
    // Convert protobuf bytes to vectors
    std::vector<unsigned char> encryptedData(request->data().begin(), request->data().end());
    std::string key(request->key().begin(), request->key().end());
    std::string iv(request->iv().begin(), request->iv().end());
    
    // Print size information for debugging
    log("Decrypting chunk ID: " + std::to_string(request->chunk_id()) + 
        ", Size: " + std::to_string(encryptedData.size()) + " bytes");
    
    // Check if the data is aligned with AES block size (16 bytes)
    bool isBlockAligned = encryptedData.size() % 16 == 0 && encryptedData.size() > 16;
    
    if (isBlockAligned) {
        log("Using special handling for block-aligned data");
    }

    // Clear any previous OpenSSL errors
    ERR_clear_error();

    // Decrypt the data
    auto startTime = std::chrono::high_resolution_clock::now();
    auto decrypted = AESCrypto::decrypt(encryptedData, key, iv);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    log("Decryption completed (" + std::to_string(decrypted.size()) + " bytes) in " + 
        std::to_string(duration) + " ms");

    // Set the response
    response->set_processed_data(decrypted.data(), decrypted.size());
    response->set_chunk_id(request->chunk_id());
    response->set_success(true);
    
    // Verify the response data size matches what was decrypted
    if (response->processed_data().size() != decrypted.size()) {
        log("Warning: Response data size (" + std::to_string(response->processed_data().size()) + 
            ") differs from decrypted data size (" + std::to_string(decrypted.size()) + ")", true);
    }
} catch (const std::exception& e) {
    response->set_success(false);
    
    // Get more detailed OpenSSL error information
    char ssl_err_buf[256];
    unsigned long err = ERR_get_error();
    
    if (err != 0) {
        ERR_error_string_n(err, ssl_err_buf, sizeof(ssl_err_buf));
        std::string error_msg = e.what();
        error_msg += " (OpenSSL: ";
        error_msg += ssl_err_buf;
        error_msg += ")";
        response->set_error_message(error_msg);
        log("Decryption error: " + error_msg, true);
    } else {
        response->set_error_message(e.what());
        log("Decryption error: " + std::string(e.what()), true);
    }
}

return grpc::Status::OK;
}

void EncryptionWorker::runServer(const std::string& serverAddress, bool useTLS) {
    // Initialize worker log file if not already open
    if (!workerLogFile.is_open()) {
        workerLogFile.open("worker_debug.log", std::ios::app);
    }

    log("Starting worker server at " + serverAddress + (useTLS ? " (with TLS)" : " (insecure)"));
    
    grpc::ServerBuilder builder;
    
    if (useTLS) {
        try {
            grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {
                ReadFile("server.key"),
                ReadFile("server.crt")
            };
            
            grpc::SslServerCredentialsOptions ssl_opts;
            ssl_opts.pem_root_certs = ReadFile("ca.crt");
            ssl_opts.pem_key_cert_pairs.push_back(keycert);
            
            builder.AddListeningPort(serverAddress, grpc::SslServerCredentials(ssl_opts));
            log("TLS security configured successfully");
        } catch (const std::exception& e) {
            log("TLS setup failed: " + std::string(e.what()), true);
            throw;
        }
    } else {
        builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    }
    
    // Add debug resource usage reporting
    log("Current working directory: " + std::string(getenv("PWD") ? getenv("PWD") : "unknown"));
    
    // Display system information
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        log("System memory usage: " + std::to_string(memInfo.dwMemoryLoad) + "%");
        log("Available physical memory: " + 
            std::to_string(memInfo.ullAvailPhys / (1024*1024)) + " MB");
    }
    
    builder.RegisterService(this);
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    log("Worker server listening on " + serverAddress + 
        (useTLS ? " (with TLS)" : " (insecure)"));
    server->Wait();
}

grpc::Status EncryptionWorker::TestConnection(
    grpc::ServerContext* context,
    const encryption::TestRequest* request,
    encryption::TestResponse* response) {
    log("Received test connection request");
    response->set_alive(true);
    response->set_worker_id("worker_001");
    response->set_status("ready");
    response->set_timestamp(time(nullptr));
    
    // Add more detailed worker status
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        log("Worker memory usage: " + std::to_string(pmc.WorkingSetSize / (1024*1024)) + " MB");
    }
    
    log("Test connection response sent");
    return grpc::Status::OK;
}
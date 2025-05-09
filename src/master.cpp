#include "master.h"
#include "utilities.h"  // For ReadFile if using TLS
#include <thread>
#include <future>
#include <iostream>
#include <vector>
#include <memory>
#include "encryption.grpc.pb.h"
#include <chrono>
#include <atomic>
#include <mutex>
#include <fstream>  // Added for ofstream
#include <filesystem> // Added for path operations
#include <direct.h>  // Added for _getcwd

// Constructor implementation
EncryptionMaster::EncryptionMaster(const std::vector<std::string>& workerAddresses, bool useTLS) 
    : useTLS_(useTLS) {
    std::cout << "Initializing EncryptionMaster with " << workerAddresses.size() << " workers" << std::endl;
    for (const auto& address : workerAddresses) {
        std::cout << "Creating channel to worker at " << address << (useTLS_ ? " (TLS)" : " (insecure)") << std::endl;
        auto channel = createChannel(address);  // Use the channel creation method
        stubs_.push_back(encryption::EncryptionService::NewStub(channel));
    }
    std::cout << "Created " << stubs_.size() << " worker stubs" << std::endl;
}

// Channel creation method
std::shared_ptr<grpc::Channel> EncryptionMaster::createChannel(const std::string& address) {
    if (useTLS_) {
        grpc::SslCredentialsOptions ssl_opts;
        try {
            ssl_opts.pem_root_certs = ReadFile("ca.crt");
            return grpc::CreateChannel(address, grpc::SslCredentials(ssl_opts));
        } catch (const std::exception& e) {
            std::cerr << "Failed to load TLS credentials: " << e.what() << std::endl;
            throw;
        }
    } else {
        return grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    }
}

// Encrypt file implementation
std::vector<FileChunk> EncryptionMaster::encryptFile(const std::string& filePath,
                                                   size_t chunkSize,
                                                   const std::string& key,
                                                   const std::string& iv) {
    // Split file into chunks
    auto chunks = FileChunker::chunkFile(filePath, chunkSize);
    std::cout << "Split file into " << chunks.size() << " chunks" << std::endl;
    
    if (chunks.empty()) {
        std::cerr << "Warning: No chunks created from file" << std::endl;
        return std::vector<FileChunk>();
    }
    
    std::vector<FileChunk> encryptedChunks(chunks.size());
    
    // Process chunks sequentially for simplicity and reliability
    for (size_t i = 0; i < chunks.size(); ++i) {
        size_t workerIndex = i % stubs_.size();
        std::cout << "Processing chunk " << i << " with worker " << workerIndex << std::endl;
        
        try {
            encryption::ChunkRequest request;
            request.set_data(chunks[i].data.data(), chunks[i].data.size());
            request.set_chunk_id(chunks[i].id);
            request.set_key(key.data(), key.size());
            request.set_iv(iv.data(), iv.size());
            
            encryption::ChunkResponse response;
            grpc::ClientContext context;
            
            // Set a longer timeout for encryption (30 seconds)
            auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(30);
            context.set_deadline(deadline);
            
            std::cout << "Sending EncryptChunk request for chunk " << i << " (" << chunks[i].data.size() << " bytes)" << std::endl;
            grpc::Status status = stubs_[workerIndex]->EncryptChunk(&context, request, &response);
            
            if (!status.ok() || !response.success()) {
                std::string error = "Encryption failed for chunk " + std::to_string(i);
                if (!status.ok()) {
                    error += ", gRPC status: " + status.error_message();
                    error += ", Error code: " + std::to_string(status.error_code());
                    error += ", Details: " + status.error_details();
                }
                if (!response.success()) {
                    error += ", Response message: " + response.error_message();
                }
                
                std::cerr << error << std::endl;
                throw std::runtime_error(error);
            }
            
            std::cout << "Successfully encrypted chunk " << i << " (" << response.processed_data().size() << " bytes)" << std::endl;
            
            FileChunk encryptedChunk;
            encryptedChunk.id = response.chunk_id();
            encryptedChunk.data.assign(response.processed_data().begin(), 
                                     response.processed_data().end());
            encryptedChunks[i] = encryptedChunk;
            
        } catch (const std::exception& e) {
            std::cerr << "Exception processing chunk " << i << ": " << e.what() << std::endl;
            throw; // Re-throw to propagate the error
        }
    }
    
    std::cout << "All chunks processed successfully" << std::endl;
    
    // Write encrypted chunks directly to output file
    std::string outputFilePath = filePath + ".encrypted";
    std::cout << "Writing encrypted output directly to: " << outputFilePath << std::endl;
    
    // Try direct file writing
    std::ofstream outFile(outputFilePath, std::ios::binary);
    if (outFile.is_open()) {
        size_t totalBytes = 0;
        for (const auto& chunk : encryptedChunks) {
            if (!chunk.data.empty()) {
                outFile.write(chunk.data.data(), chunk.data.size());
                totalBytes += chunk.data.size();
            }
        }
        outFile.close();
        std::cout << "Successfully wrote " << totalBytes << " bytes to " << outputFilePath << std::endl;
    } else {
        std::cerr << "Failed to open output file for writing: " << outputFilePath << std::endl;
    }
    
    return encryptedChunks;
}

// Add to master.cpp
bool EncryptionMaster::testWorkerConnections() {
    std::cout << "Testing connections to " << stubs_.size() << " workers..." << std::endl;
    for (size_t i = 0; i < stubs_.size(); ++i) {
        grpc::ClientContext context;
        encryption::TestRequest request;
        encryption::TestResponse response;

        // Set a timeout (5 seconds)
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
        context.set_deadline(deadline);

        // Add optional test message if needed
        request.set_test_message("ping");
        
        std::cout << "Testing worker " << i << "..." << std::endl;
        grpc::Status status = stubs_[i]->TestConnection(&context, request, &response);

        if (!status.ok()) {
            std::cerr << "Worker " << i << " connection failed: " 
                     << status.error_message() << std::endl;
            std::cerr << "Error details: " << status.error_details() << std::endl;
            std::cerr << "Error code: " << status.error_code() << std::endl;
            return false;
        }

        // Check response content
        if (!response.alive()) {
            std::cerr << "Worker " << i << " reported unhealthy status" << std::endl;
            return false;
        }
        
        std::cout << "Connection to worker " << i << " successful (ID: " 
                 << response.worker_id() << ", Status: " << response.status() << ")" << std::endl;
    }
    
    std::cout << "All worker connections tested successfully" << std::endl;
    return true;
}

std::vector<FileChunk> EncryptionMaster::decryptFile(const std::string& filePath,
                                                   size_t chunkSize,
                                                   const std::string& key,
                                                   const std::string& iv) {
    // Split file into chunks
    auto chunks = FileChunker::chunkFile(filePath, chunkSize);
    std::vector<FileChunk> decryptedChunks(chunks.size());
    
    std::cout << "Decrypting file with " << chunks.size() << " chunks" << std::endl;
    
    // Process chunks sequentially (required for CBC mode)
    for (size_t i = 0; i < chunks.size(); ++i) {
        size_t workerIndex = i % stubs_.size();
        
        // Special handling for chunks at the AES block boundary
        // Check if chunk size is divisible by 16 (AES block size)
        bool isBlockAligned = chunks[i].data.size() % 16 == 0;
        
        std::cout << "Processing chunk " << i << " (" << chunks[i].data.size() 
                  << " bytes) on worker " << workerIndex
                  << ", block aligned: " << (isBlockAligned ? "yes" : "no") << std::endl;
        
        encryption::ChunkRequest request;
        request.set_data(chunks[i].data.data(), chunks[i].data.size());
        request.set_chunk_id(chunks[i].id);
        request.set_key(key.data(), key.size());
        request.set_iv(iv.data(), iv.size());
        
        // Don't try to set block_aligned - we removed this earlier to fix protobuf issue
        // We'll rely on the worker detecting this based on the data size
        
        encryption::ChunkResponse response;
        grpc::ClientContext context;
        
        // Set a timeout for the operation
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
        context.set_deadline(deadline);
        
        std::cout << "Sending DecryptChunk request for chunk " << i << std::endl;
        grpc::Status status = stubs_[workerIndex]->DecryptChunk(&context, request, &response);
        
        if (!status.ok() || !response.success()) {
            // If decryption failed and the chunk was block-aligned, try with a slightly modified approach
            if (isBlockAligned && chunks[i].data.size() >= 16) {
                std::cout << "Retrying decryption for block-aligned chunk " << i << std::endl;
                
                // Create a new request with the data split into two parts
                // This avoids the AES padding issue at block boundaries
                size_t firstPartSize = chunks[i].data.size() - 16;
                
                encryption::ChunkRequest firstRequest;
                firstRequest.set_data(chunks[i].data.data(), firstPartSize);
                firstRequest.set_chunk_id(chunks[i].id);
                firstRequest.set_key(key.data(), key.size());
                firstRequest.set_iv(iv.data(), iv.size());
                
                encryption::ChunkResponse firstResponse;
                grpc::ClientContext firstContext;
                
                firstContext.set_deadline(deadline); // Use the same deadline
                
                grpc::Status firstStatus = stubs_[workerIndex]->DecryptChunk(&firstContext, firstRequest, &firstResponse);
                
                if (!firstStatus.ok() || !firstResponse.success()) {
                    std::string error = "Decryption failed for chunk " + std::to_string(i);
                    if (!firstStatus.ok()) {
                        error += ", Status: " + firstStatus.error_message();
                    }
                    if (!firstResponse.success()) {
                        error += ", Error: " + firstResponse.error_message();
                    }
                    std::cerr << error << std::endl;
                    throw std::runtime_error(error);
                }
                
                encryption::ChunkRequest secondRequest;
                secondRequest.set_data(chunks[i].data.data() + firstPartSize, 16);
                secondRequest.set_chunk_id(chunks[i].id + 1000); // Use a different ID for the second part
                secondRequest.set_key(key.data(), key.size());
                secondRequest.set_iv(iv.data(), iv.size());
                
                encryption::ChunkResponse secondResponse;
                grpc::ClientContext secondContext;
                
                secondContext.set_deadline(deadline); // Use the same deadline
                
                grpc::Status secondStatus = stubs_[workerIndex]->DecryptChunk(&secondContext, secondRequest, &secondResponse);
                
                if (!secondStatus.ok() || !secondResponse.success()) {
                    std::string error = "Decryption failed for chunk " + std::to_string(i) + " (part 2)";
                    if (!secondStatus.ok()) {
                        error += ", Status: " + secondStatus.error_message();
                    }
                    if (!secondResponse.success()) {
                        error += ", Error: " + secondResponse.error_message();
                    }
                    std::cerr << error << std::endl;
                    throw std::runtime_error(error);
                }
                
                // Combine the two decrypted parts
                FileChunk decryptedChunk;
                decryptedChunk.id = chunks[i].id;
                
                // Combine the first and second parts
                std::vector<char> combinedData;
                combinedData.reserve(firstResponse.processed_data().size() + secondResponse.processed_data().size());
                combinedData.insert(combinedData.end(), firstResponse.processed_data().begin(), firstResponse.processed_data().end());
                combinedData.insert(combinedData.end(), secondResponse.processed_data().begin(), secondResponse.processed_data().end());
                
                decryptedChunk.data = std::move(combinedData);
                decryptedChunks[i] = decryptedChunk;
                
                std::cout << "Successfully decrypted chunk " << i << " (split approach)" << std::endl;
            } else {
                std::string error = "Decryption failed for chunk " + std::to_string(i);
                if (!status.ok()) {
                    error += ", Status: " + status.error_message();
                }
                if (!response.success()) {
                    error += ", Error: " + response.error_message();
                }
                std::cerr << error << std::endl;
                throw std::runtime_error(error);
            }
        } else {
            FileChunk decryptedChunk;
            decryptedChunk.id = response.chunk_id();
            decryptedChunk.data.assign(response.processed_data().begin(), 
                                     response.processed_data().end());
            decryptedChunks[i] = decryptedChunk;
            std::cout << "Successfully decrypted chunk " << i << std::endl;
        }
    }
    
    std::cout << "All chunks decrypted successfully" << std::endl;
    
    // Write decrypted chunks directly to output file
    std::string outputFilePath = filePath + ".decrypted";
    std::cout << "Writing decrypted output directly to: " << outputFilePath << std::endl;
    
    // Try direct file writing
    std::ofstream outFile(outputFilePath, std::ios::binary);
    if (outFile.is_open()) {
        size_t totalBytes = 0;
        for (const auto& chunk : decryptedChunks) {
            if (!chunk.data.empty()) {
                outFile.write(chunk.data.data(), chunk.data.size());
                totalBytes += chunk.data.size();
            }
        }
        outFile.close();
        std::cout << "Successfully wrote " << totalBytes << " bytes to " << outputFilePath << std::endl;
    } else {
        std::cerr << "Failed to open output file for writing: " << outputFilePath << std::endl;
    }
    
    return decryptedChunks;
}

// Add the implementation of writeProcessedDataToFile at the end of the file
bool EncryptionMaster::writeProcessedDataToFile(const std::string& outputPath, const std::vector<FileChunk>& chunks) {
    std::cout << "\n=== FILE WRITING DIAGNOSTICS ===\n";
    std::cout << "Writing processed data to file: \"" << outputPath << "\"" << std::endl;
    
    // First, report complete environment information
    char currentDir[MAX_PATH];
    if (_getcwd(currentDir, MAX_PATH) != NULL) {
        std::cout << "Current working directory: " << currentDir << std::endl;
    } else {
        std::cerr << "Unable to get working directory" << std::endl;
    }
    
    if (chunks.empty()) {
        std::cerr << "Error: No chunks to write to output file" << std::endl;
        return false;
    }

    // Count total bytes for reporting
    size_t totalChunkBytes = 0;
    for (const auto& chunk : chunks) {
        totalChunkBytes += chunk.data.size();
    }
    std::cout << "Total data in chunks: " << totalChunkBytes << " bytes in " << chunks.size() << " chunks" << std::endl;
    
    // Create parent directory if it doesn't exist
    std::filesystem::path filePath(outputPath);
    std::filesystem::path parentDir = filePath.parent_path();
    std::cout << "Output file path: " << filePath.string() << std::endl;
    std::cout << "Parent directory: " << parentDir.string() << std::endl;
    
    if (!parentDir.empty()) {
        try {
            // Check if directory exists first
            bool dirExists = std::filesystem::exists(parentDir);
            std::cout << "Directory exists: " << (dirExists ? "Yes" : "No") << std::endl;
            
            if (!dirExists) {
                bool created = std::filesystem::create_directories(parentDir);
                std::cout << "Directory creation result: " << (created ? "Success" : "Failed") << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error working with directory: " << e.what() << std::endl;
            // Continue anyway, in case directory actually exists
        }
    }
    
    // For debugging, try to create a test file in same directory
    std::string testFilePath = filePath.parent_path().string() + "\\test_write_permissions.txt";
    {
        std::cout << "Testing write permissions by creating test file: " << testFilePath << std::endl;
        std::ofstream testFile(testFilePath);
        if (testFile.is_open()) {
            testFile << "Test file created at: " << std::time(nullptr) << std::endl;
            testFile << "This is to test write permissions." << std::endl;
            testFile.close();
            std::cout << "Successfully created test file" << std::endl;
        } else {
            std::cerr << "Failed to create test file: " << strerror(errno) << " (code: " << errno << ")" << std::endl;
        }
    }
    
    // Try direct std::ofstream approach first
    {
        std::cout << "METHOD 1: Attempting to write file using std::ofstream: " << outputPath << std::endl;
        std::ofstream outFile(outputPath, std::ios::binary);
        if (outFile.is_open()) {
            std::cout << "File opened successfully with std::ofstream" << std::endl;
            
            // Sort chunks by ID before writing
            std::vector<FileChunk> sortedChunks = chunks;
            std::sort(sortedChunks.begin(), sortedChunks.end(), 
                      [](const FileChunk& a, const FileChunk& b) { return a.id < b.id; });
            
            size_t totalBytes = 0;
            for (const auto& chunk : sortedChunks) {
                if (chunk.data.empty()) {
                    std::cerr << "Warning: Chunk " << chunk.id << " is empty, skipping" << std::endl;
                    continue;
                }
                
                outFile.write(chunk.data.data(), chunk.data.size());
                if (outFile.fail()) {
                    std::cerr << "Error writing chunk " << chunk.id << " to file" << std::endl;
                    outFile.close();
                    
                    // Try to diagnose the issue
                    std::cerr << "Write error: " << strerror(errno) << " (code: " << errno << ")" << std::endl;
                } else {
                    totalBytes += chunk.data.size();
                    std::cout << "Wrote chunk " << chunk.id << " (" << chunk.data.size() << " bytes)" << std::endl;
                }
            }
            
            outFile.close();
            std::cout << "Closed output file after writing " << totalBytes << " bytes" << std::endl;
            
            // Verify file was created successfully
            try {
                if (std::filesystem::exists(outputPath)) {
                    auto fileSize = std::filesystem::file_size(outputPath);
                    std::cout << "Successfully created output file: " << outputPath 
                              << " (size: " << fileSize << " bytes)" << std::endl;
                    return true;
                } else {
                    std::cerr << "File writing appeared to succeed but file doesn't exist: " << outputPath << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error verifying file: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "Failed to open output file with std::ofstream: " << outputPath << std::endl;
            std::cerr << "Error: " << strerror(errno) << " (code: " << errno << ")" << std::endl;
        }
    }
    
    // Try Windows API direct file creation
    {
        std::cout << "METHOD 2: Attempting to write file using Windows API: " << outputPath << std::endl;
        
        std::wstring wideOutputPath = StringToWString(outputPath);
        HANDLE hFile = CreateFileW(
            wideOutputPath.c_str(),
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hFile != INVALID_HANDLE_VALUE) {
            std::cout << "File opened successfully with Windows API" << std::endl;
            
            // Sort chunks by ID before writing
            std::vector<FileChunk> sortedChunks = chunks;
            std::sort(sortedChunks.begin(), sortedChunks.end(), 
                      [](const FileChunk& a, const FileChunk& b) { return a.id < b.id; });
            
            size_t totalBytes = 0;
            bool success = true;
            for (const auto& chunk : sortedChunks) {
                if (chunk.data.empty()) {
                    std::cerr << "Warning: Chunk " << chunk.id << " is empty, skipping" << std::endl;
                    continue;
                }
                
                DWORD bytesWritten = 0;
                if (!WriteFile(
                    hFile,
                    chunk.data.data(),
                    static_cast<DWORD>(chunk.data.size()),
                    &bytesWritten,
                    NULL
                )) {
                    DWORD error = GetLastError();
                    std::cerr << "Error writing chunk " << chunk.id << " to file: " << error << std::endl;
                    success = false;
                    break;
                }
                
                totalBytes += bytesWritten;
                std::cout << "Wrote chunk " << chunk.id << " (" << bytesWritten << " bytes)" << std::endl;
            }
            
            FlushFileBuffers(hFile);
            CloseHandle(hFile);
            std::cout << "Closed output file after writing " << totalBytes << " bytes" << std::endl;
            
            if (success) {
                try {
                    if (std::filesystem::exists(outputPath)) {
                        auto fileSize = std::filesystem::file_size(outputPath);
                        std::cout << "Successfully created output file: " << outputPath 
                                  << " (size: " << fileSize << " bytes)" << std::endl;
                        return true;
                    } else {
                        std::cerr << "File writing appeared to succeed but file doesn't exist: " << outputPath << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error verifying file: " << e.what() << std::endl;
                }
            }
        } else {
            DWORD error = GetLastError();
            std::cerr << "Failed to open output file with Windows API: " << outputPath << std::endl;
            std::cerr << "Error: " << error << std::endl;
        }
    }
    
    // If we get here, try FileChunker as a last resort
    std::cout << "METHOD 3: Falling back to FileChunker::reassembleFile method" << std::endl;
    return FileChunker::reassembleFile(outputPath, chunks);
}
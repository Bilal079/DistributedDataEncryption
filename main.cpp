// main.cpp
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <random>
#include <filesystem>
#include <atomic>
#include <thread>
#include <mutex>
#include <ctime>
#include <grpcpp/grpcpp.h>
#include "master.h"
#include "worker.h"
#include "chunk.h"
#include "crypto.h"
#include "utilities.h"
#include <windows.h> // For Windows-specific file operations
#include "dropbox_client.h"
#include "config.h"
#include "encryption.grpc.pb.h"

using namespace std;
using namespace std::chrono;
namespace fs = std::filesystem;

const string DEFAULT_WORKER_PORT = "50051";
const size_t DEFAULT_CHUNK_SIZE = 1024 * 1024; // 1MB

// Global log file (static to ensure it persists)
static ofstream debugLogFile;

// Log function for detailed debugging
void logMessage(const string& message, bool isError = false) {
    auto now = system_clock::now();
    auto time = system_clock::to_time_t(now);
    
    stringstream timestamp;
    timestamp << put_time(localtime(&time), "%Y-%m-%d %H:%M:%S");
    
    string prefix = isError ? "[ERROR]" : "[INFO]";
    
    if (isError) {
        cerr << prefix << " " << timestamp.str() << " - " << message << endl;
    } else {
        cout << prefix << " " << timestamp.str() << " - " << message << endl;
    }
    
    // Log to file if open
    if (debugLogFile.is_open()) {
        debugLogFile << prefix << " " << timestamp.str() << " - " << message << endl;
        debugLogFile.flush();
    }
}

// Verify file can be accessed and determine its size
bool verifyFileExists(const string& filePath, size_t& fileSize) {
    // Log the verification attempt
    logMessage("Verifying file exists: " + filePath);
    
    // First try Windows API directly (most reliable on Windows)
    std::wstring wideFilePath = StringToWString(filePath);
    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    if (GetFileAttributesExW(wideFilePath.c_str(), GetFileExInfoStandard, &fileAttr)) {
        ULARGE_INTEGER fileSize64;
        fileSize64.LowPart = fileAttr.nFileSizeLow;
        fileSize64.HighPart = fileAttr.nFileSizeHigh;
        fileSize = static_cast<size_t>(fileSize64.QuadPart);
        logMessage("Windows API verified file exists, size: " + to_string(fileSize) + " bytes");
        return true;
    }
    
    DWORD winError = GetLastError();
    logMessage("Windows API GetFileAttributesExW failed, error: " + to_string(winError));
    
    // Try standard C++ filesystem
    error_code ec;
    if (filesystem::exists(filePath, ec)) {
        if (!ec) {
            fileSize = filesystem::file_size(filePath, ec);
            if (!ec) {
                logMessage("std::filesystem verified file exists, size: " + to_string(fileSize) + " bytes");
                return true;
            }
            logMessage("Error getting file size with std::filesystem: " + ec.message(), true);
        }
    } else if (ec) {
        logMessage("Error checking if file exists with std::filesystem: " + ec.message(), true);
    } else {
        logMessage("std::filesystem reports file does not exist", true);
    }
    
    // Last resort: try to open the file directly
    ifstream testFile(filePath, ios::binary | ios::ate);
    if (testFile.is_open()) {
        fileSize = static_cast<size_t>(testFile.tellg());
        testFile.close();
        logMessage("Directly opened file successfully, size: " + to_string(fileSize) + " bytes");
        return true;
    }
    
    // Try with Windows API CreateFile as absolute last resort
    HANDLE hFile = CreateFileW(
        wideFilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fileSize64;
        if (GetFileSizeEx(hFile, &fileSize64)) {
            fileSize = static_cast<size_t>(fileSize64.QuadPart);
            CloseHandle(hFile);
            logMessage("Windows API CreateFile verified file exists, size: " + to_string(fileSize) + " bytes");
            return true;
        }
        CloseHandle(hFile);
    }
    
    // File could not be accessed by any method
    logMessage("File verification failed: " + filePath, true);
    return false;
}

void printHelp() {
    cout << "Distributed Encryption System\n";
    cout << "Usage:\n";
    cout << "  To run as worker: ./program worker <address:port> [--tls]\n";
    cout << "  To run as master: ./program master <input> <output> <worker1> [worker2...] [--tls]\n";
    cout << "  To encrypt: ./program encrypt <input> <output> <worker1> [worker2...] [--tls]\n";
    cout << "  To decrypt: ./program decrypt <input> <output> <worker1> [worker2...] [--tls]\n";
    cout << "  To configure Dropbox: ./program dropbox-config <access_token> [folder]\n";
    cout << "  To upload to Dropbox: ./program dropbox-upload <local_file> [dropbox_path]\n";
    cout << "  To download from Dropbox: ./program dropbox-download <dropbox_path> <local_file>\n";
    cout << "  To list Dropbox files: ./program dropbox-list [folder]\n\n";
    cout << "Examples:\n";
    cout << "  Worker: ./program worker 0.0.0.0:50051\n";
    cout << "  Master: ./program master input.txt encrypted.bin 192.168.1.100:50051 192.168.1.101:50051\n";
    cout << "  Encrypt: ./program encrypt input.txt encrypted.bin 192.168.1.100:50051 --tls\n";
    cout << "  Configure Dropbox: ./program dropbox-config YOUR_ACCESS_TOKEN /encryption_files\n";
    cout << "  Upload to Dropbox: ./program dropbox-upload encrypted.bin /encryption_files/encrypted.bin\n";
}

void runWorker(const string& address, bool useTLS) {
    logMessage("Starting worker on " + address + (useTLS ? " (TLS enabled)" : ""));
    EncryptionWorker worker;
    worker.runServer(address, useTLS);
}

// Function to handle Dropbox operations
bool handleDropboxOperation(const string& operation, int argc, char* argv[]) {
    // Try to load config
    if (!Config::loadConfig()) {
        if (operation != "dropbox-config") {
            cerr << "Error: Dropbox not configured. Run 'dropbox-config' first." << endl;
            return false;
        }
    }
    
    // Handle different operations
    if (operation == "dropbox-config") {
        if (argc < 3) {
            cerr << "Error: Missing access token" << endl;
            return false;
        }
        
        string accessToken = argv[2];
        string folder = (argc > 3) ? argv[3] : "";
        
        if (Config::saveConfig(accessToken, folder)) {
            cout << "Dropbox configuration saved successfully" << endl;
            return true;
        } else {
            cerr << "Failed to save Dropbox configuration" << endl;
            return false;
        }
    } 
    else if (operation == "dropbox-upload") {
        if (argc < 3) {
            cerr << "Error: Missing local file path" << endl;
            return false;
        }
        
        string localFile = argv[2];
        string dropboxPath;
        
        if (argc > 3) {
            dropboxPath = argv[3];
        } else {
            // Extract filename from local path
            size_t lastSlash = localFile.find_last_of("/\\");
            string filename = (lastSlash != string::npos) ? localFile.substr(lastSlash + 1) : localFile;
            
            // Combine with configured folder
            dropboxPath = Config::getDropboxFolder();
            if (!dropboxPath.empty() && dropboxPath.back() != '/') {
                dropboxPath += '/';
            }
            dropboxPath += filename;
        }
        
        // Initialize Dropbox client
        DropboxClient client(Config::getDropboxAccessToken());
        if (!client.initialize()) {
            cerr << "Failed to initialize Dropbox client" << endl;
            return false;
        }
        
        // Upload file
        return client.uploadFile(localFile, dropboxPath);
    } 
    else if (operation == "dropbox-download") {
        if (argc < 4) {
            cerr << "Error: Missing parameters. Usage: dropbox-download <dropbox_path> <local_file>" << endl;
            return false;
        }
        
        string dropboxPath = argv[2];
        string localFile = argv[3];
        
        // Initialize Dropbox client
        DropboxClient client(Config::getDropboxAccessToken());
        if (!client.initialize()) {
            cerr << "Failed to initialize Dropbox client" << endl;
            return false;
        }
        
        // Download file
        return client.downloadFile(dropboxPath, localFile);
    } 
    else if (operation == "dropbox-list") {
        string folder = (argc > 2) ? argv[2] : Config::getDropboxFolder();
        
        // Initialize Dropbox client
        DropboxClient client(Config::getDropboxAccessToken());
        if (!client.initialize()) {
            cerr << "Failed to initialize Dropbox client" << endl;
            return false;
        }
        
        // List files
        return client.listFiles(folder);
    }
    
    return false;
}

// Simple client for connecting directly to worker services
class EncryptionWorkerClient {
public:
    EncryptionWorkerClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(encryption::EncryptionService::NewStub(channel)) {}
    
    FileChunk EncryptChunk(const FileChunk& chunk, const std::string& key, const std::string& iv) {
        encryption::ChunkRequest request;
        request.set_data(chunk.data.data(), chunk.data.size());
        request.set_chunk_id(chunk.id);
        request.set_key(key.data(), key.size());
        request.set_iv(iv.data(), iv.size());
        
        encryption::ChunkResponse response;
        grpc::ClientContext context;
        
        // Set a timeout for the operation
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
        context.set_deadline(deadline);
        
        grpc::Status status = stub_->EncryptChunk(&context, request, &response);
        
        if (!status.ok() || !response.success()) {
            std::string error = "Encryption failed on worker";
            if (!status.ok()) {
                error += ": gRPC error: " + status.error_message();
            } else {
                error += ": " + response.error_message();
            }
            throw std::runtime_error(error);
        }
        
        FileChunk encryptedChunk;
        encryptedChunk.id = response.chunk_id();
        encryptedChunk.data.assign(response.processed_data().begin(), response.processed_data().end());
        return encryptedChunk;
    }
    
    FileChunk DecryptChunk(const FileChunk& chunk, const std::string& key, const std::string& iv) {
        encryption::ChunkRequest request;
        request.set_data(chunk.data.data(), chunk.data.size());
        request.set_chunk_id(chunk.id);
        request.set_key(key.data(), key.size());
        request.set_iv(iv.data(), iv.size());
        
        encryption::ChunkResponse response;
        grpc::ClientContext context;
        
        // Set a timeout for the operation
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
        context.set_deadline(deadline);
        
        grpc::Status status = stub_->DecryptChunk(&context, request, &response);
        
        if (!status.ok() || !response.success()) {
            std::string error = "Decryption failed on worker";
            if (!status.ok()) {
                error += ": gRPC error: " + status.error_message();
            } else {
                error += ": " + response.error_message();
            }
            throw std::runtime_error(error);
        }
        
        FileChunk decryptedChunk;
        decryptedChunk.id = response.chunk_id();
        decryptedChunk.data.assign(response.processed_data().begin(), response.processed_data().end());
        return decryptedChunk;
    }
    
private:
    std::unique_ptr<encryption::EncryptionService::Stub> stub_;
};

void processFile(const vector<string>& workerAddresses, const string& inputFile, string outputFile, bool encryptMode, bool useTLS, bool uploadToDropbox = false) {
    auto start = high_resolution_clock::now();
    
    logMessage("Processing file: " + inputFile + " -> " + outputFile);
    
    // If outputFile doesn't already have the encryption/decryption extension, add it
    string originalOutputFile = outputFile;
    if (encryptMode && outputFile.find(".encrypted") == string::npos) {
        outputFile += ".encrypted";
        logMessage("Adding .encrypted extension to output file: " + outputFile);
    } else if (!encryptMode && outputFile.find(".decrypted") == string::npos) {
        outputFile += ".decrypted";
        logMessage("Adding .decrypted extension to output file: " + outputFile);
    }
    
    try {
        // Get absolute paths for input/output files
        char absInputPath[MAX_PATH];
        char absOutputPath[MAX_PATH];
        
        if (_fullpath(absInputPath, inputFile.c_str(), MAX_PATH) == nullptr) {
            logMessage("Error resolving full path for input: " + inputFile, true);
            return;
        }
        
        if (_fullpath(absOutputPath, outputFile.c_str(), MAX_PATH) == nullptr) {
            logMessage("Error resolving full path for output file: " + outputFile, true);
            return;
        }
        
        string resolvedInputPath(absInputPath);
        string resolvedOutputPath(absOutputPath);
        
        logMessage("Using input file: " + resolvedInputPath);
        logMessage("Output will be saved to: " + resolvedOutputPath);
        
        // Create output directory if it doesn't exist
        string outputDir = resolvedOutputPath.substr(0, resolvedOutputPath.find_last_of("/\\"));
        if (!outputDir.empty()) {
            try {
                filesystem::create_directories(outputDir);
                logMessage("Ensured output directory exists: " + outputDir);
            } catch (const exception& e) {
                logMessage("Error creating output directory: " + string(e.what()), true);
                
                // Try Windows API as fallback
                std::wstring wideOutputDir = StringToWString(outputDir);
                if (!CreateDirectoryW(wideOutputDir.c_str(), NULL) && 
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                    logMessage("Windows API also failed to create directory: " + to_string(GetLastError()), true);
                    return;
                }
            }
        }
        
        // Check if input file exists and get its size
        size_t inputFileSize = 0;
        if (!verifyFileExists(resolvedInputPath, inputFileSize)) {
            logMessage("Error: Input file does not exist or cannot be read: " + resolvedInputPath, true);
            return;
        }
        logMessage("Input file size: " + to_string(inputFileSize) + " bytes");
        
        // Try to create a temporary file in the same location to test permissions
        string testFilePath = resolvedOutputPath + ".test";
        {
            ofstream testFile(testFilePath, ios::binary);
            if (!testFile.is_open()) {
                logMessage("Error: Cannot create output file due to permissions: " + testFilePath, true);
                error_code ec(errno, system_category());
                logMessage("System error: " + to_string(ec.value()) + " - " + ec.message(), true);
                
                // Try using Windows API as a fallback
                std::wstring wideTestFilePath = StringToWString(testFilePath);
                HANDLE hFile = CreateFileW(
                    wideTestFilePath.c_str(),
                    GENERIC_WRITE,
                    0,
                    NULL,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                
                if (hFile == INVALID_HANDLE_VALUE) {
                    logMessage("Windows API also failed to create test file: " + to_string(GetLastError()), true);
                    return;
                } else {
                    logMessage("Windows API successfully created test file");
                    CloseHandle(hFile);
                }
            } else {
                testFile.close();
            }
            
            // Clean up test file
            std::wstring wideTestFilePath = StringToWString(testFilePath);
            if (!DeleteFileW(wideTestFilePath.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
                logMessage("Warning: Failed to delete test file: " + to_string(GetLastError()), true);
            }
        }
        
        logMessage("Initializing master with " + to_string(workerAddresses.size()) + " worker(s)...");
        EncryptionMaster master(workerAddresses, useTLS);
        
        // Test connections before processing
        logMessage("Testing connections to workers...");
        if (!master.testWorkerConnections()) {
            logMessage("Error: Not all workers are reachable", true);
            return;
        }

        // Generate or get key/IV (in real use, you'd want to store these securely)
        string key, iv;
        string keyFilePath = "encryption_key.bin";
        if (encryptMode) {
            logMessage("Generating encryption key and IV...");
            AESCrypto::generateKeyIV(key, iv);
            
            // For debugging, print key and IV info
            logMessage("Key size: " + to_string(key.size()) + " bytes");
            logMessage("IV size: " + to_string(iv.size()) + " bytes");
            
            // Save key/IV to file for decryption later
            ofstream keyFile(keyFilePath, ios::binary);
            if (!keyFile) {
                logMessage("Error: Failed to open key file for writing: " + keyFilePath, true);
                error_code ec(errno, system_category());
                logMessage("System error: " + to_string(ec.value()) + " - " + ec.message(), true);
                return;
            }
            keyFile.write(key.data(), key.size());
            keyFile.write(iv.data(), iv.size());
            keyFile.close();
            logMessage("Saved encryption key to " + keyFilePath);
        } else {
            // Read key/IV from file
            logMessage("Reading encryption key and IV from " + keyFilePath + "...");
            ifstream keyFile(keyFilePath, ios::binary);
            if (!keyFile) {
                logMessage("Error: Could not find encryption key file: " + keyFilePath, true);
                return;
            }
            
            key.resize(32);
            iv.resize(16);
            keyFile.read(&key[0], 32);
            keyFile.read(&iv[0], 16);
            
            if (keyFile.fail()) {
                logMessage("Error reading encryption key file", true);
                return;
            }
            
            keyFile.close();
            logMessage("Read encryption key from " + keyFilePath);
            logMessage("Key size: " + to_string(key.size()) + " bytes");
            logMessage("IV size: " + to_string(iv.size()) + " bytes");
        }

        // Process the file
        vector<FileChunk> processedChunks;
        try {
            if (encryptMode) {
                logMessage("Encrypting file: " + resolvedInputPath);
                processedChunks = master.encryptFile(resolvedInputPath, DEFAULT_CHUNK_SIZE, key, iv);
                
                // Directly write output file after encryption
                logMessage("Explicitly calling writeProcessedDataToFile for encryption output");
                bool writeSuccess = master.writeProcessedDataToFile(resolvedOutputPath, processedChunks);
                if (!writeSuccess) {
                    logMessage("WARNING: writeProcessedDataToFile returned false", true);
                }
            } else {
                logMessage("Decrypting file: " + resolvedInputPath);
                processedChunks = master.decryptFile(resolvedInputPath, DEFAULT_CHUNK_SIZE, key, iv);
                
                // Directly write output file after decryption
                logMessage("Explicitly calling writeProcessedDataToFile for decryption output");
                bool writeSuccess = master.writeProcessedDataToFile(resolvedOutputPath, processedChunks);
                if (!writeSuccess) {
                    logMessage("WARNING: writeProcessedDataToFile returned false", true);
                }
            }
        } catch (const exception& e) {
            logMessage("Error during file processing: " + string(e.what()), true);
            return;
        }
        
        // Validate chunks before reassembly
        if (processedChunks.empty()) {
            logMessage("Error: No processed chunks returned", true);
            return;
        }
        
        logMessage("Received " + to_string(processedChunks.size()) + " processed chunks");
        
        // Count total bytes in chunks for validation
        size_t totalBytes = 0;
        for (const auto& chunk : processedChunks) {
            totalBytes += chunk.data.size();
            if (chunk.data.empty()) {
                logMessage("Warning: Chunk " + to_string(chunk.id) + " is empty", true);
            }
        }
        logMessage("Total bytes in processed chunks: " + to_string(totalBytes));
        
        // Delete any existing output file before reassembly
        if (filesystem::exists(resolvedOutputPath)) {
            logMessage("Removing existing output file...");
            error_code ec;
            filesystem::remove(resolvedOutputPath, ec);
            if (ec) {
                logMessage("Warning: Failed to remove existing output file: " + ec.message(), true);
                // Try Windows API
                std::wstring wideResolvedOutputPath = StringToWString(resolvedOutputPath);
                if (!DeleteFileW(wideResolvedOutputPath.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
                    logMessage("Windows API also failed to delete file: " + to_string(GetLastError()), true);
                }
            }
        }

        // Use the new master method to write the output file
        logMessage("Writing data to output file: " + resolvedOutputPath);
        
        // Make sure we're actually using the decrypted chunks for decryption
        if (!encryptMode && !processedChunks.empty()) {
            logMessage("Ensuring chunks are properly decrypted before writing");
            // Process chunks through decryption workers if not already decrypted
            if (key.empty() || iv.empty()) {
                logMessage("WARNING: Missing encryption key or IV for decryption", true);
            }
            
            // Check if chunks already contain decrypted data by looking at processedChunks
            bool chunksAppearDecrypted = false;
            for (const auto& chunk : processedChunks) {
                // Simple heuristic - encrypted data is usually more random
                // This is just a basic check and might not be foolproof
                if (!chunk.data.empty() && chunk.data[0] != 0) {
                    chunksAppearDecrypted = true;
                    break;
                }
            }
            
            if (!chunksAppearDecrypted) {
                logMessage("Chunks don't appear to be decrypted, forcing decryption process");
                // Create chunks from the input file
                auto encryptedChunks = FileChunker::chunkFile(resolvedInputPath, DEFAULT_CHUNK_SIZE);
                logMessage("Split input file into " + to_string(encryptedChunks.size()) + " chunks for decryption");
                
                // Process each chunk with the worker
                processedChunks.clear();
                processedChunks.resize(encryptedChunks.size());
                
                for (size_t i = 0; i < encryptedChunks.size(); ++i) {
                    size_t workerIndex = i % workerAddresses.size();
                    string workerAddress = workerAddresses[workerIndex];
                    logMessage("Processing chunk " + to_string(i) + " with worker at: " + workerAddress);
                    
                    try {
                        EncryptionWorkerClient worker(grpc::CreateChannel(workerAddress, grpc::InsecureChannelCredentials()));
                        auto decryptedChunk = worker.DecryptChunk(encryptedChunks[i], key, iv);
                        processedChunks[i] = decryptedChunk;
                        logMessage("Successfully decrypted chunk " + to_string(i) + " (" + to_string(decryptedChunk.data.size()) + " bytes)");
                    } catch (const exception& e) {
                        logMessage("Error decrypting chunk " + to_string(i) + ": " + string(e.what()), true);
                        return false;
                    }
                }
                logMessage("All chunks successfully decrypted, ready for reassembly");
            } else {
                logMessage("Chunks appear to already be decrypted, proceeding with reassembly");
            }
        }
        
        bool success = master.writeProcessedDataToFile(resolvedOutputPath, processedChunks);
        
        if (!success) {
            logMessage("Failed to write output file using master component. Attempting alternate methods...", true);
            
            // First, try with direct file reassembly
            logMessage("Attempting to reassemble file using FileChunker...");
            bool reassembled = FileChunker::reassembleFile(resolvedOutputPath, processedChunks);
            
            if (!reassembled) {
                logMessage("FileChunker reassembly failed. Trying direct file writing...", true);
                
                // Try direct file writing with ofstream
                std::ofstream directFile(resolvedOutputPath, std::ios::binary);
                if (directFile.is_open()) {
                    logMessage("Successfully opened file with std::ofstream");
                    
                    // Sort chunks by ID before writing
                    std::vector<FileChunk> sortedChunks = processedChunks;
                    std::sort(sortedChunks.begin(), sortedChunks.end(), 
                              [](const FileChunk& a, const FileChunk& b) { return a.id < b.id; });
                    
                    // Write the processed chunks directly
                    size_t bytesWritten = 0;
                    for (const auto& chunk : sortedChunks) {
                        if (!chunk.data.empty()) {
                            directFile.write(chunk.data.data(), chunk.data.size());
                            bytesWritten += chunk.data.size();
                        }
                    }
                    directFile.close();
                    logMessage("Wrote " + std::to_string(bytesWritten) + " bytes directly to file");
                    
                    success = true;
                } else {
                    logMessage("Failed to create file directly with std::ofstream", true);
                }
            } else {
                success = true;
            }
        }
        
        if (success) {
            auto end = high_resolution_clock::now();
            auto duration = duration_cast<milliseconds>(end - start);
            
            // Verify file exists and get size
            size_t outputFileSize = 0;
            if (verifyFileExists(resolvedOutputPath, outputFileSize)) {
                logMessage("File processed successfully: " + resolvedOutputPath + 
                           " (" + to_string(outputFileSize) + " bytes)");
                logMessage("Time taken: " + to_string(duration.count()) + " ms");
                
                // Test if the file can be opened for reading
                ifstream readTest(resolvedOutputPath, ios::binary);
                if (readTest.is_open()) {
                    readTest.close();
                    logMessage("Verified file can be opened for reading");
                } else {
                    logMessage("Warning: File exists but cannot be opened for reading", true);
                    
                    // Try Windows API as a fallback for verification
                    std::wstring wideOutputPath = StringToWString(resolvedOutputPath);
                    HANDLE hReadTest = CreateFileW(
                        wideOutputPath.c_str(),
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                    );
                    
                    if (hReadTest != INVALID_HANDLE_VALUE) {
                        CloseHandle(hReadTest);
                        logMessage("Verified file can be opened for reading with Windows API");
                    } else {
                        logMessage("File exists but cannot be opened with any method", true);
                    }
                }
            } else {
                logMessage("Warning: reassembleFile returned true but file verification failed: " + 
                          resolvedOutputPath, true);
                logMessage("Attempting to locate output file in the directory...", true);
                
                // Try alternate verification with full Windows API approach
                std::wstring wideOutputPath = StringToWString(resolvedOutputPath);
                HANDLE hFile = CreateFileW(
                    wideOutputPath.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                
                if (hFile != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER fileSize64;
                    if (GetFileSizeEx(hFile, &fileSize64)) {
                        outputFileSize = static_cast<size_t>(fileSize64.QuadPart);
                        logMessage("Windows API direct access succeeded! File size: " + to_string(outputFileSize));
                        CloseHandle(hFile);
                    } else {
                        CloseHandle(hFile);
                        logMessage("Windows API could open the file but couldn't get size", true);
                    }
                } else {
                    DWORD error = GetLastError();
                    logMessage("Windows API also failed to access file: " + to_string(error), true);
                }
                
                // Check if file might exist with different case/extension
                try {
                    for (const auto& entry : filesystem::directory_iterator(outputDir)) {
                        logMessage("Found file in directory: " + entry.path().string());
                    }
                } catch (const exception& e) {
                    logMessage("Error listing directory: " + string(e.what()), true);
                }
            }
        } else {
            logMessage("Failed to reassemble output file: " + resolvedOutputPath, true);
            logMessage("Check if you have write permissions to this location.", true);
            
            // Try to diagnose why reassembly failed
            if (filesystem::exists(outputDir)) {
                logMessage("Output directory exists");
                
                try {
                    auto perms = filesystem::status(outputDir).permissions();
                    bool hasWritePermission = ((perms & filesystem::perms::owner_write) != filesystem::perms::none);
                    logMessage("Directory write permission: " + string(hasWritePermission ? "Yes" : "No"));
                } catch (const exception& e) {
                    logMessage("Error checking permissions: " + string(e.what()), true);
                }
                
                // Try Windows API to check write permissions
                std::wstring wideOutputDir = StringToWString(outputDir);
                DWORD attributes = GetFileAttributesW(wideOutputDir.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES) {
                    bool isReadOnly = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
                    logMessage("Windows says directory is read-only: " + string(isReadOnly ? "Yes" : "No"));
                    
                    // Try to create a test file directly in the directory
                    std::string testPath = outputDir + "\\test_write_perm.tmp";
                    std::wstring wideTestPath = StringToWString(testPath);
                    HANDLE hTestFile = CreateFileW(
                        wideTestPath.c_str(),
                        GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                    );
                    
                    if (hTestFile != INVALID_HANDLE_VALUE) {
                        logMessage("Successfully created test file in directory, write permissions confirmed");
                        CloseHandle(hTestFile);
                        DeleteFileW(wideTestPath.c_str());
                    } else {
                        DWORD error = GetLastError();
                        logMessage("Failed to create test file in directory: " + to_string(error), true);
                    }
                }
            } else {
                logMessage("Output directory does not exist!", true);
                
                // Try to create the directory with Windows API
                std::wstring wideOutputDir = StringToWString(outputDir);
                if (CreateDirectoryW(wideOutputDir.c_str(), NULL)) {
                    logMessage("Successfully created output directory with Windows API");
                } else {
                    DWORD error = GetLastError();
                    logMessage("Failed to create output directory with Windows API: " + to_string(error), true);
                }
            }
        }

        // After successful file processing, upload to Dropbox if requested
        if (uploadToDropbox) {
            logMessage("Uploading processed file to Dropbox...");
            
            // Try to load Dropbox config
            if (!Config::loadConfig()) {
                logMessage("Error: Dropbox not configured. Run 'dropbox-config' first.", true);
                return;
            }
            
            // Initialize Dropbox client
            DropboxClient client(Config::getDropboxAccessToken());
            if (!client.initialize()) {
                logMessage("Failed to initialize Dropbox client", true);
                return;
            }
            
            // Extract filename from output path
            string filename = outputFile;
            size_t lastSlash = filename.find_last_of("/\\");
            if (lastSlash != string::npos) {
                filename = filename.substr(lastSlash + 1);
            }
            
            // Combine with configured folder
            string dropboxPath = Config::getDropboxFolder();
            if (!dropboxPath.empty() && dropboxPath.back() != '/') {
                dropboxPath += '/';
            }
            dropboxPath += filename;
            
            // Upload file
            if (client.uploadFile(outputFile, dropboxPath)) {
                logMessage("File successfully uploaded to Dropbox: " + dropboxPath);
            } else {
                logMessage("Failed to upload file to Dropbox", true);
            }
        }
    } catch (const exception& e) {
        logMessage("Unhandled error: " + string(e.what()), true);
    }
}

int main(int argc, char* argv[]) {
    // Initialize debug log file
    debugLogFile.open("encryption_process.log", ios::app);
    if (!debugLogFile.is_open()) {
        cerr << "Warning: Unable to open debug log file" << endl;
    }
    
    logMessage("Starting distributed encryption application");
    
    if (argc < 2) {
        printHelp();
        return 1;
    }

    string mode(argv[1]);
    bool useTLS = false;
    
    // Check for --tls flag
    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "--tls") {
            useTLS = true;
            logMessage("TLS security enabled");
            break;
        }
    }

    // Check for Dropbox operations
    if (mode == "dropbox-config" || mode == "dropbox-upload" || 
        mode == "dropbox-download" || mode == "dropbox-list") {
        if (handleDropboxOperation(mode, argc, argv)) {
            return 0;
        } else {
            return 1;
        }
    }

    try {
        if (mode == "worker" && argc >= 3) {
            string address(argv[2]);
            // Add default port if not specified
            if (address.find(':') == string::npos) {
                address += ":" + DEFAULT_WORKER_PORT;
                logMessage("No port specified, using default: " + DEFAULT_WORKER_PORT);
            }
            runWorker(address, useTLS);
        }
        else if ((mode == "master" || mode == "encrypt" || mode == "decrypt") && argc >= 5) {
            string inputFile(argv[2]);
            string outputFile(argv[3]);
            vector<string> workerAddresses;
            
            for (int i = 4; i < argc; ++i) {
                if (string(argv[i]) == "--tls") continue;
                string address(argv[i]);
                // Add default port if not specified
                if (address.find(':') == string::npos) {
                    address += ":" + DEFAULT_WORKER_PORT;
                    logMessage("No port specified for worker, using default: " + address);
                }
                workerAddresses.push_back(address);
            }
            
            if (workerAddresses.empty()) {
                logMessage("Error: No worker addresses provided", true);
                return 1;
            }
            
            logMessage("Mode: " + mode + ", Input: " + inputFile + ", Output: " + outputFile);
            logMessage("Worker count: " + to_string(workerAddresses.size()));
            
            bool encryptMode = (mode == "master" || mode == "encrypt");
            bool uploadToDropbox = false;
            
            // Check for --dropbox flag to upload after processing
            for (int i = 0; i < argc; i++) {
                string arg(argv[i]);
                if (arg == "--dropbox") {
                    uploadToDropbox = true;
                    break;
                }
            }
            
            processFile(workerAddresses, inputFile, outputFile, encryptMode, useTLS, uploadToDropbox);
        }
        else {
            printHelp();
            return 1;
        }
    } catch (const exception& e) {
        logMessage("Error: " + string(e.what()), true);
        return 1;
    }

    logMessage("Application finished");
    debugLogFile.close();
    return 0;
}
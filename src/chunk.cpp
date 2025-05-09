#include "chunk.h"
#include "utilities.h"
#include <fstream>
#include <iostream>
#include <system_error>
#include <direct.h>  // For _mkdir on Windows
#include <sstream>   // For stringstream
#include <string.h>  // For strerror
#include <filesystem> // For std::filesystem
#include <windows.h> // For Windows API file operations

std::vector<FileChunk> FileChunker::chunkFile(const std::string& filePath, size_t chunkSize) {
    std::cout << "Opening file for chunking: " << filePath << std::endl;
    
    // Get absolute path for better error reporting
    char absolutePath[MAX_PATH];
    if (_fullpath(absolutePath, filePath.c_str(), MAX_PATH) == nullptr) {
        std::cerr << "Error resolving full path for: " << filePath << std::endl;
        return {};
    }
    
    std::string resolvedPath(absolutePath);
    std::cout << "Resolved absolute path: " << resolvedPath << std::endl;
    
    std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::error_code ec(errno, std::system_category());
        std::cerr << "Failed to open file: " << resolvedPath 
                  << ", error: " << ec.value() << " - " << ec.message() << std::endl;
        throw std::runtime_error("Failed to open file: " + resolvedPath);
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::cout << "File size: " << fileSize << " bytes" << std::endl;

    std::vector<FileChunk> chunks;
    int chunkId = 0;
    
    // Use a smaller chunk size to avoid boundary issues with AES blocks
    // AES operates on 16-byte blocks, so we want to avoid issues at block boundaries
    size_t safeChunkSize = chunkSize;
    if (safeChunkSize % 16 == 0) {
        safeChunkSize -= 16; // Ensure we're not exactly at a block boundary
    }
    
    std::cout << "Using chunk size: " << safeChunkSize << " bytes" << std::endl;
    std::vector<char> buffer(safeChunkSize);

    while (file) {
        file.read(buffer.data(), buffer.size());
        std::streamsize bytesRead = file.gcount();
        
        if (bytesRead > 0) {
            FileChunk chunk;
            chunk.data.assign(buffer.begin(), buffer.begin() + bytesRead);
            chunk.id = chunkId++;
            chunks.push_back(chunk);
            std::cout << "Created chunk " << chunk.id << " with " << bytesRead << " bytes" << std::endl;
        }
    }

    std::cout << "Created " << chunks.size() << " chunks from file" << std::endl;
    return chunks;
}

// Windows-specific directory creation function
bool createDirectoryRecursive(const std::string& dirPath) {
    std::cout << "Creating directory: " << dirPath << std::endl;
    
    // Split the path into components
    std::stringstream ss(dirPath);
    std::string item;
    std::string path;
    
    char separator = '\\';
    if (dirPath.find('/') != std::string::npos) {
        separator = '/';  // Use forward slash if found in path
    }
    
    while (std::getline(ss, item, separator)) {
        if (item.empty()) continue;
        
        path += item + separator;
        
        // Skip drive letter (e.g., C:)
        if (item.length() == 2 && item[1] == ':') continue;
        
        // Try to create directory
        if (_mkdir(path.c_str()) != 0 && errno != EEXIST) {
            std::error_code ec(errno, std::system_category());
            std::cerr << "Failed to create directory: " << path 
                      << ", error: " << ec.value() << " - " << ec.message() << std::endl;
            return false;
        }
    }
    
    return true;
}

// Helper function to write file using Windows API
bool writeFileWithWindowsAPI(const std::string& filePath, const std::vector<FileChunk>& chunks) {
    std::cout << "Attempting to write file using Windows API: " << filePath << std::endl;
    std::cout << "File path length: " << filePath.length() << " characters" << std::endl;
    
    // Check if path contains long path prefix
    bool hasLongPathPrefix = (filePath.substr(0, 4) == "\\\\?\\");
    std::string actualPath = filePath;
    
    // Convert path to wide string for Windows API
    std::wstring wideFilePath = StringToWString(filePath);
    std::cout << "Wide path length: " << wideFilePath.length() << " characters" << std::endl;
    
    // Try to add long path prefix if the path is long
    if (!hasLongPathPrefix && wideFilePath.length() > MAX_PATH - 12) { // Leave room for filename
        std::cout << "Path is long, adding \\\\?\\ prefix" << std::endl;
        if (filePath.substr(0, 2) == "\\\\") {
            // UNC path - use \\?\UNC\server\share format
            actualPath = "\\\\?\\UNC\\" + filePath.substr(2);
        } else {
            // Local path
            actualPath = "\\\\?\\" + filePath;
        }
        wideFilePath = StringToWString(actualPath);
        std::cout << "Modified path with prefix: " << actualPath << std::endl;
    }
    
    // Show detailed file path information
    std::cout << "Absolute Path: " << filePath << std::endl;
    std::string directory = filePath.substr(0, filePath.find_last_of("/\\"));
    std::cout << "Directory: " << directory << std::endl;
    std::string filename = filePath.substr(filePath.find_last_of("/\\") + 1);
    std::cout << "Filename: " << filename << std::endl;
    
    // Verify parent directory exists
    std::wstring wideDirectory = StringToWString(directory);
    DWORD dirAttrs = GetFileAttributesW(wideDirectory.c_str());
    if (dirAttrs == INVALID_FILE_ATTRIBUTES) {
        std::cout << "ERROR: Parent directory does not exist or cannot be accessed: " << directory << std::endl;
        std::cout << "GetFileAttributesW error: " << GetLastError() << std::endl;
        
        // Try to create the directory
        if (!CreateDirectoryW(wideDirectory.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            std::cout << "ERROR: Failed to create parent directory: " << GetLastError() << std::endl;
            return false;
        } else {
            std::cout << "Created parent directory" << std::endl;
        }
    } else {
        std::cout << "Parent directory exists" << std::endl;
    }
    
    // Create or open file with explicit security attributes and flags
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, FALSE };
    
    std::cout << "Creating file with CreateFileW..." << std::endl;
    HANDLE hFile = CreateFileW(
        wideFilePath.c_str(),
        GENERIC_WRITE,
        0,                      // No sharing
        &sa,                    // Explicit security attributes
        CREATE_ALWAYS,          // Always create
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,  // Normal file with write-through
        NULL                    // No template
    );
    
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        std::cerr << "Windows API CreateFile failed: " << filePath 
                  << ", Error code: " << error << std::endl;
        
        // Print error description
        LPVOID errorMsg;
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            error,
            0,
            (LPWSTR)&errorMsg,
            0,
            NULL
        );
        
        if (errorMsg) {
            std::wcerr << L"Error message: " << (LPWSTR)errorMsg << std::endl;
            LocalFree(errorMsg);
        }
        
        // Attempt an alternate approach
        std::cout << "Attempting creation with alternate file API..." << std::endl;
        std::ofstream testFile(filePath.c_str(), std::ios::binary | std::ios::out);
        if (testFile.is_open()) {
            std::cout << "Successfully opened file with std::ofstream" << std::endl;
            testFile.close();
            
            // Now try again with Windows API
            hFile = CreateFileW(
                wideFilePath.c_str(),
                GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,  // Open existing since we just created it
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            
            if (hFile == INVALID_HANDLE_VALUE) {
                error = GetLastError();
                std::cerr << "Windows API CreateFile still failed on second attempt: " 
                          << ", Error code: " << error << std::endl;
                return false;
            }
            
            std::cout << "Successfully opened file on second attempt" << std::endl;
        } else {
            std::cerr << "Both Windows API and std::ofstream failed to create file" << std::endl;
            return false;
        }
    }
    
    size_t totalBytesWritten = 0;
    bool success = true;
    
    std::cout << "File handle obtained successfully, beginning to write " << chunks.size() << " chunks" << std::endl;
    
    // Verify file handle is valid again
    DWORD fileType = GetFileType(hFile);
    if (fileType == FILE_TYPE_UNKNOWN) {
        std::cerr << "WARNING: File handle has unknown type: " << GetLastError() << std::endl;
    } else {
        std::cout << "File handle verified as valid, type: " << fileType << std::endl;
    }
    
    for (const auto& chunk : chunks) {
        if (chunk.data.empty()) {
            std::cerr << "Warning: Chunk " << chunk.id << " is empty, skipping" << std::endl;
            continue;
        }
        
        std::cout << "Writing chunk " << chunk.id << " (" << chunk.data.size() << " bytes)" << std::endl;
        
        DWORD bytesWritten = 0;
        if (!WriteFile(
            hFile,
            chunk.data.data(),
            static_cast<DWORD>(chunk.data.size()),
            &bytesWritten,
            NULL
        )) {
            DWORD error = GetLastError();
            std::cerr << "Windows API WriteFile failed for chunk " << chunk.id 
                      << ", Error code: " << error << std::endl;
            
            // Print error description
            LPVOID errorMsg;
            FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                NULL,
                error,
                0,
                (LPWSTR)&errorMsg,
                0,
                NULL
            );
            
            if (errorMsg) {
                std::wcerr << L"Error message: " << (LPWSTR)errorMsg << std::endl;
                LocalFree(errorMsg);
            }
            
            success = false;
            break;
        }
        
        if (bytesWritten != chunk.data.size()) {
            std::cerr << "Windows API WriteFile: Partial write for chunk " << chunk.id 
                      << ", wrote " << bytesWritten << " of " << chunk.data.size() << " bytes" << std::endl;
            success = false;
            break;
        }
        
        totalBytesWritten += bytesWritten;
        std::cout << "Successfully wrote chunk " << chunk.id << " (" << bytesWritten << " bytes)" << std::endl;
    }
    
    if (success) {
        std::cout << "All chunks written successfully, total bytes: " << totalBytesWritten << std::endl;
    } else {
        std::cerr << "Failed to write all chunks, stopping at " << totalBytesWritten << " bytes" << std::endl;
    }
    
    std::cout << "Flushing file buffers..." << std::endl;
    // Flush file buffers to ensure data is written to disk
    if (!FlushFileBuffers(hFile)) {
        DWORD error = GetLastError();
        std::cerr << "Windows API FlushFileBuffers failed: Error code: " << error << std::endl;
        
        // Print error description
        LPVOID errorMsg;
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            error,
            0,
            (LPWSTR)&errorMsg,
            0,
            NULL
        );
        
        if (errorMsg) {
            std::wcerr << L"Error message: " << (LPWSTR)errorMsg << std::endl;
            LocalFree(errorMsg);
        }
        
        success = false;
    } else {
        std::cout << "File buffers flushed successfully" << std::endl;
    }
    
    std::cout << "Closing file handle..." << std::endl;
    if (!CloseHandle(hFile)) {
        DWORD error = GetLastError();
        std::cerr << "Windows API CloseHandle failed: Error code: " << error << std::endl;
        
        // Print error description
        LPVOID errorMsg;
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            error,
            0,
            (LPWSTR)&errorMsg,
            0,
            NULL
        );
        
        if (errorMsg) {
            std::wcerr << L"Error message: " << (LPWSTR)errorMsg << std::endl;
            LocalFree(errorMsg);
        }
        
        success = false;
    } else {
        std::cout << "File handle closed successfully" << std::endl;
    }
    
    // Verify the file exists and is the correct size after closing
    if (success) {
        std::cout << "Verifying file exists after writing..." << std::endl;
        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        if (GetFileAttributesExW(wideFilePath.c_str(), GetFileExInfoStandard, &fileInfo)) {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = fileInfo.nFileSizeLow;
            fileSize.HighPart = fileInfo.nFileSizeHigh;
            std::cout << "File verified: " << filePath << ", size: " << fileSize.QuadPart << " bytes" << std::endl;
            
            if (fileSize.QuadPart != totalBytesWritten) {
                std::cerr << "WARNING: File size mismatch. Written: " << totalBytesWritten 
                          << ", Actual: " << fileSize.QuadPart << std::endl;
            }
        } else {
            DWORD error = GetLastError();
            std::cerr << "ERROR: Failed to verify file after writing: " << error << std::endl;
            success = false;
        }
    }
    
    return success;
}

bool FileChunker::reassembleFile(const std::string& outputPath, const std::vector<FileChunk>& chunks) {
    std::cout << "\n============= STARTING FILE REASSEMBLY =============" << std::endl;
    std::cout << "Reassembling file: \"" << outputPath << "\" from " << chunks.size() << " chunks" << std::endl;
    
    // Log useful environment information
    std::cout << "Current directory: " << std::filesystem::current_path().string() << std::endl;
    char* tempDir = getenv("TEMP");
    std::cout << "TEMP directory: " << (tempDir ? tempDir : "Not set") << std::endl;
    
    // Test file creation in current directory
    std::string testFileName = "test_permission.tmp";
    std::ofstream testFile(testFileName, std::ios::binary);
    if (testFile.is_open()) {
        testFile.close();
        std::cout << "Successfully created test file in current directory" << std::endl;
        std::filesystem::remove(testFileName);
    } else {
        std::cerr << "WARNING: Could not create test file in current directory. Permission issues may exist." << std::endl;
    }
    
    // Check if we have chunks to write
    if (chunks.empty()) {
        std::cerr << "Error: No chunks to reassemble" << std::endl;
        return false;
    }
    
    // Count total bytes in chunks
    size_t totalBytes = 0;
    for (const auto& chunk : chunks) {
        totalBytes += chunk.data.size();
    }
    std::cout << "Total data size in chunks: " << totalBytes << " bytes" << std::endl;
    
    // Get absolute path for better error reporting
    char absolutePath[MAX_PATH];
    if (_fullpath(absolutePath, outputPath.c_str(), MAX_PATH) == nullptr) {
        std::cerr << "Error resolving full path for: " << outputPath << std::endl;
        return false;
    }
    
    std::string resolvedPath(absolutePath);
    std::cout << "Resolved absolute path: \"" << resolvedPath << "\"" << std::endl;
    
    // Print additional diagnostic info
    std::cout << "Output file diagnostic: Current directory=" << std::filesystem::current_path().string() << std::endl;
    std::cout << "Requested file path=\"" << outputPath << "\"" << std::endl;
    std::cout << "Resolved absolute path=\"" << resolvedPath << "\"" << std::endl;
    
    // Fix potential issues with path
    for (char& c : resolvedPath) {
        if (c == '/') c = '\\'; // Ensure Windows path format
    }
    std::cout << "Normalized path: \"" << resolvedPath << "\"" << std::endl;
    
    // Check if file already exists
    if (std::filesystem::exists(resolvedPath)) {
        std::cout << "Target file already exists, will be overwritten" << std::endl;
        try {
            std::filesystem::remove(resolvedPath);
            std::cout << "Removed existing file" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error removing existing file: " << e.what() << std::endl;
        }
    }
    
    // Convert to Windows wide string path for more reliable operations
    std::wstring wideResolvedPath = StringToWString(resolvedPath);
    
    // Extract directory path and create if needed
    size_t lastSeparator = resolvedPath.find_last_of("/\\");
    if (lastSeparator != std::string::npos) {
        std::string dirPath = resolvedPath.substr(0, lastSeparator);
        std::cout << "Ensuring directory exists: \"" << dirPath << "\"" << std::endl;
        
        // Check if directory exists using filesystem
        bool dirExists = false;
        try {
            dirExists = std::filesystem::exists(dirPath);
            std::cout << "Directory exists (filesystem check): " << (dirExists ? "Yes" : "No") << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error checking directory existence: " << e.what() << std::endl;
        }
        
        // Convert directory path to wide string for Windows API
        std::wstring wideDirPath = StringToWString(dirPath);
        
        // Try Windows API first for directory creation (more reliable with Unicode paths)
        if (!CreateDirectoryW(wideDirPath.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            // Try to create parent directories recursively with Windows API
            bool dirCreated = false;
            std::wstring currentPath;
            
            // Split the path and create each directory level
            for (size_t i = 0; i < wideDirPath.length(); i++) {
                if (wideDirPath[i] == L'\\' || wideDirPath[i] == L'/') {
                    // Skip root directory or drive letter
                    if (currentPath.length() <= 3) { // e.g., "C:\" is 3 chars
                        currentPath += wideDirPath[i];
                        continue;
                    }
                    
                    if (!CreateDirectoryW(currentPath.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                        std::cerr << "Failed to create directory: " << std::string(currentPath.begin(), currentPath.end()) << std::endl;
                    }
                }
                currentPath += wideDirPath[i];
            }
            
            // Create the final directory
            if (!CreateDirectoryW(wideDirPath.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                std::cerr << "Failed to create final directory: " << dirPath << std::endl;
                
                // Fall back to C++ filesystem if Windows API fails
                try {
                    if (!std::filesystem::exists(dirPath)) {
                        if (!std::filesystem::create_directories(dirPath)) {
                            std::cerr << "Both Windows API and std::filesystem failed to create directory" << std::endl;
                            return false;
                        }
                    }
                } catch (const std::filesystem::filesystem_error& e) {
                    std::cerr << "Filesystem error: " << e.what() << std::endl;
                    return false;
                }
            }
        }
    }
    
    // Try Windows API directly for file creation - most reliable approach
    std::cout << "Attempting direct Windows API file creation..." << std::endl;
    if (writeFileWithWindowsAPI(resolvedPath, chunks)) {
        std::cout << "Successfully wrote file using Windows API" << std::endl;
        
        // Final verification before returning
        bool finalCheck = false;
        std::cout << "Final verification of created file..." << std::endl;
        
        // Use Windows API for final verification
        DWORD attributes = GetFileAttributesW(wideResolvedPath.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            std::cout << "File verified by Windows API: " << resolvedPath << std::endl;
            finalCheck = true;
        } else {
            std::cout << "WARNING: Windows API verification failed. Error: " << GetLastError() << std::endl;
        }
        
        // Also try filesystem
        try {
            if (std::filesystem::exists(resolvedPath)) {
                std::cout << "File verified by filesystem: " << resolvedPath << std::endl;
                finalCheck = true;
            } else {
                std::cout << "WARNING: Filesystem verification failed" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception during filesystem check: " << e.what() << std::endl;
        }
        
        if (!finalCheck) {
            std::cerr << "CRITICAL: File could not be verified by any method after creation!" << std::endl;
        }
        
        return finalCheck;
    }
    
    // If direct approach failed, try with temporary file
    std::string tempFilePath = resolvedPath + ".tmp";
    std::wstring wideTempPath = StringToWString(tempFilePath);
    std::cout << "Creating temporary file with Windows API: " << tempFilePath << std::endl;
    
    // Create temporary file with Windows API
    HANDLE hTempFile = CreateFileW(
        wideTempPath.c_str(),
        GENERIC_WRITE,
        0,                      // No sharing
        NULL,                   // Default security
        CREATE_ALWAYS,          // Always create
        FILE_ATTRIBUTE_NORMAL,  // Normal file
        NULL                    // No template
    );
    
    if (hTempFile == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        std::cerr << "Windows API CreateFile failed for temp file: Error code: " << error << std::endl;
        
        // Final fallback: try std::ofstream
        std::ofstream outputFile(tempFilePath, std::ios::binary);
        if (!outputFile.is_open()) {
            std::error_code ec(errno, std::system_category());
            std::cerr << "Error opening temp output file: " << tempFilePath << ", error: " 
                      << ec.value() << " - " << ec.message() << std::endl;
            
            return false;
        }
        
        // Write chunks to file using std::ofstream
        size_t totalBytes = 0;
        for (const auto& chunk : chunks) {
            if (chunk.data.empty()) {
                std::cerr << "Warning: Chunk " << chunk.id << " is empty" << std::endl;
                continue;
            }
            
            outputFile.write(chunk.data.data(), chunk.data.size());
            if (outputFile.fail()) {
                std::error_code ec(errno, std::system_category());
                std::cerr << "Error writing to file: " << ec.value() << " - " << ec.message() << std::endl;
                outputFile.close();
                return false;
            }
            totalBytes += chunk.data.size();
        }
        
        outputFile.close();
        
        // Now copy the temp file to final location
        if (CopyFileW(wideTempPath.c_str(), wideResolvedPath.c_str(), FALSE)) {
            std::cout << "Successfully copied temp file to final location" << std::endl;
            // Delete the temp file
            DeleteFileW(wideTempPath.c_str());
            return true;
        } else {
            DWORD error = GetLastError();
            std::cerr << "Failed to copy temp file to final location. Error: " << error << std::endl;
            return false;
        }
    }
    
    // If we got here, we have a valid temp file handle, write chunks
    size_t totalBytesWritten = 0;
    bool success = true;
    
    for (const auto& chunk : chunks) {
        if (chunk.data.empty()) {
            std::cerr << "Warning: Chunk " << chunk.id << " is empty, skipping" << std::endl;
            continue;
        }
        
        DWORD bytesWritten = 0;
        if (!WriteFile(
            hTempFile,
            chunk.data.data(),
            static_cast<DWORD>(chunk.data.size()),
            &bytesWritten,
            NULL
        )) {
            DWORD error = GetLastError();
            std::cerr << "Windows API WriteFile failed for chunk " << chunk.id 
                      << ", Error code: " << error << std::endl;
            success = false;
            break;
        }
        
        if (bytesWritten != chunk.data.size()) {
            std::cerr << "Windows API WriteFile: Partial write for chunk " << chunk.id 
                      << ", wrote " << bytesWritten << " of " << chunk.data.size() << " bytes" << std::endl;
            success = false;
            break;
        }
        
        totalBytesWritten += bytesWritten;
    }
    
    // Flush and close the temp file
    FlushFileBuffers(hTempFile);
    CloseHandle(hTempFile);
    
    if (!success) {
        std::cerr << "Failed to write all chunks to temp file" << std::endl;
        DeleteFileW(wideTempPath.c_str());
        return false;
    }
    
    // Delete the target file if it exists
    DeleteFileW(wideResolvedPath.c_str());
    
    // Copy the temp file to the final destination
    if (CopyFileW(wideTempPath.c_str(), wideResolvedPath.c_str(), FALSE)) {
        std::cout << "Successfully copied temp file to final location" << std::endl;
        // Delete the temp file
        DeleteFileW(wideTempPath.c_str());
        
        // Final verification before returning
        bool finalCheck = false;
        std::cout << "Final verification of created file..." << std::endl;
        
        // Use Windows API for final verification
        DWORD attributes = GetFileAttributesW(wideResolvedPath.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            std::cout << "File verified by Windows API: " << resolvedPath << std::endl;
            finalCheck = true;
        } else {
            std::cout << "WARNING: Windows API verification failed. Error: " << GetLastError() << std::endl;
        }
        
        // Also try filesystem
        try {
            if (std::filesystem::exists(resolvedPath)) {
                std::cout << "File verified by filesystem: " << resolvedPath << std::endl;
                finalCheck = true;
            } else {
                std::cout << "WARNING: Filesystem verification failed" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception during filesystem check: " << e.what() << std::endl;
        }
        
        if (!finalCheck) {
            std::cerr << "CRITICAL: File could not be verified by any method after creation!" << std::endl;
        }
        
        return finalCheck;
    } else {
        DWORD error = GetLastError();
        std::cerr << "Failed to copy temp file to final location. Error: " << error << std::endl;
        return false;
    }
}
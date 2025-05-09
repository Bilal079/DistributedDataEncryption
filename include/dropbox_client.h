#pragma once

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class DropboxClient {
public:
    DropboxClient(const std::string& accessToken);
    ~DropboxClient();

    // Initialize the CURL library
    bool initialize();

    // Upload a file to Dropbox
    bool uploadFile(const std::string& localFilePath, const std::string& dropboxPath);

    // Download a file from Dropbox
    bool downloadFile(const std::string& dropboxPath, const std::string& localFilePath);

    // List files in a Dropbox folder
    bool listFiles(const std::string& dropboxPath);

private:
    // Helper functions
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    bool performCurlRequest(CURL* curl, const std::string& url, const std::string& data, std::string& response);
    
    std::string accessToken_;
    bool isInitialized_;
}; 
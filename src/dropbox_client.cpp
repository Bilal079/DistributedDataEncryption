#include "dropbox_client.h"
#include <sstream>
#include <cstring>

// Constructor
DropboxClient::DropboxClient(const std::string& accessToken) 
    : accessToken_(accessToken), isInitialized_(false) {
}

// Destructor
DropboxClient::~DropboxClient() {
    // Cleanup curl if initialized
    if (isInitialized_) {
        curl_global_cleanup();
    }
}

// Initialize CURL library
bool DropboxClient::initialize() {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        std::cerr << "Failed to initialize curl: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    isInitialized_ = true;
    return true;
}

// Callback function for CURL to write received data
size_t DropboxClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realSize = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), realSize);
    return realSize;
}

// Helper function to perform CURL requests
bool DropboxClient::performCurlRequest(CURL* curl, const std::string& url, const std::string& data, std::string& response) {
    if (!isInitialized_) {
        std::cerr << "CURL not initialized. Call initialize() first." << std::endl;
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set authorization header
    struct curl_slist* headers = NULL;
    std::string authHeader = "Authorization: Bearer " + accessToken_;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Set POST data if provided
    if (!data.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    }
    
    // Set write callback function
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Cleanup
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        std::cerr << "HTTP error: " << httpCode << std::endl;
        std::cerr << "Response: " << response << std::endl;
        return false;
    }
    
    return true;
}

// Upload a file to Dropbox
bool DropboxClient::uploadFile(const std::string& localFilePath, const std::string& dropboxPath) {
    if (!isInitialized_) {
        std::cerr << "CURL not initialized. Call initialize() first." << std::endl;
        return false;
    }
    
    std::cout << "Uploading file: " << localFilePath << " to Dropbox path: " << dropboxPath << std::endl;
    
    // Open the file
    std::ifstream file(localFilePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << localFilePath << std::endl;
        return false;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read file content
    std::vector<char> fileData(fileSize);
    if (!file.read(fileData.data(), fileSize)) {
        std::cerr << "Failed to read file: " << localFilePath << std::endl;
        return false;
    }
    
    // Create CURL handle
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL handle" << std::endl;
        return false;
    }
    
    // Prepare for upload
    std::string url = "https://content.dropboxapi.com/2/files/upload";
    
    // Set Dropbox API parameters
    json dropboxArg;
    dropboxArg["path"] = dropboxPath;
    dropboxArg["mode"] = "overwrite";
    dropboxArg["autorename"] = true;
    dropboxArg["mute"] = false;
    dropboxArg["strict_conflict"] = false;
    
    std::string dropboxArgStr = dropboxArg.dump();
    
    // Set headers
    struct curl_slist* headers = NULL;
    std::string authHeader = "Authorization: Bearer " + accessToken_;
    std::string contentTypeHeader = "Content-Type: application/octet-stream";
    std::string dropboxApiArgHeader = "Dropbox-API-Arg: " + dropboxArgStr;
    
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, contentTypeHeader.c_str());
    headers = curl_slist_append(headers, dropboxApiArgHeader.c_str());
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fileData.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, fileSize);
    
    // Response handling
    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    
    // Check HTTP response code
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        std::cerr << "HTTP error: " << httpCode << std::endl;
        std::cerr << "Response: " << response << std::endl;
        return false;
    }
    
    std::cout << "File uploaded successfully to Dropbox" << std::endl;
    return true;
}

// Download a file from Dropbox
bool DropboxClient::downloadFile(const std::string& dropboxPath, const std::string& localFilePath) {
    if (!isInitialized_) {
        std::cerr << "CURL not initialized. Call initialize() first." << std::endl;
        return false;
    }
    
    std::cout << "Downloading file from Dropbox path: " << dropboxPath << " to local path: " << localFilePath << std::endl;
    
    // Create CURL handle
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL handle" << std::endl;
        return false;
    }
    
    // Prepare for download
    std::string url = "https://content.dropboxapi.com/2/files/download";
    
    // Set Dropbox API parameters
    json dropboxArg;
    dropboxArg["path"] = dropboxPath;
    
    std::string dropboxArgStr = dropboxArg.dump();
    
    // Set headers
    struct curl_slist* headers = NULL;
    std::string authHeader = "Authorization: Bearer " + accessToken_;
    std::string dropboxApiArgHeader = "Dropbox-API-Arg: " + dropboxArgStr;
    
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, dropboxApiArgHeader.c_str());
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    
    // Open file for writing
    FILE* fp = fopen(localFilePath.c_str(), "wb");
    if (!fp) {
        std::cerr << "Failed to open file for writing: " << localFilePath << std::endl;
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        return false;
    }
    
    // Set write callback to write directly to file
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Close file
    fclose(fp);
    
    // Cleanup
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Check HTTP response code
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    
    if (httpCode < 200 || httpCode >= 300) {
        std::cerr << "HTTP error: " << httpCode << std::endl;
        return false;
    }
    
    std::cout << "File downloaded successfully from Dropbox" << std::endl;
    return true;
}

// List files in a Dropbox folder
bool DropboxClient::listFiles(const std::string& dropboxPath) {
    if (!isInitialized_) {
        std::cerr << "CURL not initialized. Call initialize() first." << std::endl;
        return false;
    }
    
    // Create CURL handle
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL handle" << std::endl;
        return false;
    }
    
    // Prepare the request
    std::string url = "https://api.dropboxapi.com/2/files/list_folder";
    
    // Set Dropbox API parameters
    json requestData;
    requestData["path"] = dropboxPath;
    requestData["recursive"] = false;
    requestData["include_media_info"] = false;
    requestData["include_deleted"] = false;
    requestData["include_has_explicit_shared_members"] = false;
    requestData["include_mounted_folders"] = true;
    
    std::string requestDataStr = requestData.dump();
    
    // Perform the request
    std::string response;
    bool success = performCurlRequest(curl, url, requestDataStr, response);
    
    // Cleanup
    curl_easy_cleanup(curl);
    
    if (!success) {
        return false;
    }
    
    // Parse and display the response
    try {
        json responseJson = json::parse(response);
        auto entries = responseJson["entries"];
        
        std::cout << "Files in Dropbox folder " << dropboxPath << ":" << std::endl;
        for (const auto& entry : entries) {
            std::string name = entry["name"];
            std::string path = entry["path_display"];
            std::string type = entry[".tag"];
            
            std::cout << "- " << name << " (" << type << "): " << path << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse response: " << e.what() << std::endl;
        return false;
    }
    
    return true;
} 
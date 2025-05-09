#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Config {
public:
    static bool loadConfig(const std::string& configFile = "dropbox_config.json") {
        try {
            std::ifstream file(configFile);
            if (!file.is_open()) {
                std::cerr << "Failed to open config file: " << configFile << std::endl;
                return false;
            }
            
            json config;
            file >> config;
            
            if (config.contains("dropbox_access_token")) {
                dropboxAccessToken_ = config["dropbox_access_token"];
            } else {
                std::cerr << "Config file missing dropbox_access_token" << std::endl;
                return false;
            }
            
            if (config.contains("dropbox_folder")) {
                dropboxFolder_ = config["dropbox_folder"];
            } else {
                // Default to root folder
                dropboxFolder_ = "";
            }
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error loading config: " << e.what() << std::endl;
            return false;
        }
    }
    
    static bool saveConfig(const std::string& accessToken, const std::string& folder = "", 
                          const std::string& configFile = "dropbox_config.json") {
        try {
            json config;
            config["dropbox_access_token"] = accessToken;
            config["dropbox_folder"] = folder;
            
            std::ofstream file(configFile);
            if (!file.is_open()) {
                std::cerr << "Failed to open config file for writing: " << configFile << std::endl;
                return false;
            }
            
            file << config.dump(4); // Pretty print with 4 spaces
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error saving config: " << e.what() << std::endl;
            return false;
        }
    }
    
    static std::string getDropboxAccessToken() {
        return dropboxAccessToken_;
    }
    
    static std::string getDropboxFolder() {
        return dropboxFolder_;
    }
    
private:
    static inline std::string dropboxAccessToken_;
    static inline std::string dropboxFolder_;
}; 
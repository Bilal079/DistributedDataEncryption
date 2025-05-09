// crypto.h
#ifndef CRYPTO_H
#define CRYPTO_H

#include <vector>
#include <string>

class AESCrypto {
public:
    static std::vector<unsigned char> encrypt(const std::vector<char>& data,
                                            const std::string& key,
                                            const std::string& iv);
    
    static std::vector<char> decrypt(const std::vector<unsigned char>& encryptedData,
                                   const std::string& key,
                                   const std::string& iv);
    
    static void generateKeyIV(std::string& key, std::string& iv);
    
    // Add this new method
    static void printHex(const std::string& data, const std::string& label);
};

#endif // CRYPTO_H
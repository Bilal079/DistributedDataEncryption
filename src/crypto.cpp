#include "crypto.h"
#include <openssl/rand.h>
#include <openssl/err.h>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>

// Helper function to get OpenSSL error details
std::string getOpenSSLErrors() {
    std::ostringstream oss;
    unsigned long err;
    
    while ((err = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        oss << buf << "; ";
    }
    
    return oss.str();
}

std::vector<unsigned char> AESCrypto::encrypt(const std::vector<char>& data, 
                                             const std::string& key, 
                                             const std::string& iv) {
    // Check key and IV size
    if (key.size() != 32) {
        throw std::runtime_error("Invalid key size: Expected 32 bytes");
    }
    
    if (iv.size() != 16) {
        throw std::runtime_error("Invalid IV size: Expected 16 bytes");
    }

    // Create and initialize the context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context: " + getOpenSSLErrors());
    }

    // Initialize the encryption operation
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, 
                          reinterpret_cast<const unsigned char*>(key.data()), 
                          reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize encryption: " + getOpenSSLErrors());
    }

    // Provide the message to be encrypted, and obtain the encrypted output
    std::vector<unsigned char> ciphertext(data.size() + EVP_MAX_BLOCK_LENGTH);
    int len;
    int ciphertext_len;

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, 
                         reinterpret_cast<const unsigned char*>(data.data()), 
                         data.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption update failed: " + getOpenSSLErrors());
    }
    ciphertext_len = len;

    // Finalize the encryption - add padding if necessary
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Final encryption failed: " + getOpenSSLErrors());
    }
    ciphertext_len += len;

    // Clean up and return result
    ciphertext.resize(ciphertext_len);
    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
}

std::vector<char> AESCrypto::decrypt(const std::vector<unsigned char>& encryptedData, 
                                    const std::string& key, 
                                    const std::string& iv) {
    // Check key and IV size
    if (key.size() != 32) {
        throw std::runtime_error("Invalid key size: Expected 32 bytes");
    }
    
    if (iv.size() != 16) {
        throw std::runtime_error("Invalid IV size: Expected 16 bytes");
    }
    
    // Ensure the encrypted data has at least one block
    if (encryptedData.size() < 16 || encryptedData.size() % 16 != 0) {
        throw std::runtime_error("Invalid encrypted data size: Must be multiple of 16 bytes");
    }

    // Create and initialize the context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context: " + getOpenSSLErrors());
    }

    // Initialize the decryption operation
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, 
                          reinterpret_cast<const unsigned char*>(key.data()), 
                          reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize decryption: " + getOpenSSLErrors());
    }

    // Provide the ciphertext to be decrypted, and obtain the plaintext output
    std::vector<char> plaintext(encryptedData.size());
    int len;
    int plaintext_len;

    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &len, 
                         encryptedData.data(), encryptedData.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption update failed: " + getOpenSSLErrors());
    }
    plaintext_len = len;

    // Finalize the decryption - handle padding
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Final decryption failed: " + getOpenSSLErrors());
    }
    plaintext_len += len;

    // Clean up and return result
    plaintext.resize(plaintext_len);
    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

void AESCrypto::generateKeyIV(std::string& key, std::string& iv) {
    key.resize(32); // 256 bits
    iv.resize(16);  // 128 bits

    if (RAND_bytes(reinterpret_cast<unsigned char*>(&key[0]), key.size()) != 1 ||
        RAND_bytes(reinterpret_cast<unsigned char*>(&iv[0]), iv.size()) != 1) {
        throw std::runtime_error("Failed to generate random key/IV: " + getOpenSSLErrors());
    }
}

void AESCrypto::printHex(const std::string& data, const std::string& label) {
    std::cout << label;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : data) {
        oss << std::setw(2) << static_cast<int>(c) << ' ';
    }
    std::cout << oss.str() << std::endl;
}
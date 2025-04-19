#include "crypto.h"
#include <openssl/rand.h>
#include <openssl/err.h>
#include <stdexcept>
#include <iostream>
using namespace std;

vector<unsigned char> AESCrypto::encrypt(const vector<char>& data, 
                                             const string& key, 
                                             const string& iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw runtime_error("Failed to create cipher context");
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, 
                          reinterpret_cast<const unsigned char*>(key.data()), 
                          reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("Failed to initialize encryption");
    }

    vector<unsigned char> ciphertext(data.size() + EVP_MAX_BLOCK_LENGTH);
    int len;
    int ciphertext_len;

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, 
                         reinterpret_cast<const unsigned char*>(data.data()), 
                         data.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("Encryption failed");
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("Final encryption failed");
    }
    ciphertext_len += len;

    ciphertext.resize(ciphertext_len);
    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
}

vector<char> AESCrypto::decrypt(const vector<unsigned char>& encryptedData, 
                                    const string& key, 
                                    const string& iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw runtime_error("Failed to create cipher context");
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, 
                          reinterpret_cast<const unsigned char*>(key.data()), 
                          reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("Failed to initialize decryption");
    }

    vector<char> plaintext(encryptedData.size() + EVP_MAX_BLOCK_LENGTH);
    int len;
    int plaintext_len;

    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &len, 
                         encryptedData.data(), encryptedData.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("Decryption failed");
    }
    plaintext_len = len;

    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("Final decryption failed");
    }
    plaintext_len += len;

    plaintext.resize(plaintext_len);
    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

void AESCrypto::generateKeyIV(string& key, string& iv) {
    key.resize(32); // 256 bits
    iv.resize(16);  // 128 bits

    if (RAND_bytes(reinterpret_cast<unsigned char*>(&key[0]), key.size()) != 1 ||
        RAND_bytes(reinterpret_cast<unsigned char*>(&iv[0]), iv.size()) != 1) {
        throw runtime_error("Failed to generate random key/IV");
    }
}
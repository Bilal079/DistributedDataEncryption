#ifndef CRYPTO_H
#define CRYPTO_H

#include <string>
#include <vector>
#include <openssl/evp.h>
using namespace std;

class AESCrypto {
public:
    static vector<unsigned char> encrypt(const vector<char>& data, 
                                             const string& key, 
                                             const string& iv);
    static vector<char> decrypt(const vector<unsigned char>& encryptedData, 
                                    const string& key, 
                                    const string& iv);
    static void generateKeyIV(string& key, string& iv);
};

#endif // CRYPTO_H
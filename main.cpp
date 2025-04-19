#include "chunk.h"
#include "crypto.h"
#include <iostream>
#include <fstream>
using namespace std;

int main() {
    string inputFilePath = "input.txt"; //Input file to be encrypted
    string encryptedFilePath = "encrypted_output.txt"; //Encrypted output
    size_t chunkSize = 1024 * 1024; //1 MB chunks

    try {
        //Generate AES key and IV
        string key, iv;
        AESCrypto::generateKeyIV(key, iv);

        //Chunk the file
        vector<FileChunk> chunks = FileChunker::chunkFile(inputFilePath, chunkSize);

        //Encrypt each chunk
        vector<FileChunk> encryptedChunks;
        for (const auto& chunk : chunks) {
            auto encryptedData = AESCrypto::encrypt(chunk.data, key, iv);

            FileChunk encryptedChunk;
            encryptedChunk.id = chunk.id;
            encryptedChunk.data.assign(encryptedData.begin(), encryptedData.end());
            encryptedChunks.push_back(encryptedChunk);
        }

        //Reassemble encrypted chunks
        if (FileChunker::reassembleFile(encryptedFilePath, encryptedChunks)) {
            cout << "Encryption complete. Output written to " << encryptedFilePath << endl;
        } else {
            cerr << "Failed to write encrypted file.\n";
        }

    } catch (const exception& ex) {
        cerr << "Error: " << ex.what() << endl;
    }

    return 0;
}

#include <iostream>
#include <string>
#include <vector>
#include "master.h"
#include "worker.h"
#include "crypto.h"
using namespace std;

void runWorker(const string& address) {
    EncryptionWorker worker;
    worker.runServer(address);
}

void runMaster(const vector<string>& workerAddresses, 
              const string& inputFile, 
              const string& outputFile) {
    EncryptionMaster master(workerAddresses);
    
    //Generate key and IV
    string key, iv;
    AESCrypto::generateKeyIV(key, iv);
    
    cout << "Using key: " << key << endl;
    cout << "Using IV: " << iv << endl;
    
    //Encrypt the file
    const size_t chunkSize = 1024 * 1024; //1MB chunks
    auto encryptedChunks = master.encryptFile(inputFile, chunkSize, key, iv);
    
    //Reassemble the file
    if (FileChunker::reassembleFile(outputFile, encryptedChunks)) {
        cout << "File encrypted successfully: " << outputFile << endl;
    } else {
        cerr << "Failed to reassemble encrypted file" << endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <mode> [options]" << endl;
        cerr << "Modes:" << endl;
        cerr << "  worker <address> - Run as worker node" << endl;
        cerr << "  master <input> <output> <worker1> [worker2...] - Run as master node" << endl;
        return 1;
    }
    
    string mode(argv[1]);
    
    if (mode == "worker" && argc >= 3) {
        runWorker(argv[2]);
    } else if (mode == "master" && argc >= 5) {
        string inputFile(argv[2]);
        string outputFile(argv[3]);
        vector<string> workerAddresses;
        
        for (int i = 4; i < argc; ++i) {
            workerAddresses.push_back(argv[i]);
        }
        
        runMaster(workerAddresses, inputFile, outputFile);
    } else {
        cerr << "Invalid arguments" << endl;
        return 1;
    }
    
    return 0;
}
#include "chunk.h"
#include <fstream>
#include <iostream>
using namespace std;

vector<FileChunk> FileChunker::chunkFile(const string& filePath, size_t chunkSize) {
    ifstream file(filePath, ios::binary | ios::ate);
    if (!file.is_open()) {
        throw runtime_error("Failed to open file: " + filePath);
    }

    streamsize fileSize = file.tellg();
    file.seekg(0, ios::beg);

    vector<FileChunk> chunks;
    int chunkId = 0;
    vector<char> buffer(chunkSize);

    while (file) {
        file.read(buffer.data(), chunkSize);
        streamsize bytesRead = file.gcount();
        
        if (bytesRead > 0) {
            FileChunk chunk;
            chunk.data.assign(buffer.begin(), buffer.begin() + bytesRead);
            chunk.id = chunkId++;
            chunks.push_back(chunk);
        }
    }

    return chunks;
}

bool FileChunker::reassembleFile(const string& outputPath, const vector<FileChunk>& chunks) {
    ofstream outputFile(outputPath, ios::binary);
    if (!outputFile.is_open()) {
        return false;
    }

    for (const auto& chunk : chunks) {
        outputFile.write(chunk.data.data(), chunk.data.size());
    }

    return true;
}
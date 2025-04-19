#ifndef CHUNK_H
#define CHUNK_H

#include <vector>
#include <string>
using namespace std;

struct FileChunk {
    vector<char> data;
    int id;
};

class FileChunker {
public:
    static vector<FileChunk> chunkFile(const string& filePath, size_t chunkSize);
    static bool reassembleFile(const string& outputPath, const vector<FileChunk>& chunks);
};

#endif // CHUNK_H

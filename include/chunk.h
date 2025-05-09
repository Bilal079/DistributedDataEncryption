#ifndef CHUNK_H
#define CHUNK_H

#include <vector>
#include <string>

struct FileChunk {
    std::vector<char> data;
    int id;
};

class FileChunker {
public:
    static std::vector<FileChunk> chunkFile(const std::string& filePath, size_t chunkSize);
    static bool reassembleFile(const std::string& outputPath, const std::vector<FileChunk>& chunks);
};

#endif // CHUNK_H
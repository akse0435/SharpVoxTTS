#ifndef SHARPVOX_WAV_WRITER_H
#define SHARPVOX_WAV_WRITER_H

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace SharpVox {

class WavStreamWriter {
public:
    WavStreamWriter(const std::string& path, int32_t sampleRate);
    // Streaming variant for non-seekable outputs (pipes): writes 0xFFFFFFFF
    // chunk sizes (unknown length) and never patches the header. Does not
    // close fp on Dispose.
    WavStreamWriter(FILE* fp, int32_t sampleRate);
    ~WavStreamWriter();

    // Non-copyable
    WavStreamWriter(const WavStreamWriter&) = delete;
    WavStreamWriter& operator=(const WavStreamWriter&) = delete;

    void Write(const int16_t* samples, int32_t count);
    void Write(const std::vector<int16_t>& samples);

    // Finalize header (called automatically by destructor)
    void Dispose();

private:
    FILE*   _fp;
    int32_t _sampleRate;
    int32_t _dataBytes;
    bool    _disposed;
    bool    _streaming;
    bool    _ownsFile;

    void WriteHeader();
    void WriteInt16(int16_t value);
    void WriteInt32(int32_t value);
    void WriteBytes(const char* data, int32_t count);
};

class WavWriter {
public:
    static void WriteWav(const std::string& path, const std::vector<int16_t>& samples, int32_t sampleRate);
};

}  // namespace SharpVox

#endif  // SHARPVOX_WAV_WRITER_H

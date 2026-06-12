#include "WavWriter.h"

#include <stdexcept>

namespace SharpVox {

WavStreamWriter::WavStreamWriter(const std::string& path, int32_t sampleRate)
    : _fp(nullptr), _sampleRate(sampleRate), _dataBytes(0), _disposed(false),
      _streaming(false), _ownsFile(true) {
    _fp = fopen(path.c_str(), "wb");
    if (!_fp) {
        throw std::runtime_error("Failed to open output file: " + path);
    }
    WriteHeader();
}

WavStreamWriter::WavStreamWriter(FILE* fp, int32_t sampleRate)
    : _fp(fp), _sampleRate(sampleRate), _dataBytes(0), _disposed(false),
      _streaming(true), _ownsFile(false) {
    if (!_fp) {
        throw std::runtime_error("Null output stream");
    }
    WriteHeader();
}

void WavStreamWriter::WriteHeader() {
    // Streaming mode uses 0xFFFFFFFF (unknown length); players read until EOF.
    int32_t unknown = -1;

    // RIFF header
    WriteBytes("RIFF", 4);
    WriteInt32(_streaming ? unknown : 0); // file size - 8 (patched on Dispose)
    WriteBytes("WAVE", 4);

    // fmt chunk
    WriteBytes("fmt ", 4);
    WriteInt32(16); // chunk size
    WriteInt16(1);  // PCM
    WriteInt16(1);  // mono
    WriteInt32(_sampleRate);
    WriteInt32(_sampleRate * 2); // byte rate
    WriteInt16(2);  // block align
    WriteInt16(16); // bits per sample

    // data chunk
    WriteBytes("data", 4);
    WriteInt32(_streaming ? unknown : 0); // data size (patched on Dispose)
}

WavStreamWriter::~WavStreamWriter() {
    Dispose();
}

void WavStreamWriter::Write(const int16_t* samples, int32_t count) {
    if (count > 0) {
        fwrite(samples, sizeof(int16_t), (size_t)count, _fp);
        _dataBytes += count * 2;
        // Keep pipe consumers fed instead of buffering whole utterances
        if (_streaming) fflush(_fp);
    }
}

void WavStreamWriter::Write(const std::vector<int16_t>& samples) {
    Write(samples.data(), static_cast<int32_t>(samples.size()));
}

void WavStreamWriter::Dispose() {
    if (_disposed) return;
    _disposed = true;
    if (_fp) {
        if (_streaming) {
            fflush(_fp);
        } else {
            fseek(_fp, 4, SEEK_SET);
            WriteInt32(36 + _dataBytes);
            fseek(_fp, 40, SEEK_SET);
            WriteInt32(_dataBytes);
        }
        if (_ownsFile) fclose(_fp);
        _fp = nullptr;
    }
}

void WavStreamWriter::WriteInt16(int16_t value) {
    fwrite(&value, sizeof(int16_t), 1, _fp);
}

void WavStreamWriter::WriteInt32(int32_t value) {
    fwrite(&value, sizeof(int32_t), 1, _fp);
}

void WavStreamWriter::WriteBytes(const char* data, int32_t count) {
    fwrite(data, 1, (size_t)count, _fp);
}

void WavWriter::WriteWav(const std::string& path, const std::vector<int16_t>& samples, int32_t sampleRate) {
    WavStreamWriter writer(path, sampleRate);
    writer.Write(samples);
}

}  // namespace SharpVox

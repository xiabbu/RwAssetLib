/**
 * core/RwStream.cpp - RenderWare binary stream implementation.
 */

#include "core/RwStream.h"

#include <cstdint>
#include <cstring>

RwStream::RwStream() = default;

RwStream::~RwStream()
{
    Close();
}

// ============================================================================
// Opening / closing
// ============================================================================

bool RwStream::OpenRead(const char* filename)
{
    if (!Close()) {
        return false;
    }
    m_lastError.clear();

    m_file = std::fopen(filename, "rb");
    if (!m_file) {
        SetError("Failed to open file for reading");
        return false;
    }

    m_isWrite = false;
    m_isMemory = false;

    std::fseek(m_file, 0, SEEK_END);
    m_fileSize = static_cast<RwUInt32>(std::ftell(m_file));
    std::fseek(m_file, 0, SEEK_SET);
    return true;
}

bool RwStream::OpenReadMemory(const void* data, size_t size)
{
    if (!Close()) {
        return false;
    }
    m_lastError.clear();

    if (!data && size != 0) {
        SetError("Invalid memory buffer");
        return false;
    }

    m_isWrite = false;
    m_isMemory = true;
    m_memData = static_cast<const RwUInt8*>(data);
    m_memSize = size;
    m_memPos = 0;
    m_fileSize = static_cast<RwUInt32>(size);
    return true;
}

bool RwStream::OpenWrite(const char* filename)
{
    if (!Close()) {
        return false;
    }
    m_lastError.clear();

    m_file = std::fopen(filename, "w+b");
    if (!m_file) {
        SetError("Failed to open file for writing");
        return false;
    }

    m_isWrite = true;
    m_isMemory = false;
    m_fileSize = 0;
    return true;
}

bool RwStream::OpenWriteMemory()
{
    if (!Close()) {
        return false;
    }
    m_lastError.clear();

    m_isWrite = true;
    m_isMemory = true;
    m_memPos = 0;
    m_fileSize = 0;
    m_writeBuf.clear();
    return true;
}

#ifdef _WIN32
bool RwStream::OpenRead(const wchar_t* filename)
{
    if (!Close()) {
        return false;
    }
    m_lastError.clear();

    m_file = _wfopen(filename, L"rb");
    if (!m_file) {
        SetError("Failed to open file for reading");
        return false;
    }

    m_isWrite = false;
    m_isMemory = false;

    std::fseek(m_file, 0, SEEK_END);
    m_fileSize = static_cast<RwUInt32>(std::ftell(m_file));
    std::fseek(m_file, 0, SEEK_SET);
    return true;
}

bool RwStream::OpenWrite(const wchar_t* filename)
{
    if (!Close()) {
        return false;
    }
    m_lastError.clear();

    m_file = _wfopen(filename, L"w+b");
    if (!m_file) {
        SetError("Failed to open file for writing");
        return false;
    }

    m_isWrite = true;
    m_isMemory = false;
    m_fileSize = 0;
    return true;
}
#endif

bool RwStream::Close()
{
    bool success = true;
    if (m_file) {
        const int ioError = std::ferror(m_file);
        const int closeResult = std::fclose(m_file);
        m_file = nullptr;
        if (ioError || closeResult != 0) {
            if (m_lastError.empty()) {
                SetError(ioError ? "File stream I/O failed" : "Failed to close file");
            }
            success = false;
        }
    }
    m_isWrite = false;
    m_fileSize = 0;
    m_isMemory = false;
    m_memData = nullptr;
    m_memSize = 0;
    m_memPos = 0;
    m_writeBuf.clear();
    m_declaredScopes.clear();
    return success;
}

bool RwStream::Fingerprint(RwUInt32 offset, RwUInt32 size, std::uint64_t& value)
{
    value = 14695981039346656037ULL;
    if (m_isMemory) {
        const auto* data = m_isWrite ? m_writeBuf.data() : m_memData;
        for (RwUInt32 i = 0; i < size; ++i) {
            value = (value ^ data[offset + i]) * 1099511628211ULL;
        }
        return true;
    }
    const RwUInt32 position = GetPosition();
    if (!Seek(offset)) {
        return false;
    }
    RwUInt8 buffer[4096];
    for (RwUInt32 remaining = size; remaining != 0;) {
        const size_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (std::fread(buffer, 1, count, m_file) != count) {
            SetError("Failed to read chunk content for fingerprint");
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            value = (value ^ buffer[i]) * 1099511628211ULL;
        }
        remaining -= static_cast<RwUInt32>(count);
    }
    return Seek(position);
}

// ============================================================================
// Position
// ============================================================================

RwUInt32 RwStream::GetPosition() const
{
    if (m_isMemory) {
        return static_cast<RwUInt32>(m_memPos);
    }
    if (!m_file) {
        return 0;
    }
    return static_cast<RwUInt32>(std::ftell(m_file));
}

bool RwStream::Seek(RwUInt32 position)
{
    if (!m_lastError.empty()) {
        return false;
    }
    if (m_isMemory) {
        if (m_isWrite) {
            // Seeking past the end while writing zero-fills (mirrors fseek on a file).
            if (position > m_writeBuf.size()) {
                m_writeBuf.resize(position, 0);
            }
            m_memPos = position;
            return true;
        }
        if (position > m_memSize) {
            return false;
        }
        m_memPos = position;
        return true;
    }
    if (!m_file) {
        return false;
    }
    if (std::fseek(m_file, static_cast<long>(position), SEEK_SET) != 0) {
        SetError("Failed to seek file");
        return false;
    }
    return true;
}

bool RwStream::Skip(RwInt32 offset)
{
    if (!m_lastError.empty()) {
        return false;
    }
    if (m_isMemory) {
        const std::int64_t next = static_cast<std::int64_t>(m_memPos) + static_cast<std::int64_t>(offset);
        if (next < 0) {
            return false;
        }
        return Seek(static_cast<RwUInt32>(next));
    }
    if (!m_file) {
        return false;
    }
    if (std::fseek(m_file, offset, SEEK_CUR) != 0) {
        SetError("Failed to seek file");
        return false;
    }
    return true;
}

bool RwStream::IsEOF() const
{
    if (m_isMemory) {
        if (m_isWrite) {
            return m_memPos >= m_writeBuf.size();
        }
        return m_memPos >= m_memSize;
    }
    if (!m_file) {
        return true;
    }
    return std::feof(m_file) != 0 || GetPosition() >= m_fileSize;
}

// ============================================================================
// Reading
// ============================================================================

bool RwStream::Read(void* buffer, size_t size)
{
    if (size == 0) {
        return false;
    }

    if (m_isMemory) {
        if (m_isWrite || !m_memData) {
            return false;
        }
        if (m_memPos + size > m_memSize) {
            return false;
        }
        std::memcpy(buffer, m_memData + m_memPos, size);
        m_memPos += size;
        return true;
    }

    if (!m_file || m_isWrite) {
        return false;
    }
    return std::fread(buffer, 1, size, m_file) == size;
}

bool RwStream::ReadInt8(RwInt8* value) { return Read(value, sizeof(RwInt8)); }
bool RwStream::ReadUInt8(RwUInt8* value) { return Read(value, sizeof(RwUInt8)); }

bool RwStream::ReadInt16(RwInt16* value)
{
    if (!Read(value, sizeof(RwInt16))) {
        return false;
    }
    if (!IsLittleEndian()) {
        SwapEndian16(value);
    }
    return true;
}

bool RwStream::ReadUInt16(RwUInt16* value)
{
    return ReadInt16(reinterpret_cast<RwInt16*>(value));
}

bool RwStream::ReadInt32(RwInt32* value)
{
    if (!Read(value, sizeof(RwInt32))) {
        return false;
    }
    if (!IsLittleEndian()) {
        SwapEndian32(value);
    }
    return true;
}

bool RwStream::ReadUInt32(RwUInt32* value)
{
    return ReadInt32(reinterpret_cast<RwInt32*>(value));
}

bool RwStream::ReadReal(RwReal* value)
{
    if (!Read(value, sizeof(RwReal))) {
        return false;
    }
    if (!IsLittleEndian()) {
        SwapEndian32(value);
    }
    return true;
}

bool RwStream::ReadReals(RwReal* values, size_t count)
{
    if (count == 0) {
        return true;
    }
    if (!Read(values, sizeof(RwReal) * count)) {
        return false;
    }
    if (!IsLittleEndian()) {
        SwapEndian32Array(values, count);
    }
    return true;
}

bool RwStream::ReadMatrix(RwMatrix* value)
{
    return ReadV3d(&value->right) && ReadUInt32(&value->flags) &&
           ReadV3d(&value->up) && ReadUInt32(&value->pad1) &&
           ReadV3d(&value->at) && ReadUInt32(&value->pad2) &&
           ReadV3d(&value->pos) && ReadUInt32(&value->pad3);
}

// ============================================================================
// Writing
// ============================================================================

bool RwStream::Write(const void* buffer, size_t size)
{
    if (!m_lastError.empty()) {
        return false;
    }
    if (size == 0) {
        return false;
    }

    if (m_isMemory) {
        if (!m_isWrite) {
            return false;
        }
        if (m_memPos + size > m_writeBuf.size()) {
            m_writeBuf.resize(m_memPos + size, 0);
        }
        std::memcpy(m_writeBuf.data() + m_memPos, buffer, size);
        m_memPos += size;
        return true;
    }

    if (!m_file || !m_isWrite) {
        return false;
    }
    if (std::fwrite(buffer, 1, size, m_file) != size) {
        SetError("Failed to write file");
        return false;
    }
    return true;
}

bool RwStream::WriteInt8(RwInt8 value) { return Write(&value, sizeof(RwInt8)); }
bool RwStream::WriteUInt8(RwUInt8 value) { return Write(&value, sizeof(RwUInt8)); }

bool RwStream::WriteInt16(RwInt16 value)
{
    if (!IsLittleEndian()) {
        SwapEndian16(&value);
    }
    return Write(&value, sizeof(RwInt16));
}

bool RwStream::WriteUInt16(RwUInt16 value)
{
    return WriteInt16(static_cast<RwInt16>(value));
}

bool RwStream::WriteInt32(RwInt32 value)
{
    if (!IsLittleEndian()) {
        SwapEndian32(&value);
    }
    return Write(&value, sizeof(RwInt32));
}

bool RwStream::WriteUInt32(RwUInt32 value)
{
    return WriteInt32(static_cast<RwInt32>(value));
}

bool RwStream::WriteReal(RwReal value)
{
    if (!IsLittleEndian()) {
        SwapEndian32(&value);
    }
    return Write(&value, sizeof(RwReal));
}

bool RwStream::WriteReals(const RwReal* values, size_t count)
{
    if (count == 0) {
        return true;
    }
    if (IsLittleEndian()) {
        return Write(values, sizeof(RwReal) * count);
    }
    for (size_t i = 0; i < count; ++i) {
        if (!WriteReal(values[i])) {
            return false;
        }
    }
    return true;
}

bool RwStream::WriteMatrix(const RwMatrix& value)
{
    return WriteV3d(value.right) && WriteUInt32(value.flags) &&
           WriteV3d(value.up) && WriteUInt32(value.pad1) &&
           WriteV3d(value.at) && WriteUInt32(value.pad2) &&
           WriteV3d(value.pos) && WriteUInt32(value.pad3);
}

bool RwStream::WritePadding(size_t count, RwUInt8 value)
{
    if (count == 0) {
        return true;
    }
    std::vector<RwUInt8> pad(count, value);
    return Write(pad.data(), pad.size());
}

// ============================================================================
// Chunk primitives
// ============================================================================

bool RwStream::ReadChunkHeader(RwChunkHeader& header)
{
    return ReadUInt32(&header.type) && ReadUInt32(&header.size) && ReadUInt32(&header.version);
}

bool RwStream::WriteChunkHeader(RwUInt32 type, RwUInt32 size, RwUInt32 version)
{
    return WriteUInt32(type) && WriteUInt32(size) && WriteUInt32(version);
}

// ============================================================================
// Declared-size scope tracking
// ============================================================================

void RwStream::PushDeclaredScope()
{
    m_declaredScopes.push_back(0);
}

std::int64_t RwStream::PopDeclaredScope()
{
    if (m_declaredScopes.empty()) {
        return 0;
    }
    const std::int64_t excess = m_declaredScopes.back();
    m_declaredScopes.pop_back();
    return excess;
}

void RwStream::AddDeclaredExcess(std::int64_t excess)
{
    if (!m_declaredScopes.empty()) {
        m_declaredScopes.back() += excess;
    }
}

// ============================================================================
// Endian helpers
// ============================================================================

void RwStream::SwapEndian16(void* data)
{
    auto* bytes = static_cast<RwUInt8*>(data);
    const RwUInt8 t = bytes[0];
    bytes[0] = bytes[1];
    bytes[1] = t;
}

void RwStream::SwapEndian32(void* data)
{
    auto* bytes = static_cast<RwUInt8*>(data);
    RwUInt8 t;
    t = bytes[0]; bytes[0] = bytes[3]; bytes[3] = t;
    t = bytes[1]; bytes[1] = bytes[2]; bytes[2] = t;
}

void RwStream::SwapEndian32Array(void* data, size_t count)
{
    auto* arr = static_cast<RwUInt32*>(data);
    for (size_t i = 0; i < count; ++i) {
        SwapEndian32(&arr[i]);
    }
}

bool RwStream::IsLittleEndian()
{
    static const RwUInt32 test = 1;
    return *reinterpret_cast<const RwUInt8*>(&test) == 1;
}

void RwStream::SetError(const char* msg)
{
    m_lastError = msg ? msg : "";
}

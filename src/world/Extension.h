/**
 * world/Extension.h - rwID_EXTENSION container bookkeeping and traversal.
 *
 * Every RenderWare object (frame, geometry, material, texture, atomic, clump)
 * ends with an EXTENSION chunk holding zero or more plugin chunks, streamed in
 * plugin registration order. That order differs between exporters -- DBO files
 * contain both [HANIM, USERDATA] and [USERDATA, HANIM] frame orders -- so it is
 * data and must be replayed exactly.
 *
 * Traversal follows _rwPluginRegistryReadDataChunks
 * (RenderWare batkbin.c) exactly:
 *
 *     while (length > 0) {
 *         read chunk header (readType, readLength)
 *         if a plugin claims readType -> its readCB consumes whatever it needs
 *         else                        -> RwStreamSkip(readLength)
 *         length -= (readLength + rwCHUNKHEADERSIZE);
 *     }
 *
 * Two properties matter and are the reason DBO's mis-sized chunks load at all:
 *
 *   1. Loop termination is driven by the *declared* lengths, not by the stream
 *      position. A plugin whose declared size disagrees with its payload still
 *      leaves the loop counting correctly.
 *   2. The stream advances by what the plugin reader actually consumed. So a
 *      reader that parses semantically (as RpNtlMaterialExt does, via
 *      RwStreamFindChunk + RwTextureStreamRead) stays in sync regardless of the
 *      declared size.
 *
 * Implementing the same rule means no slack constants, no look-ahead and no
 * special cases are needed for the DBO size quirks.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <algorithm>
#include <functional>
#include <vector>

struct DffExtensionList
{
    /// Chunk types in the order they appeared inside the EXTENSION container.
    std::vector<RwUInt32> order;
    /// Payload of chunks kept verbatim, in order of appearance among themselves.
    std::vector<RawChunk> raws;
    /// The EXTENSION container header itself (for declared-size preservation).
    RwChunkSpan span;

    void Clear()
    {
        order.clear();
        raws.clear();
        span.Reset();
    }

    bool Contains(RwUInt32 type) const
    {
        return std::find(order.begin(), order.end(), type) != order.end();
    }

    void Append(RwUInt32 type) { order.push_back(type); }

    void Remove(RwUInt32 type)
    {
        order.erase(std::remove(order.begin(), order.end(), type), order.end());
    }

    /**
     * Inserts `type` before the first unmodelled chunk, keeping known plugins
     * grouped ahead of pass-through data (matches RW's registration order).
     * No-op when already present or when no order was recorded.
     */
    template <typename IsKnownFn>
    void EnsurePresent(RwUInt32 type, IsKnownFn isKnown)
    {
        if (order.empty() || Contains(type)) {
            return;
        }
        auto it = std::find_if(order.begin(), order.end(), [&](RwUInt32 t) { return !isKnown(t); });
        order.insert(it, type);
    }
};

/**
 * Per-chunk handler used by ReadExtensionContainer.
 *
 * Return false to abort with an error. Set `handled` to true if the payload was
 * consumed (the stream may sit anywhere the semantic parse ended); leave it
 * false to have the container keep the chunk verbatim.
 */
using DffExtensionChunkHandler = std::function<bool(const RwChunkHeader& header, bool& handled)>;

/**
 * Walks an EXTENSION container using RW's declared-length-driven loop.
 *
 * `declaredLength` is the EXTENSION chunk's header size. On return the stream
 * sits wherever the last handler left it (which, for DBO files, can differ from
 * containerStart + declaredLength).
 */
bool ReadExtensionContainer(RwStream& stream,
                            DffDiagnostics& diag,
                            RwUInt32 declaredLength,
                            RwUInt32 containerVersion,
                            DffExtensionList& list,
                            const DffExtensionChunkHandler& handler);

/**
 * RawChunkCursor - walks `raws` in order while a writer replays `order`.
 *
 * Guarantees the raw payloads line up with the recorded order; a mismatch means
 * the model was mutated inconsistently and is reported as a write error.
 */
class RawChunkCursor
{
public:
    explicit RawChunkCursor(const std::vector<RawChunk>& raws) : m_raws(raws) {}

    const RawChunk* Next(RwUInt32 expectedType)
    {
        if (m_index >= m_raws.size()) {
            return nullptr;
        }
        const RawChunk& c = m_raws[m_index];
        if (c.type != expectedType) {
            return nullptr;
        }
        ++m_index;
        return &c;
    }

    bool Exhausted() const { return m_index == m_raws.size(); }
    size_t Index() const { return m_index; }

private:
    const std::vector<RawChunk>& m_raws;
    size_t                       m_index = 0;
};

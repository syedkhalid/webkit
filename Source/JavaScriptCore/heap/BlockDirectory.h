/*
 * Copyright (C) 2012-2018 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include "AllocationFailureMode.h"
#include "Allocator.h"
#include "CellAttributes.h"
#include "FreeList.h"
#include "LocalAllocator.h"
#include "MarkedBlock.h"
#include <wtf/DataLog.h>
#include <wtf/FastBitVector.h>
#include <wtf/SharedTask.h>
#include <wtf/Vector.h>

namespace JSC {

class GCDeferralContext;
class Heap;
class IsoCellSet;
class MarkedSpace;
class LLIntOffsetsExtractor;
class ThreadLocalCacheLayout;

#define FOR_EACH_BLOCK_DIRECTORY_BIT(macro) \
    macro(live, Live) /* The set of block indices that have actual blocks. */\
    macro(empty, Empty) /* The set of all blocks that have no live objects. */ \
    macro(allocated, Allocated) /* The set of all blocks that are full of live objects. */\
    macro(canAllocateButNotEmpty, CanAllocateButNotEmpty) /* The set of all blocks are neither empty nor retired (i.e. are more than minMarkedBlockUtilization full). */ \
    macro(destructible, Destructible) /* The set of all blocks that may have destructors to run. */\
    macro(eden, Eden) /* The set of all blocks that have new objects since the last GC. */\
    macro(unswept, Unswept) /* The set of all blocks that could be swept by the incremental sweeper. */\
    \
    /* These are computed during marking. */\
    macro(markingNotEmpty, MarkingNotEmpty) /* The set of all blocks that are not empty. */ \
    macro(markingRetired, MarkingRetired) /* The set of all blocks that are retired. */

// FIXME: We defined canAllocateButNotEmpty and empty to be exclusive:
//
//     canAllocateButNotEmpty & empty == 0
//
// Instead of calling it canAllocate and making it inclusive:
//
//     canAllocate & empty == empty
//
// The latter is probably better. I'll leave it to a future bug to fix that, since breathing on
// this code leads to regressions for days, and it's not clear that making this change would
// improve perf since it would not change the collector's behavior, and either way the directory
// has to look at both bitvectors.
// https://bugs.webkit.org/show_bug.cgi?id=162121

class BlockDirectory {
    WTF_MAKE_NONCOPYABLE(BlockDirectory);
    WTF_MAKE_FAST_ALLOCATED;
    
    friend class LLIntOffsetsExtractor;

public:
    BlockDirectory(Heap*, size_t cellSize);
    ~BlockDirectory();
    void setSubspace(Subspace*);
    void lastChanceToFinalize();
    void prepareForAllocation();
    void stopAllocating();
    void stopAllocatingForGood();
    void resumeAllocating();
    void beginMarkingForFullCollection();
    void endMarking();
    void snapshotUnsweptForEdenCollection();
    void snapshotUnsweptForFullCollection();
    void sweep();
    void shrink();
    void assertNoUnswept();
    size_t cellSize() const { return m_cellSize; }
    const CellAttributes& attributes() const { return m_attributes; }
    bool needsDestruction() const { return m_attributes.destruction == NeedsDestruction; }
    DestructionMode destruction() const { return m_attributes.destruction; }
    HeapCell::Kind cellKind() const { return m_attributes.cellKind; }
    Heap* heap() { return m_heap; }

    bool isFreeListedCell(const void* target);

    template<typename Functor> void forEachBlock(const Functor&);
    template<typename Functor> void forEachNotEmptyBlock(const Functor&);
    
    RefPtr<SharedTask<MarkedBlock::Handle*()>> parallelNotEmptyBlockSource();
    
    void addBlock(MarkedBlock::Handle*);
    void removeBlock(MarkedBlock::Handle*);

    bool isPagedOut(double deadline);
    
    Lock& bitvectorLock() { return m_bitvectorLock; }

#define BLOCK_DIRECTORY_BIT_ACCESSORS(lowerBitName, capitalBitName)     \
    bool is ## capitalBitName(const AbstractLocker&, size_t index) const { return m_ ## lowerBitName[index]; } \
    bool is ## capitalBitName(const AbstractLocker& locker, MarkedBlock::Handle* block) const { return is ## capitalBitName(locker, block->index()); } \
    void setIs ## capitalBitName(const AbstractLocker&, size_t index, bool value) { m_ ## lowerBitName[index] = value; } \
    void setIs ## capitalBitName(const AbstractLocker& locker, MarkedBlock::Handle* block, bool value) { setIs ## capitalBitName(locker, block->index(), value); }
    FOR_EACH_BLOCK_DIRECTORY_BIT(BLOCK_DIRECTORY_BIT_ACCESSORS)
#undef BLOCK_DIRECTORY_BIT_ACCESSORS

    template<typename Func>
    void forEachBitVector(const AbstractLocker&, const Func& func)
    {
#define BLOCK_DIRECTORY_BIT_CALLBACK(lowerBitName, capitalBitName) \
        func(m_ ## lowerBitName);
        FOR_EACH_BLOCK_DIRECTORY_BIT(BLOCK_DIRECTORY_BIT_CALLBACK);
#undef BLOCK_DIRECTORY_BIT_CALLBACK
    }
    
    template<typename Func>
    void forEachBitVectorWithName(const AbstractLocker&, const Func& func)
    {
#define BLOCK_DIRECTORY_BIT_CALLBACK(lowerBitName, capitalBitName) \
        func(m_ ## lowerBitName, #capitalBitName);
        FOR_EACH_BLOCK_DIRECTORY_BIT(BLOCK_DIRECTORY_BIT_CALLBACK);
#undef BLOCK_DIRECTORY_BIT_CALLBACK
    }
    
    BlockDirectory* nextDirectory() const { return m_nextDirectory; }
    BlockDirectory* nextDirectoryInSubspace() const { return m_nextDirectoryInSubspace; }
    BlockDirectory* nextDirectoryInAlignedMemoryAllocator() const { return m_nextDirectoryInAlignedMemoryAllocator; }
    
    void setNextDirectory(BlockDirectory* directory) { m_nextDirectory = directory; }
    void setNextDirectoryInSubspace(BlockDirectory* directory) { m_nextDirectoryInSubspace = directory; }
    void setNextDirectoryInAlignedMemoryAllocator(BlockDirectory* directory) { m_nextDirectoryInAlignedMemoryAllocator = directory; }
    
    MarkedBlock::Handle* findEmptyBlockToSteal();
    
    MarkedBlock::Handle* findBlockToSweep();
    
    Subspace* subspace() const { return m_subspace; }
    MarkedSpace& markedSpace() const;
    
    Allocator allocator() const { return Allocator(m_tlcOffset); }
    
    void dump(PrintStream&) const;
    void dumpBits(PrintStream& = WTF::dataFile());
    
private:
    friend class LocalAllocator;
    friend class IsoCellSet;
    friend class MarkedBlock;
    friend class ThreadLocalCacheLayout;
    
    MarkedBlock::Handle* findBlockForAllocation();
    
    MarkedBlock::Handle* tryAllocateBlock();
    
    Vector<MarkedBlock::Handle*> m_blocks;
    Vector<unsigned> m_freeBlockIndices;

    // Mutator uses this to guard resizing the bitvectors. Those things in the GC that may run
    // concurrently to the mutator must lock this when accessing the bitvectors.
    Lock m_bitvectorLock;
#define BLOCK_DIRECTORY_BIT_DECLARATION(lowerBitName, capitalBitName) \
    FastBitVector m_ ## lowerBitName;
    FOR_EACH_BLOCK_DIRECTORY_BIT(BLOCK_DIRECTORY_BIT_DECLARATION)
#undef BLOCK_DIRECTORY_BIT_DECLARATION
    
    // After you do something to a block based on one of these cursors, you clear the bit in the
    // corresponding bitvector and leave the cursor where it was.
    size_t m_allocationCursor { 0 }; // Points to the next block that is a candidate for allocation.
    size_t m_emptyCursor { 0 }; // Points to the next block that is a candidate for empty allocation (allocating in empty blocks).
    size_t m_unsweptCursor { 0 }; // Points to the next block that is a candidate for incremental sweeping.
    
    unsigned m_cellSize;
    CellAttributes m_attributes;
    // FIXME: All of these should probably be references.
    // https://bugs.webkit.org/show_bug.cgi?id=166988
    Heap* m_heap { nullptr };
    Subspace* m_subspace { nullptr };
    BlockDirectory* m_nextDirectory { nullptr };
    BlockDirectory* m_nextDirectoryInSubspace { nullptr };
    BlockDirectory* m_nextDirectoryInAlignedMemoryAllocator { nullptr };
    
    Lock m_localAllocatorsLock;
    size_t m_tlcOffset;
    SentinelLinkedList<LocalAllocator, BasicRawSentinelNode<LocalAllocator>> m_localAllocators;
};

} // namespace JSC

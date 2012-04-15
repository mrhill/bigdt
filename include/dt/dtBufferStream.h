#ifndef dtBufferFile_H_
#define dtBufferFile_H_

/** @file dtBufferStream.h
    Large file buffers with caching.

    <b>Overview</b>

    dtBufferStream provides an interface to do memory-buffered reads and edits on
    large (64 bit) files.

    The underlying file will be opened in read-only mode, and mapped partially into memory.
    Only changed segments of a file are kept in memory, unchanged segments can be swapped
    in and out.

    dtSegment describes a buffer segment. A segment can be of the following types:

    - dtSEGMENTTYPE_NULL: unmapped file segment. dtSegment::mFileSize bytes starting at
      dtSegment::mFileOffset are logically mapped at the current buffer offset.

    - dtSEGMENTTYPE_MAP: mapped file segment. dtSegment::mSize bytes from buffer
      dtSegment::mpData are replacing dtSegment::mFileSize bytes from the file at the
      current buffer offset.

    Segments are indexed via 2 structures: double-linked list and a binary tree.

    Segments do not store their absolute buffer offset, to avoid structure updating
    after inserts or deletes. Instead the buffer offset of a segment can be calculated
    by walking the index tree (see below).

    Segments of type dtSEGMENTTYPE_MAP do not store the file offset of the replaced
    data, because this information is generally not needed. The only time, when the
    file offset is needed is, when a buffer is saved back to disk. In this case
    segments will be processed sequentially and the file offset can be calculated by
    adding up segment sizes.

    <b>Double-linked list</b>

    Via dtSegment::mPrev and dtSegment::mNext, circular wrap at buffer end.
    dtBufferStream::mSegmentUsedFirst and dtBufferStream::mSegmentUsedLast point
    to the first and last segment in the chain.

    <b>Binary tree</b>

    dtBufferStream::mSegmentUsedRoot designates the tree root, leafs are referenced
    via dtSegment::mLT and dtSegment::mGE. A segment's absolute buffer offset is the
    sorting criterium.

    dtSegment::mOffset stores a segment start's buffer offset relative to its tree
    parent (relative to 0 for root). This allows for insertion and deletion of data
    without the need to update too many offset fields.

    Special restriction for 0-sized segments:
    - as an optimization measure, 0-sized Null-segments are not allowed, except for
      the empty-buffer case where exactly 1 0-size Null- or Map-segment exists
    - a 0-sized Map-segment must exist only as a right child
*/

#include "dtBuffer.h"
#include "dtStream.h"
#include "dtSegmentTree.h"
#include "babel/file.h"

struct dtPage
{
    bbU8*   mpData;
    bbU32   mSize;
    bbU8    mNextFree;
    bbU8    mIndex;
};

/** Optimum size for cached file segment. Must be power of 2. */
#define dtBUFFERSTREAM_SEGMENTSIZE 0x80000UL
#define dtBUFFERSTREAM_MAXPAGES dtBUFFER_MAXSECTIONS

/** Large file buffer implementation with changes caching.

    This buffer implementation allows to handle large files (64 bit).
    Reads on the file are memory cached. Writes are memory cached until a
    cumulative size limit is reached, then tempfile caching is used.
*/
class dtBufferStream : public dtBuffer, private dtSegmentTree
{
private:
    bbUINT          mPageFree;          //!< Next free index in mPagePool[]

    bbU32           mSegmentLastMapped; //!< Index of last and still mapped segment, or -1 if none
    bbU64           mSegmentLastOffset; //!< Startoffset of mSegmentLastMapped segment

    bbFILEH         mhFile;             //!< Handle to underlying file
    bbFILEH         mhTempFile;         //!< Handle to temp file

    dtPage          mPagePool[dtBUFFERSTREAM_MAXPAGES];

#ifdef bbDEBUG
public:
    bbUINT mHitCount;
    bbUINT mMissCount;

    enum dtOP { DEL, INS_DISCARD, INS_COMMIT };
    void DumpSavedTree();
    void LoadSavedTree(const bbCHAR* const pFileName, int const execute);
    void SaveTree(dtOP op, bbU64 offset, bbU64 size);
    void CheckTree();
    bbU32 DebugCheck();                 //!< Test integrity, returns buffer CRC
    void DumpSegments(bbFILEH hFile = NULL);
#endif

private:
    dtPage* PageAlloc(bbU32 const size);

    /** Free mapped section descriptor, previously allocated via SectionGet().
        @param pSec Pointer to section descriptor
    */
    inline void PageFree(dtPage* const pPage)
    {
        if (pPage)
        {
            pPage->mNextFree = mPageFree;
            mPageFree = pPage->mIndex;
        }
    }

    /** Clear segment index. */
    void ClearSegments();

    inline void NodeSubstractOffset(bbU32 const idx, bbU64 const segmentstart, bbS64 const diff)
    {
        bbASSERT(segmentstart <= mBufSize);
        dtSegmentTree::NodeSubstractOffset(idx, segmentstart, diff);
    }

public:
    static bbCHAR*  spTempDir;    //!< Path to store temporary files

    dtBufferStream();
    virtual ~dtBufferStream();

    /** Create and open a buffer.
        @param pPath Pointer to 0-terminated path of file to open, 
                     NULL to create a new buffer, or
                     -1 to create an unopened buffer
        @return Pointer to opened buffer object, or NULL on failure.
    */
    static dtBufferStream* Create(const bbCHAR* pPath);

    virtual bbERR OnOpen(const bbCHAR* const pPath, int isnew);
    virtual void  OnClose();
    virtual bbERR OnSave(const bbCHAR* pPath, dtBUFFERSAVETYPE const savetype);
    virtual bbERR Delete( bbU64 const offset, bbU64 size, void* const user);
    virtual dtSection* Insert(bbU64 const offset, bbU32 const size);
    virtual dtSection* Map(bbU64 offset, bbU32 size, dtMAP const accesshint);
    virtual dtSection* MapSeq(bbU64 const offset, bbUINT minsize, dtMAP const accesshint);
    virtual bbERR Commit(dtSection* const pSection, void* const user);
    virtual void Discard(dtSection* const pSection);

    friend class e7WinDbg;
};

#endif /* dtBufferFile_H_ */


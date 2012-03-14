#ifndef dtSegmentTree_H_
#define dtSegmentTree_H_

#include "dtdefs.h"
#include "babel/Arr.h"

/** dtSegment::mType segment type */
enum dtSEGMENTTYPE
{
    dtSEGMENTTYPE_NULL = 0, //!< Segment is an unmapped portion of the file
    dtSEGMENTTYPE_MAP,      //!< Segment is a memory mapped portion of the file
};

/** Descriptor for a cached file segment. */
struct dtSegment
{
    bbU32   mLT;        //!< index of node with offset less than this node, (bbU32)-1 is NIL
    bbU32   mGE;        //!< index of node with offset greater or equal than this node, (bbU32)-1 is NIL
    bbU32   mPrev;      //!< Previous index, used, circular
    bbU32   mNext;      //!< Next index, used or free, circular
    bbU64   mOffset;    //!< Buffer offset, relative to parent segment, root is absolute
    bbU64   mFileSize;  //!< Original size of segment on file
    union {
    bbU64   mFileOffset;//!< File offset of segment (not buffer offset), valid for dtSEGMENTTYPE_NULL
    struct {
    bbU8*   mpData;     //!< Pointer to heap block containing data, valid for dtSEGMENTTYPE_MAP
    bbU32   mSize;      //!< Size of cached \a mpData block in bytes, valid for dtSEGMENTTYPE_MAP
    };
    };
    bbU8    mType;      //!< Segment type, see dtSEGMENTTYPE
    bbU8    mLevel;     //!< AA-tree node level
    bbU8    mChanged;   //!< !=0 if segment is changed, valid for dtSEGMENTTYPE_MAP only
    bbU8    mOpt;       //!< do not use
    bbU32   mCapacity;  //!< do not use

    inline bbU64 GetSize() const
    {
        return mType==dtSEGMENTTYPE_NULL ? mFileSize : (bbU64)mSize;
    }
};

/** Number of entries to enlarge dtBufferStream::mSegments on each realloc. */
#define dtSEGMENTTREE_IDXENLARGE 32

#if bbSIZEOF_UPTR==4
bbDECLAREARR(dtSegment, dtArrSegment, 48);
#elif bbSIZEOF_UPTR==8
bbDECLAREARR(dtSegment, dtArrSegment, 56);
#endif

/** Tree of buffer segments. */
class dtSegmentTree
{
protected:
    dtArrSegment mSegments;          //!< Tree nodes
    bbU32        mSegmentFree;       //!< Index of first free entry in mSegments[]
    bbU32        mSegmentUsedFirst;  //!< mSegments[] index of first node
    bbU32        mSegmentUsedLast;   //!< mSegments[] index of last node
    bbU32        mSegmentUsedRoot;   //!< mSegments[] index of tree root node

#ifdef bbDEBUG
    virtual void CheckTree() = 0;
#endif    

    dtSegmentTree();
    ~dtSegmentTree();

    /** Delete all nodes. */
    void ClearSegments();

    /** Allocate a new node.
        The allocated dtSegment struct will be unitialized.
        This function invalidates any dtSegment* pointers, because it may reallocate the mSegments[] array.
        @return Index into mSegments[], or (bbU32)-1 on error
    */
    bbU32 NewSegment();

    /** Undo a NewSegment() call.
        Must be called only, if segment hasn't been linked yet, dtSegment::mNext must be unchanged.
        Only undos the last call to NewSegment().
        @param Node index into mSegments[] for allocated node returned from last NewSegment()
    */
    inline void UndoSegment(bbU32 const idx)
    {
        mSegmentFree = idx;
    }

    /** Find segment at specified buffer offset (not file offset).
        If the offset points to the boundary between two segments, the right segment is returned.
        If the offset is larger or equal than the buffer size, behaviour is defined by parameter
        \a wrap:
        - wrap == 0 : the index of the last segment is returned
        - wrap != 0 : the index of the first segment is returned
        This calls always succeeds.
        @param offset Buffer offset
        @param pSegmentStart Buffer offset, the segment starts
        @param wrap Behavior for offset >= buffer size
        @return Segment index
    */
    bbU32 FindSegment(bbU64 const offset, bbU64* const pSegmentStart, int const wrap);

    /** Link new segment to its left neighbour.
        The segment with mSegments[] index \a idx must be the right neighbour of \a pSubTree.
        Will not adjust any tree index offsets.
        @param idx      Index of new segment to link
        @param pSubTree Left neighbour of \a idx, this is the root of a subtree
        @param offset   Buffer offset of \a idx relative to \a pSubTree
    */
    void NodeLinkRight(bbU32 const idx, dtSegment* pSubTree, bbS64 offset);

    /** Link new segment into tree and adjust all tree offsets.
        The segment \a mSegments[idx] must have dtSegment::mPrev and dtSegment::mNext initialized
        already. Also dtSegment::GetSize() must return the segment's size.
        @param idx    Index of new segment to link
        @param segmentstart Absolute offset of segment \a idx, must be >0
    */
    void NodeInsert(bbU32 const idx, bbU64 const segmentstart);

    /** Adjust relative tree offsets after reducing or increasing a segment size.
        This function will do bookkeeping on the relative offsets in the
        index tree, after reducing a segment size. The implementation will not look at the
        node's size, so it does not matter whether it is changed before or after this call.
        @param idx Index of segment which was reduced in size, mSize member already reduced
        @param segmentstart Absolute offset of segment \a idx
        @param diff Number of bytes the segment was reduced (positive number) or increase (negative number)
    */
    void NodeSubstractOffset(bbU32 const idx, bbU64 const segmentstart, bbS64 const diff);

    /** Delete node from index tree.
        Will unlink node from index tree, and adjust relative offsets of remaining nodes.
        Will not update the linked list, nor return the node into free pool.
        @param idx Node to be deleted
        @param segmentstart Absolute buffer offset of node
    */
    void NodeDelete(bbU32 const idx, bbU64 const segmentstart);

    /** Split Null segment into two Null segments.

        \a segmentoffset must not be 0, and smaller or equal than \a idx segment's size.

        If \a segmentoffset is at the segment end, no new segment is inserted,
        instead the index of the next node in mSegments[] is returned. In this
        case the returned segment may not be of type Null.
        
        This function invalidates any dtSegment* pointers, because it may
        reallocate the mSegments[] array.

        @param idx Index of segment to split, must be Null segment
        @param segmentoffset Segment-relative offset to split at
        @return Index of inserted right Null segment, or -1 on failure
    */
    bbU32 SplitNullSegment(bbU32 const idx, bbU64 segmentoffset);

    /** Split Map segment into two Map segments.

        \a segmentoffset must not be 0 and smaller or equal than \a idx segment's size.

        If \a segmentoffset is at the segment end, no new segment is inserted,
        instead the index of the next node in mSegments[] is returned, in this
        case the returned segment may not be of type Map.
        
        This function invalidates any dtSegment* pointers, because it may
        reallocate the mSegments[] array.

        @param idx Index of segment to split, must be Map segment
        @param segmentoffset Segment-relative offset to split at. This is a 32 bit offset,
                             because a mapped segment is always smaller than 4 GB.
        @return Index of inserted right Null segment, or -1 on failure
    */
    bbU32 SplitMapSegment(bbU32 const idx, bbU32 const segmentoffset);
};

#endif /* dtSegmentTree_H_ */


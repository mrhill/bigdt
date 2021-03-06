#ifndef dtRef_H_
#define dtRef_H_

#include "dtdefs.h"

/** Buffer reference point.

    Defined method to reset a text reference point to dtBuffer
    start is to set all members to 0, and initialize the mLogLine member.

    dtRefPt::mEncstate is intended for storing the data or character
    encoding state at a given text reference point, e.g. e7WinText will
    copy this to bbENCSTATE::state.
*/
struct dtRefPt
{
    bbU64 mOffset;      //!< Buffer offset
    bbU64 mLine;        //!< Physical line number, starting at 0
    bbU8  mBitOffs;     //!< Buffer bit offset
    bbU8  mOpt;         //!< Option bits
    bbU8  mSubLine;     //!< Sub line number or flag in current logical line, 0 indicates start of logical line
    bbU8  mLevel;       //!< AA-Tree level, dtRefStore internal use
    bbU32 mColumn;      //!< Physical column number on current line, starting at 0
    bbU32 mEncState;    //!< Character encoding state, or other mode specific data
    bbS64 mLogLine;     //!< Logical line number
};

/** dtRefPt::opt flags. */
enum dtREFOPT
{
    dtREFOPT_ESTIMATED    = 0x10, //!< Buffer reference is estimated
    dtREFOPT_ESTLOGLINE   = 0x20, //!< Logical line number is estimated
    dtREFOPT_EOB          = 0x80, //!< End of buffer flag

    // text specific flags
    dtREFOPT_AREA0        = 0x0,  //!< Marks editing area 0 (meaning is e7Win implementation specific)
    dtREFOPT_AREA1        = 0x1,  //!< Marks editing area 1
    dtREFOPT_AREA2        = 0x2,  //!< Marks editing area 2
    dtREFOPT_AREA3        = 0x3,  //!< Marks editing area 3
    dtREFOPT_AREACOUNT    = 4,    //!< Maximum number of editing areas
    dtREFOPT_AREAMASK     = 0x3,  //!< Bitmask to mask editing area
};

struct dtRefNode
{
    bbU32   mLT; //!< index of node with offset less than this node, (bbU32)-1 is none
    bbU32   mGE; //!< index of node with offset greater or equal than this node, (bbU32)-1 is none
    dtRefPt ref; //!< Reference point
};

enum dtREFSKIP
{
    dtREFSKIP_OFFSET = 0,
    dtREFSKIP_LINE,
    dtREFSKIP_LOGLINE,
    dtREFSKIP_COLUMN
};

/** Client callback interface for dtRefStore. */
struct dtRefIf
{
    /** Skip through buffer line by line until terminating condition matched.
        @param pRef  [in]  Cache reference point to start scan at <br>
                     [out] Updated cache reference point
        @param dst   Physical line / offset / logical line to skip to
        @param mode  Terminating condition
                     <table>
                     <tr><td>#e7SKIPLINESMODE_OFFS</td><td>Stop when current offset matches \a dst</td></tr>
                     <tr><td>#e7SKIPLINESMODE_LINE</td><td>Stop when current physical line matches \a dst</td></tr>
                     <tr><td>#e7SKIPLINESMODE_LOGLINE</td><td>Stop when current logical line matches \a dst</td></tr>
                     </table>
    */
    virtual bbERR RefSkip(dtRefPt* const pRef, bbU64 const dst, dtREFSKIP const mode) = 0;

    /** Skip backwards through buffer to find start of physical line for estimated buffer reference.

        An implementation should start at the buffer offset in \a pRef and skip backwards to find
        the start of the the buffer offset's physical line. If the input reference point already
        points to linestart, mOffset should be returned as is.

        An implementation must not scan more than \a MaxScanSize bytes back, if a physical linestart
        cannot be found, bbENOTFOUND error should be returned.

        @param pRef [in]  Estimated Reference point to start scanning from.
                          dtRefPt::mOffset contains the buffer offset to start scanning from.
                          dtRefPt::mLine contains the estimated physical line number, whose start offset should be found.
                          dtRefPt::mBitoffs will be 0.
                          dtRefPt::mOpt will be initialized to dtREFOPT_ESTIMATED.

                    [out] dtRefPt::mOffset and dtRefPt::mBitoffs should be updated to point to physical linestart.
                          dtRefPt::mOpt should be updated with required flags.
                          dtRefPt::mLogline should be updated or estimated, if estimated dtREFOPT_ESTLOGLINE should be set.

        @param pKnownRef Last known reference point before the estimated reference point \a pRef.

        @param MaxScanSize Maximum number of bytes to scan backwards
    */
    virtual bbERR RefSyncLine(dtRefPt* const pRef, const dtRefPt* const pKnownRef, bbU32 const MaxScanSize) = 0;

    /** Get buffer size from client.
        @return Current buffer size
    */
    virtual bbU64 RefGetBufSize() = 0;

    /** Get logical line number start from client.
        @return Start line
    */
    virtual bbS64 RefGetLogLineStart() = 0;
};

/** Cache of buffer references for fast random access into variable line length buffes.

    <b>Note about reference points on buffer end</b>

    A reference point at the buffer end is defined as follows:
    - mOffset is equal to the buffer size
    - mLine is the last rendered physical line. If the last character fills the last logical column
      it will be an empty line. If the buffer ends with line return sequence, it will be an empty line.
    - mLogLine and mSubLine behave as for mLine
    - dtREFOPT_EOB is set
    - If dtREFOPT_ESTIMATED is set, mLine is estimated
*/
class dtRefStore
{
    dtRefNode*  mpNodes;        //!< Tree nodes, linear array
    bbU32       mTreeSize;      //!< Capacity of mpNodes[]
    bbU32       mNodeRoot;      //!< Index of tree root, -1 if empty
    bbU32       mNodeFree;      //!< Index of first free entry, 0 if empty
    bbU32       mMaxScanSize;   //!< Maximum bytes to scan, before reference points are estimated. Must be power of 2.
    dtRefIf*    mpIf;           //!< Associated interface

    /** Allocate a new node.
        The allocated node struct will be unitialized.
        This function invalidates any node pointers, because it may reallocate the mpNodes[] array.
        @return Index into mpNodes[], or (bbU32)-1 on error
    */
    bbU32 NewNode();

    inline dtRefNode* GetNode(bbU32 const idx) const
    {
        return mpNodes + idx;
    }

    inline bbU32 GetOptimumTreeSize() const
    {
        return 32;//xxx
    }

    bbU32 GetBytesPerLine(dtRefPt* const pRef);

public:
    dtRefStore();
    ~dtRefStore();

    void FindNodeFromOffset(bbU64 const offset, dtRefPt* const pRef);
    void FindNodeFromLine(bbU64 const line, dtRefPt* const pRef);

    inline void SetIf(dtRefIf* const pIf)
    {
        mpIf = pIf;
    }

    /** Set allowed size of data to scan, before reverting to estimate reference points.
        @param MaxScanSize Size of data to scan in bytes
    */
    inline void SetMaxScanSize(bbU32 const MaxScanSize)
    {
        bbASSERT((MaxScanSize & (MaxScanSize-1)) == 0);
        mMaxScanSize = MaxScanSize;
    }

    /** Clear all cached reference points. */
    void Clear();

    /** Test if cache is empty.
        @return true if empty
    */
    inline int IsEmpty() const
    {
        return mNodeRoot == (bbU32)-1;
    }

    /** Save reference point to tree.
        This call will save a new reference point and possibly delete
        old references to balance the tree size.

        If pRef->mOpt flag dtREFOPT_ESTLOGLINE or dtREFOPT_ESTIMATED is set,
        or pRef->mColumn is not 0, the call does nothing and returns bbEBADPARAM.

        @param pRef Reference point to save, will be copied
    */
    bbERR SaveRef(const dtRefPt* const pRef);

    /** Get reference point from buffer offset.

        If the given offset is on or beyound the buffer end, the reference
        point for the start of the line with the buffer end and bbEOK is returned.

        @param offset Buffer offset
        @param pRef   Returns reference point
    */
    bbERR Offset2Ref(bbU64 offset, dtRefPt* const pRef);

    /** Get reference point from physical line.

        If the given physical line is beyound the buffer end, i.e.
        larger than the last physical line, the function fails
        with bbEEOF error.

        @param line   Line number, starting at 0
        @param pRef   Returns reference point
    */
    bbERR Line2Ref(bbU64 const line, dtRefPt* const pRef);

    /** Update reference points for buffer change.
        @param pChange Buffer change descriptor
    */
    void BufferChange(const dtBufferChange* pChange);
};

#endif


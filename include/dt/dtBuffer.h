#ifndef dtBuffer_H_
#define dtBuffer_H_

/** @file
    Buffer interface.
*/

#include "dtdefs.h"
#include "babel/Arr.h"
#include "dtHistory.h"

enum dtBUFFERSTATE
{
    dtBUFFERSTATE_INIT = 0, //!< Buffer is constructed
    dtBUFFERSTATE_OPEN      //!< Buffer is opened and can be read and written
};

/** Change IDs for dtBufferNotify::OnMetaChange */
enum dtMETACHANGE
{
    dtMETACHANGE_STATE = 0,  //!< dtBuffer::mState was changed
    dtMETACHANGE_NAME,       //!< dtBuffer::mpName was changed
    dtMETACHANGE_MODIFIED,   //!< Buffer modified state was changed
    dtMETACHANGE_ISNEW,      //!< Buffer::IsNew() state was changed
    dtMETACHANGE_CANUNDO,    //!< Can undo/redo state was changed
};

/** dtBuffer notification interface. */
struct dtBufferNotify
{
    /** Buffer contents changed.
        The default implementation does nothing.
        <table>
        <tr><th>type</th><th>offset</th><th>length</th></tr>
        <tr><td>dtCHANGE_ALL     </td><td>ignore</td><td>ignore</td></tr>
        <tr><td>dtCHANGE_OVERWRITE</td><td>Buffer offset of overwrite start</td><td>Length of overwritten section in bytes</td></tr>
        <tr><td>dtCHANGE_INSERT   </td><td>Buffer offset of inserted data</td><td>Length of inserted data</td></tr>
        <tr><td>dtCHANGE_DELETE   </td><td>Buffer offset of deleted data</td><td>Length of deleted data, before delete</td></tr>
        </table>
        @param pBuf Pointer to buffer
        @param pChange Change parameters
    */
    virtual void OnBufferChange(dtBuffer* const pBuf, dtBufferChange* const pChange);

    /** Buffer meta-data changed.
        The default implementation does nothing.
        @param pBuf Pointer to buffer
        @param type Type of information that changed
    */
    virtual void OnBufferMetaChange(dtBuffer* const pBuf, dtMETACHANGE const type);
};

/** Flag bits for dtBuffer::mOpt. */
enum dtBUFFEROPT
{
    dtBUFFEROPT_READONLY      = 0x1U,   /**< Buffer is read-only. */
    dtBUFFEROPT_FIXED         = 0x2U,   /**< Buffer is fixed size. */
    dtBUFFEROPT_NOINSERT      = 0x4U,   /**< Buffer cannot insert. */
    dtBUFFEROPT_NEW           = 0x8U,   /**< Buffer is new file not on disk. */
    dtBUFFEROPT_MODIFIED      = 0x10U,  /**< Buffer is modified. */
    dtBUFFEROPT_CANUNDO       = 0x20U,  /**< Entry available in undo history. */
    dtBUFFEROPT_CANREDO       = 0x40U,  /**< Entry available in redo history. */
};

/** Save type for OnSave() parameter savetype. */
enum dtBUFFERSAVETYPE
{
    dtBUFFERSAVETYPE_NEW = 0,   //!< Buffer is new and saved first time
    dtBUFFERSAVETYPE_INPLACE,   //!< Buffer is saved back to original location
    dtBUFFERSAVETYPE_SAVEAS     //!< Buffer is saved to a new location, the old location should be preserved (save as)
};

enum dtSECTIONTYPE
{
    dtSECTIONTYPE_NONE = 0,
    dtSECTIONTYPE_MAP,
    dtSECTIONTYPE_MAPSEQ,
    dtSECTIONTYPE_INSERT
};

/** Descriptor for a mapped dtBuffer section.
    @see dtBuffer::Map, dtBuffer::MapSeq, dtBuffer::Insert
*/
struct dtSection
{
    bbU8*   mpData;         //!< Pointer to section of data
    bbU32   mSize;          //!< Size of section in bytes
    bbU8    mType;          //!< dtSECTIONTYPE
    bbU8    mIndex;         //!< Index of this struct in dtBuffer::mSections
    bbU8    mNextFree;      //!< Index of next free entry in dtBuffer::mSections
    bbU8    mOpt;           //!< dtBuffer implementation specifc option bits
    union {
    void*   mpContext;      //!< dtBuffer implementation specific context
    bbU32   mSegment;
    dtPage* mpPage;
    };
    bbU64   mOffset;        //!< Buffer offset of mapped section
};

/** Maximum number of concurrently mapable dtBuffer sections. */
#define dtBUFFER_MAXSECTIONS 9

enum dtMAP
{
    dtMAP_READONLY = 0, //!< Hint for dtBuffer::MapSeq: access will be read-only
    dtMAP_WRITE = 1     //!< Hint for dtBuffer::MapSeq: access will be read-write
};

/** Interface to file buffer.
    Maximum supported buffersize is 48 bit (256 TByte).
    This class wraps the following components
    - buffer
    - buffer name
    - undo history
*/
class dtBuffer
{
protected:
    bbU64               mBufSize;       //!< Current buffer size, implementations must keep this updated
    bbU8                mOpt;           //!< Flag bitmask, see dtBUFFEROPT
    bbU8                mSectionFree;   //!< Next free index in mSections[]
    bbU8                mState;         //!< Buffer state, see dtBUFFERSTATE
    bbU8                mUndoPoint;     //!< True if next change should be marked as undo point
    bbU8                mUndoActive;    //!< 1 if inside undo, 2 if inside redo call (internal use for preventing recursive history)
    bbU8                mUndoPointRec;  //!< Multi-part undo/redo reached undopoint, valid only if mUndoActive!=0
    bbU32               mSyncPtNoMod;   //!< Last sync point when buffer was not modified
    bbU32               mSyncPt;        //!< Circular ID of last change
    bbCHAR*             mpName;         //!< Name (filename, URL, etc), 0-terminated, managed heap block, NULL if closed
public:
    bbU32               mRefCt;         //!< Application defined reference count
protected:
    bbU32               mHistPos;       //!< Next read position in change history
    dtHistory           mHistory;       //!< Change history

    dtArrPBufferNotify  mNotifyHandlers;//!< Notification handler registry

    static bbUINT       mNewBufferCount;//!< Next new file number.

    /** Pool of buffer section map descriptors.
        Unused entries are organized in a single linked list,
        dtBuffer::mSectionFree -> dtSection->mNextFree -> ..
    */
    dtSection mSections[dtBUFFER_MAXSECTIONS];

    /** Allocate a mapped section descriptor.
        The function fails with error code bbEFULL, if dtBUFFER_MAXSECTIONS
        sections have already been mapped.
        @return Pointer to unitialized section descriptor, or NULL on failure.
    */
    dtSection* SectionAlloc();

    /** Free mapped section descriptor, previously allocated via SectionGet().
        @param pSec Pointer to section descriptor, can be NULL
    */
    inline void SectionFree(dtSection* const pSection)
    {
        if (pSection)
        {
            #ifdef bbDEBUG
            pSection->mType = dtSECTIONTYPE_NONE;
            #endif
            pSection->mNextFree = mSectionFree;
            mSectionFree = pSection->mIndex;
        }
    }

    void NotifyChange(dtCHANGE const type, bbU64 offset, bbU64 length, void* const user);
    void NotifyMetaChange(dtMETACHANGE const type);

    void UpdateCanUndoState();
    void ClearUndo();

    inline void SetState(dtBUFFERSTATE const state)
    {
        mState = (bbU8)state;
        NotifyMetaChange(dtMETACHANGE_STATE);
    }

    /** Attach heap block with new buffer name to dtBuffer::mpName.
        The old heap block will be freed.
        @param pName Heap block with 0-terminated string, object control is taken over.
    */
    void AttachName(bbCHAR* const pName);

public:
    dtBuffer();

    inline dtBUFFERSTATE GetState() const { return (dtBUFFERSTATE)mState; }

    /** Add buffer notification handler to this buffer instance.
        @param pNotify Notification interface
    */
    bbERR AddNotifyHandler(dtBufferNotify* const pNotify);

    /** Remove buffer notification handler from this buffer instance.
        @param pNotify Notification interface
    */
    void RemoveNotifyHandler(dtBufferNotify* const pNotify);

    /** Set undo point.
        This call registers the current buffer state as an undo/redo point.
    */
    inline void SetUndo()
    {
        mUndoPoint = 1;
    }

    /** Undo last change from history.
        Will set error bbEEND if no change available.
        @param user User context, will be forwarded to OnChange() callback
    */
    bbERR Undo(void* const user);

    /** Redo last change from history.
        Will set error bbEEND if no change available.
        @param user User context, will be forwarded to OnChange() callback
    */
    bbERR Redo(void* const user);

    inline bool CanUndo() const { return mHistory.CanUndo(); }
    inline bool CanRedo() const { return mHistory.CanRedo(); }

    /** Test if buffer is modified.
        @retval !=0 Modified
        @retval 0 Not modified
    */
    inline int IsModified() const { return mOpt & dtBUFFEROPT_MODIFIED; }

    /** Test if buffer is open.
        @retval !=0 Open
        @retval 0 Closed
    */
    inline int IsOpen() const { return mState!=dtBUFFERSTATE_INIT; }

    /** Test if buffer is new, i.e. was never saved to disk.
        @retval !=0 Modified
        @retval 0 Not modified
    */
    inline int IsNew() const { return mOpt & dtBUFFEROPT_NEW; }

    /** Open buffer for new or existing file.

        Parameter \a pPath is interpreted by the dtBuffer implementation, usually it points
        to a file. Passing NULL opens a new empty buffer.

        The buffer must be in state dtBUFFERSTATE_INIT to call this function.

        @param pPath 0-terminated path or NULL for new file.
    */
    bbERR Open(const bbCHAR* pPath);

    /** Close buffer.

        This function will close a buffer discarding any contents, free undo history, and
        reset the buffer name. The buffer will be reset to state dtBUFFERSTATE_INIT.

        If buffer is already in state dtBUFFERSTATE_INIT, the call has no effect.
    */
    void Close();

    /** Save buffer to datasource.

        If parameter \a pNewPath is NULL, the buffer will be saved to its original
        location, as designated by the buffer name. If it points to a path, the
        buffer contents is saved and reassociated to this location.
        If the buffer is new (dtBuffer::IsNew()) pNewPath must be specified.

        The buffer is new state will be cleared after this call.

        @param pNewPath Path to new save location (Save As), or NULL to save to original location.
    */
    bbERR Save(const bbCHAR* const pNewPath);

    /** Get buffer name (filename, URL, etc).
        @return 0-terminated name string, memory managed by buffer instance.
                Will be NULL if buffer is closed.
    */
    inline const bbCHAR* GetName() const { return mpName; }

    /** Get size of data in bytes.
        @return Size in bytes
    */
    inline bbU64 GetSize() const { return mBufSize; }

    /** Read buffer section to external memory block.

        If the section exceeds the buffer size, the valid part will be copied, and
        bbEEOF error is set. The function returns the number of bytes not copied.
        If this value is not 0, an error code will be set.

        This function is provided for convenience. For best performance
        buffer data should be read inplace using dtBuffer::Map() and dtBuffer::MapSeq().
        However if you do need to copy data, this function is usually faster
        than calling dtBuffer::Map() followed by copying the section externally.

        @param pDst   Pointer to target block, must be large enough to hold \a size bytes
        @param offset Buffer offset to start copying at
        @param size   Size in bytes
        @return Number of bytes not copied on failure, or 0 on success
    */
    bbU32 Read(bbU8* pDst, bbU64 offset, bbU32 size);

    bbERR Write(bbU64 offset, bbU8* pData, bbU32 size, int overwrite, void* user);

    //
    // - Interface
    //
protected:

    /** Normalize file path.
        The default implementation calls bbPathNorm().
        @param pPath 0-terminated path string
        @return Heap block contianing normalized path
    */
    virtual bbCHAR* PathNorm(const bbCHAR* const pPath);

    /** Open existing or new buffer.
        @param pPath Path or name of file to open
        @param isnew !=0 if creating a new file, \a pPath can be ignored
    */
    virtual bbERR OnOpen(const bbCHAR* const pPath, int isnew) = 0;

    /** Close buffer.
    */
    virtual void OnClose() = 0;

    /** Save buffer to datasource.
        @param pPath Path to save location.
        @param savetype Hint for save operation
    */
    virtual bbERR OnSave(const bbCHAR* pPath, dtBUFFERSAVETYPE savetype) = 0;

public:
    virtual ~dtBuffer();

    /** Delete a block of data from the buffer.
        If the delete area starts at or exceeds the buffer size, bbEBADPARAM is returned.
        @param offset Offset relative to buffer start to start deletion at.
        @param size Number of bytes to delete
        @param user User context, will be forwarded to OnChange() callback
        @return bbEOK on success, or error code on failure.
    */
    virtual bbERR Delete(bbU64 const offset, bbU64 size, void* const user) = 0;

    /** Inserts section at given position and open it for access.

        The inserted data is unitialized.
        The returned section contains the data in one run without gaps.
        If the insert offset lies beyound buffer end or \a size if 0,
        bbEBADPARAM error is returned.

        If the function returns successfull, dtBuffer::Discard
        or dtBuffer::Commit must be called later. In between no
        other call must be called that mofifies the buffer.

        @param offset Offset relative to buffer start to start data insertion at,
                      must not exceed the buffer size.
        @param size Size of data to insert in bytes, must not be 0.
        @return Pointer to memory, or NULL on failure (check #bbgErr).
                The memory block returned stays under control of the buffer instance.
    */
    virtual dtSection* Insert(bbU64 const offset, bbU32 const size) = 0;

    /** Map a section of the file to contineous memory.

        If the section to map starts on or exceeds buffer end, bbEBADPARAM is returned.

        The returned block contains the data in one run without gaps.

        If the function returns successfull, dtBuffer::Discard
        or dtBuffer::Commit must be called later. In between no
        other call must be called that mofifies the buffer.

        @param offset     Start offset from beginning of the file.
        @param size       Length of data to be mapped in bytes.
        @param accesshint Used as hint for optimizations such as preventing recording undo
        @return Pointer to memory, or NULL on failure.
    */
    virtual dtSection* Map(bbU64 offset, bbU32 size, dtMAP const accesshint) = 0;

    /** Sequentially map buffer portions to contineous memory.

        This function is intended for fast sequential walk through the
        buffer. On the first call pass the start offset. On each subsequent
        call add the returned section size dtSection::mSize to the last
        used offset to calculate the next offset.

        The size of the returned section is determined by the buffer
        implementation.

        If parameter \a minsize is larger than 0, the returned section size
        is guaranted to hold at least the given amount of bytes, unless
        the buffer end is exceeded, in which case only the number of bytes
        left are returned. The minimum size feature is usefull, if
        a buffer operation is to be done on access units larger than 1 byte.

        If \a offset is equal or larger than the buffer size, NULL is returned and
        bbEEOF error is set.

        If the function returns a valid pointer, dtBuffer::Discard()
        or dtBuffer::Commit() must be called later. If \a access was passed
        as dtMAP_READONLY, Discard() must be called. If \a access was passed
        as dtMAP_WRITE, Commit() must be called later. In between no
        other call must be called that modifies the buffer. If the returned
        section is to be discarded later, the contained data must not be
        modified.

        @param offset     Start offset of section from beginning of the file.
        @param minsize    Minimum requested size of buffer section, 0 for no restriction
        @param accesshint Used as hint for optimizations such as preventing recording undo
        @return Pointer to section descriptor, -1 if end of buffer reached, or NULL on error
    */
    virtual dtSection* MapSeq(bbU64 const offset, bbUINT minsize, dtMAP const accesshint) = 0;

    /** Commit a section mapped by dtBuffer::Map or dtBuffer::MapSeq committing changes.

        The buffer will be marked dirty in the range of the section. If the
        buffer implementation has caching functionality, the section will be
        written through to the base level buffer space.

        The section referenced by the handle \a ptr will be released
        regardless of whether the commit was successfull or not.

        @param pSection Pointer to section as returned by dtBuffer::Map.
        @param user     User context, will be forwarded to OnChange() callback
    */
    virtual bbERR Commit(dtSection* const pSection, void* const user) = 0;

    /** Discard a section mapped by dtBuffer::Map or dtBuffer::MapSeq without changes.
        @param pSection Pointer to section as returned by dtBuffer::Map. Can be NULL.
    */
    virtual void Discard(dtSection* const pSection) = 0;
};

#endif /* dtBuffer_H_ */


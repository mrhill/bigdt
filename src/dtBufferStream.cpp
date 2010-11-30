#include "dtBufferStream.h"
#include <babel/str.h>
#include <babel/file.h>
#include <babel/log.h>

enum
{
    dtSECTIONOPT_MAP_SEQ = 0,
    dtSECTIONOPT_MAP_PAGE = 1,
    dtSECTIONOPT_MAP_TYPEMASK = 1,
    dtSECTIONOPT_MAP_WRITE = 4,
    dtSECTIONOPT_INSERT_ENLARGE = 0,
    dtSECTIONOPT_INSERT_CREATE = 1,
    dtSECTIONOPT_INSERT_REPLACE = 2
};

#ifdef bbDEBUG
static int            gCheckTreeDisable = 0;
static bbU64*         gpOffsets = NULL;
static bbU64*         gpOffsetStack = NULL;
static bbU32          gOffsetSize = 0;
static dtSegment*     gpSavedSegments = NULL;
static bbU32          gSavedSegmentSize = 0;
static dtBufferStream       gSavedClass;
static dtBufferStream::dtOP gSavedOp;
static bbU64          gSavedOffset, gSavedSize;

static struct DestructGuard { ~DestructGuard() {
        bbMemClear(&gSavedClass, sizeof(gSavedClass));
}} gDestructGuard;
#endif

bbCHAR* dtBufferStream::spTempDir = NULL;

dtBufferStream::dtBufferStream()
{
    bbASSERT(sizeof(dtSegment) <= mSegments.GetElementSize());

    mSegmentLastMapped = (bbU32)-1;
    bbMemClear(mPagePool, sizeof(mPagePool));

    mhFile = mhTempFile = NULL;

    #ifdef bbDEBUG
    mHitCount=
    mMissCount=0;
    #endif
}

dtBufferStream::~dtBufferStream()
{
    ClearSegments();

#ifdef bbDEBUG
    bbMemFreeNull((void**)&gpSavedSegments);
    bbMemFreeNull((void**)&gpOffsets);
    bbMemFreeNull((void**)&gpOffsetStack);
    gSavedSegmentSize = 0;
    gOffsetSize = 0;
#endif
}

bbERR dtBufferStream::OnOpen(const bbCHAR* const pPath, int isnew)
{
    bbU32 idx;
    dtSegment* pSeg;
    
    bbASSERT(mSegments.GetSize() == 0);

    if (isnew)
    {
        mBufSize = 0;
        bbASSERT(mhFile == NULL);
    }
    else
    {
        if ((mhFile = bbFileOpen(pPath, bbFILEOPEN_READ)) == NULL)
            goto dtBuffer_file_Open_err;

        mBufSize = bbFileExt(mhFile);
        if (mBufSize == (bbU64)-1)
            goto dtBuffer_file_Open_err;
    }

    //
    // Init segment index
    //
    if ((idx = NewSegment()) == (bbU32)-1)
        goto dtBuffer_file_Open_err;

    pSeg = mSegments.GetPtr(idx);
    bbMemClear(pSeg, sizeof(*pSeg));
    pSeg->mType       = dtSEGMENTTYPE_NULL;
    pSeg->mFileSize   = mBufSize;
    pSeg->mLT         =
    pSeg->mGE         = (bbU32)-1;

    mSegmentLastMapped = (bbU32)-1;

    //
    // Init page index
    //
    for (idx = 0; idx<dtBUFFERSTREAM_MAXPAGES; idx++)
    {
        bbASSERT(!mPagePool[idx].mpData);
        mPagePool[idx].mSize = 0;
        mPagePool[idx].mIndex = (bbU8)idx;
        mPagePool[idx].mNextFree = (bbU8)(idx + 1);
    }
    mPageFree = 0;

    return bbEOK;

    dtBuffer_file_Open_err:
    OnClose();
    return bbELAST;
}

void dtBufferStream::OnClose()
{
    for (bbUINT idx = 0; idx<dtBUFFERSTREAM_MAXPAGES; idx++)
    {
        bbMemFreeNull((void**)&mPagePool[idx].mpData);
    }

    ClearSegments();

    bbFileClose(mhFile);
    mhFile = NULL;
}

bbERR dtBufferStream::OnSave(const bbCHAR* pPath, dtBUFFERSAVETYPE const savetype)
{
    bbFILEH hFile = NULL;
    bbCHAR* pTmpName = NULL;
    bbU8*   pCopyBuf = NULL;
    bbU32   copysize = dtBUFFERSTREAM_SEGMENTSIZE;
    bbU32   idx;

    //
    // Create tempfile in same directory as pPath and allocate a copy buffer
    //
    if (savetype == dtBUFFERSAVETYPE_INPLACE)
    {
        bbCHAR* pDir;
        if (bbPathSplit(pPath, &pDir, NULL, NULL) != bbEOK)
            return bbELAST;
        pTmpName = bbPathTemp(pDir);
        bbMemFree(pDir);
        if (!pTmpName)
            return bbELAST;
    }

    if (savetype != dtBUFFERSAVETYPE_NEW)
    {
        for(;;)
        {
            pCopyBuf = (bbU8*)bbMemAlloc(copysize);
            if (!pCopyBuf)
            {
                copysize = copysize >> 1;
                if (copysize >= 4096)
                    continue;
                goto err;
            }
            break;
        }
    }

    //
    // Get save file handle
    //
    if ((hFile = bbFileOpen( (savetype==dtBUFFERSAVETYPE_INPLACE) ? pTmpName : pPath, bbFILEOPEN_READWRITE|bbFILEOPEN_TRUNC)) == NULL)
        goto err;

    //
    // Save
    //
    idx = mSegmentUsedFirst;
    do
    {
        bbASSERT(idx <= mSegments.GetSize());

        dtSegment* pSegment = mSegments.GetPtr(idx);

        if (pSegment->mType == dtSEGMENTTYPE_NULL)
        {
            if (bbU64 size = pSegment->mFileSize)
            {
                if (bbFileSeek(mhFile, pSegment->mFileOffset, bbFILESEEK_SET) != bbEOK)
                    goto err;

                while(size)
                {
                    bbU32 tocopy = size > copysize ? copysize : (bbU32)size;
                    size -= tocopy;
                    if ((bbFileRead(mhFile, pCopyBuf, tocopy) != bbEOK) ||
                        (bbFileWrite(hFile, pCopyBuf, tocopy) != bbEOK))
                    {
                        goto err;
                    }
                }
            }
        }
        else
        {
            if (bbFileWrite(hFile, pSegment->mpData, pSegment->mSize) != bbEOK)
                goto err;
        }

        idx = pSegment->mNext;

    } while (idx != mSegmentUsedFirst);

    bbFileClose(hFile);
    hFile = NULL;
    bbFileClose(mhFile);
    mhFile = NULL;

    if (savetype == dtBUFFERSAVETYPE_INPLACE)
    {
        if ((bbEOK != bbFileDelete(mpName)) ||
            (bbEOK != bbFileRename(pTmpName, pPath)))
        {
            bbLog(bbErr, bbT("Save error, cannot rename %s to %s"), pTmpName, pPath);
            mhFile = bbFileOpen(pPath, bbFILEOPEN_READ); // try to recover
            goto err;
        }

        bbMemFree(pCopyBuf);
        bbMemFree(pTmpName);
    }

    if ((mhFile = bbFileOpen(pPath, bbFILEOPEN_READ)) == NULL)
        goto err;

    return bbEOK;

    err:
    bbFileClose(hFile);
    if (pTmpName)
    {
        bbFileDelete(pTmpName);
        bbMemFree(pTmpName);
    }
    bbMemFree(pCopyBuf);
    return bbELAST;
}

void dtBufferStream::ClearSegments()
{
    mSegmentLastMapped = (bbU32)-1;

    if (mSegments.GetSize())
    {
        bbU32 walk = mSegmentUsedLast;
        do
        {
            dtSegment* const pWalk = mSegments.GetPtr(walk);

            if (pWalk->mType == dtSEGMENTTYPE_MAP)
                bbMemFree(pWalk->mpData);

            walk = pWalk->mPrev;

        } while (walk != mSegmentUsedLast);
    }

    dtSegmentTree::ClearSegments();
}

bbERR dtBufferStream::Delete(bbU64 const offset, bbU64 size, void* const user)
{
    if ((offset + size) >= mBufSize)
    {
        if (((offset + size) > mBufSize) || (offset == mBufSize))
            return bbErrSet(bbEBADPARAM);
    }

    if (!size)
        return bbEOK;

    #ifdef bbDEBUG
    SaveTree(DEL, offset, size);
    #endif

    bbU64 const size_org = size;

    bbU8* pUndo = NULL;
    if (!mUndoActive)
    {
        if ((pUndo = mHistory.Push(dtCHANGE_DELETE, offset, size, mUndoPoint!=0)) == NULL)
            return bbELAST;
        mUndoPoint = 0;
        UpdateCanUndoState();
    }

    mSegmentLastMapped = (bbU32)-1;

    bbU64 segmentstart, segmentoffset;
    bbU32 del = (bbU32)-1;
    bbU32 idx = FindSegment(offset, &segmentstart, 0);
    dtSegment* pSegment = mSegments.GetPtr(idx);

    //
    // Align pSegment start to delete offset
    //
    if ((segmentoffset = offset - segmentstart) != 0)
    {
        bbASSERT(segmentoffset < pSegment->GetSize());

        if (pSegment->mType == dtSEGMENTTYPE_NULL)
        {
            //
            //        |-Del--... ->           |-Del--...
            // |-Null-------...       |-Null--|-Null--...
            //

            // Insert a new Null segment

            idx = SplitNullSegment(idx, segmentoffset); // invalidates any dtSegment*
            if (idx == (bbU32)-1)
            {
                if (pUndo)
                    mHistory.PushRevert();
                return bbELAST;
            }
        }
        else
        {
            bbASSERT(segmentoffset <= 0xFFFFFFFFUL);

            if (size <= (pSegment->mSize - (bbU32)segmentoffset))
            {
                // Shortcut: if segment is Map, and delete area is within the segment,
                //           just reduce its size and return
                //
                //       |-Del-|                    |-Del-|
                //       |     |          ->        |/    
                // |-Map----------|-X--...    |-Map-|--|-X--...   
                //

                bbU32 const delend = (bbU32)segmentoffset + (bbU32)size;
                bbASSERT(delend <= pSegment->mSize);
                if (pUndo)
                    bbMemMove(pUndo, pSegment->mpData + (bbU32)segmentoffset, (bbU32)size);
                bbMemMove(pSegment->mpData + (bbU32)segmentoffset,
                          pSegment->mpData + delend,
                          pSegment->mSize - delend);
                bbMemRealloc(pSegment->mSize -= (bbU32)size, (void**)&pSegment->mpData);

                NodeSubstractOffset(idx, segmentstart, size); // adjust relative offsets in index tree
                #ifdef bbDEBUG
                CheckTree();
                #endif

                pSegment->mChanged = 1;

                bbASSERT(size <= mBufSize);
                mBufSize -= (bbU32)size;

                NotifyChange(dtCHANGE_DELETE, offset, size, user);
                return bbEOK;
            }

            //      ->|ovl|<-               ->|ovl|<-        ovl is overlapping area in bytes
            //        |-Del--...  ->          |   |-Del--... size -= ovl
            // |-Map------|-X--...     |-Map--|   |-X--...   Map->mSize -= ovl
            //

            if ((del = NewSegment()) == (bbU32)-1) // allocate empty segment header, beyound this point the buffer
            {
                if (pUndo)
                    mHistory.PushRevert();
                return bbELAST;                    // gets modified, so this will be the last call that may fail
            }
            pSegment = mSegments.GetPtr(idx); // memory may have moved

            bbASSERT(segmentoffset < pSegment->mSize);
            bbU32 const ovl = pSegment->mSize - (bbU32)segmentoffset;

            if (pUndo)
            {
                bbMemMove(pUndo, pSegment->mpData + (bbU32)segmentoffset, ovl);
                pUndo += ovl;
            }

            bbMemRealloc(pSegment->mSize = (bbU32)segmentoffset, (void**)&pSegment->mpData);

            NodeSubstractOffset(idx, segmentstart, ovl); // adjust relative offsets in index tree
            #ifdef bbDEBUG
            CheckTree();
            #endif

            pSegment->mChanged = 1;

            bbASSERT(ovl < size);
            size -= ovl;
            mBufSize -= ovl;

            idx = pSegment->mNext;
        }
    }

    if (del == (bbU32)-1)
    {
        // delete segment header hasn't been alloced yet
        if ((del = NewSegment()) == (bbU32)-1)
        {
            if (pUndo)
                mHistory.PushRevert();
            return bbELAST;
        }
    }

    #ifdef bbDEBUG
    CheckTree();
    gCheckTreeDisable = 1;
    #endif

    // - at this point the delete offset lies on a segment boundary

    pSegment = mSegments.GetPtr(idx);
    bbU32 const prev = pSegment->mPrev;

    //
    // prev |del           |
    //      |idx|idx|idx|tail|
    //

    bbASSERT(mBufSize >= size);
    mBufSize -= size;

    //
    // Eat all segments which are fully covered by the delete area
    //
    bbU64 delfilesize = 0;
    for(;;)
    {
        bbU64 segmentsize = pSegment->GetSize();

        if (size < segmentsize)
            break;

        size -= segmentsize;
        delfilesize += pSegment->mFileSize;

        if (pSegment->mType == dtSEGMENTTYPE_MAP)
        {
            if (pUndo)
            {
                bbMemMove(pUndo, pSegment->mpData, (bbU32)segmentsize);
                pUndo += (bbU32)segmentsize;
            }
            bbMemFree(pSegment->mpData);
        }
        else
        {
            if (pUndo)
            {
                bbFileSeek(mhFile, pSegment->mFileOffset, bbFILESEEK_SET);//xxx
                bbFileRead(mhFile, pUndo, (bbU32)segmentsize);//xxx
                pUndo += (bbU32)segmentsize;
            }
        }

        // unlink node from tree
        NodeDelete(idx, offset);

        bbU32 const tmp = idx;
        idx = pSegment->mNext;

        // return segment to free pool
        pSegment->mNext = mSegmentFree;
        mSegmentFree = tmp;

        pSegment = mSegments.GetPtr(idx);
    }

    if ((pSegment->mType == dtSEGMENTTYPE_NULL) || ((idx == mSegmentUsedFirst) && (size==0)))
    {
        //
        // If delete eat loop ended on a Null segment, or at buffer end,
        // we need to insert a new 0-size Map segment
        //
        bbASSERT(pSegment->GetSize() >= size);

        if (size)
        {
            //
            //       |-D-------|
            // |-----|---|---|-N-| -> |-----|M|N-|
            //
            bbASSERT((idx != mSegmentUsedFirst) || (mSegmentUsedFirst == mSegmentUsedLast));

            if (pUndo)
            {
                bbFileSeek(mhFile, pSegment->mFileOffset, bbFILESEEK_SET);//xxx
                bbFileRead(mhFile, pUndo, (bbU32)size);//xxx
                pUndo += (bbU32)size;
            }

            pSegment->mFileOffset += size;
            pSegment->mFileSize -= size;
            NodeSubstractOffset(idx, offset, size);
        }
        // else
        //
        //       |-D-----|
        // |-----|---|---|-N-| -> |-----|M|-N-|
        //
        //       |-D-----|
        // |-N---|---|---| -> |-N---|M|
        //
        //       |-D-----|
        // |-M---|---|---| -> |-M---|M|
        //

        dtSegment* const pSegmentDel = mSegments.GetPtr(del);
        pSegmentDel->mType = dtSEGMENTTYPE_MAP;
        pSegmentDel->mChanged = 1;
        pSegmentDel->mpData = NULL;
        pSegmentDel->mSize = 0;
        pSegmentDel->mFileSize = delfilesize + size;

        if (mSegmentUsedLast == prev) // special case: del is at buffer start
        {
            if (idx == mSegmentUsedFirst) // did delete eat loop end with buffer wrap?
            {
                bbASSERT(size == 0);
                mSegmentUsedLast = del;
            }
            else
            {
                pSegmentDel->mNext = idx;
                pSegment->mPrev = del;
            }

            mSegmentUsedFirst = del;

            pSegmentDel->mPrev = mSegmentUsedLast;
            mSegments[mSegmentUsedLast].mNext = del;

            // set new tree root
            pSegmentDel->mLT = (bbU32)-1;
            pSegmentDel->mGE = mSegmentUsedRoot;
            pSegmentDel->mOffset = 0;
            mSegmentUsedRoot = del;
        }
        else
        {
            if (idx == mSegmentUsedFirst) // did delete eat loop end with buffer wrap?
                mSegmentUsedLast = del;

            pSegmentDel->mNext = idx;
            pSegment->mPrev = del;

            pSegmentDel->mPrev = prev;
            mSegments[prev].mNext = del;

            NodeLinkRight(del, mSegments.GetPtr(prev), mSegments[prev].GetSize());
        }
    }
    else
    {
        bbASSERT(pSegment->mSize >= size);

        //
        //       |-D-------|
        // |-----|---|---|-M-| -> |-----|M-|
        //
        //
        //       |-D-----|
        // |-----|---|---|-M-| -> |-----|-M-|
        //

        if (size)
        {
            if (pUndo)
            {
                bbMemMove(pUndo, pSegment->mpData, (bbU32)size);
                pUndo += (bbU32)size;
            }

            bbMemMove(pSegment->mpData, pSegment->mpData + size, pSegment->mSize -= (bbU32)size);
            bbMemRealloc(pSegment->mSize, (void**)&pSegment->mpData);
        }

        pSegment->mFileSize += delfilesize;
        pSegment->mChanged = 1;

        pSegment->mPrev = prev;
        mSegments[prev].mNext = idx;
        
        if (mSegmentUsedLast == prev) // deleting from buffer start
            mSegmentUsedFirst = idx;
        
        if (size)
            NodeSubstractOffset(idx, offset, size); // adjust relative offsets in index tree
        
        // return del segment to free pool
        mSegments[del].mNext = mSegmentFree;
        mSegmentFree = del;
    }

    #ifdef bbDEBUG
    gCheckTreeDisable = 0;
    CheckTree();
    #endif

    NotifyChange(dtCHANGE_DELETE, offset, size_org, user);

    return bbEOK;
}

dtSection* dtBufferStream::Insert(bbU64 const offset, bbU32 const size)
{
    if ((offset > mBufSize) || (size == 0))
    {
        bbErrSet(bbEBADPARAM);
        return NULL;
    }

    mSegmentLastMapped = (bbU32)-1;

    dtSection* const pSection = SectionAlloc();
    if (!pSection)
        return NULL;

    pSection->mType   = dtSECTIONTYPE_INSERT;
    pSection->mOffset = offset;
    pSection->mSize   = size;

    bbU64 segmentstart;
    bbU32 insert, prev, idx = FindSegment(offset, &segmentstart, 0);
    bbU64 const segmentoffset = offset - segmentstart;
    dtSegment* pSegment;

    if (segmentoffset) // insert into middle of a segment -> split into two
    {
        if (mSegments[idx].mType == dtSEGMENTTYPE_NULL)
        {
            if ((idx = SplitNullSegment(idx, segmentoffset)) == (bbU32)-1)
                goto dtBufferStream_Insert_err;
        }
        else
        {
            bbASSERT(segmentoffset <= 0xFFFFFFFFUL);

            // xxx Idea for another shortcut: To prevent split fragmentation here, realloc
            // the existing Map segment, and temporary write data to end of block. In Commit
            // move the data to the right spot. Alternatively introduce gap buffers on each
            // segment as in !Zap

            if ((idx = SplitMapSegment(idx, (bbU32)segmentoffset)) == (bbU32)-1)
                goto dtBufferStream_Insert_err;
        }
    }

    //
    // At this point, we insert at a segment boundary
    // idx points to the right segment (first for insert at buffer start or end)
    //

    prev = mSegments[idx].mPrev;
    pSegment = mSegments.GetPtr(prev);

    //
    // 'Replace' case: right segment is size 0 -> replace it
    // This case exists to eliminate 0-size segments whenever possible
    // - also covers the empty buffer case (right is same as left segment)
    // - if left segment is 0 and type Map, it will be covered by the 'Enlarge' case
    // - if left segment is 0 and type Null, we have a bug
    //
    if ((mSegments[idx].GetSize() == 0) && ((idx != mSegmentUsedFirst) || (idx == prev)))
    {
        bbASSERT((idx != prev) || (offset == 0)); // if left=right, we should be inserting at buffer start
        bbASSERT((mSegments[idx].mType == dtSEGMENTTYPE_NULL) || (mSegments[idx].mpData == NULL)) // if Map, then pData should be NULL

        if ((pSection->mpData = (bbU8*)bbMemAlloc(size)) == NULL)
            goto dtBufferStream_Insert_err;

        pSection->mSegment = idx;
        pSection->mOpt     = dtSECTIONOPT_INSERT_REPLACE;
        return pSection;
    }

    bbASSERT((pSegment->mType==dtSEGMENTTYPE_MAP) || (pSegment->mFileSize != 0)); // at this point we must not meet 0-sized Null segments

    //
    // 'Enlarge' case: If previous section is mapped and smaller dtBUFFERSTREAM_SEGMENTSIZE,
    // append insert at previous segment.
    //

    if ((pSegment->mType == dtSEGMENTTYPE_MAP) &&
        (pSegment->mSize < dtBUFFERSTREAM_SEGMENTSIZE) &&
        (offset || !mBufSize)) // prevent shortcut at buffer start, unless buffersize is 0
    {
        if (bbEOK != bbMemRealloc(pSegment->mSize + size, (void**)&pSegment->mpData))
            goto dtBufferStream_Insert_err;

        pSection->mpData   = pSegment->mpData + pSegment->mSize;
        pSection->mSegment = prev;
        pSection->mOpt     = dtSECTIONOPT_INSERT_ENLARGE; // mark that no new node was created in mSegments[]
        return pSection;
    }

    //
    // 'Create' case: new segment and link in between the two existing segments
    //

    if ((insert = NewSegment()) == (bbU32)-1)
        goto dtBufferStream_Insert_err;

    pSegment = mSegments.GetPtr(insert);

    if ((pSegment->mpData = (bbU8*)bbMemAlloc(size)) == NULL)
    {
        UndoSegment(insert);
        goto dtBufferStream_Insert_err;
    }

    pSegment->mType     = dtSEGMENTTYPE_MAP;
    pSegment->mFileSize = 0;
    pSegment->mSize     = 0; // set later in Commit, but set to 0 so bbMemRealloc() in Discard() will free
    pSegment->mChanged  = 1;

    pSection->mpData    = pSegment->mpData;
    pSection->mSegment  = insert;
    pSection->mOpt      = dtSECTIONOPT_INSERT_CREATE; // mark that new node was created in mSegments[]

    // The new segment will finally be linked in Commit(), or discarded in Discard()
    pSegment->mNext = idx;
    pSegment->mPrev = prev;

    return pSection;

    dtBufferStream_Insert_err:
    SectionFree(pSection);
    return NULL;
}

dtPage* dtBufferStream::PageAlloc(bbU32 const size)
{
    bbUINT const i = mPageFree;

    if (i >= dtBUFFERSTREAM_MAXPAGES)
    {
        bbErrSet(bbEFULL);
        return NULL;
    }

    dtPage* const pPage = mPagePool + i;

    if (size > pPage->mSize)
    {
        if (bbEOK != bbMemRealloc(size, (void**)&pPage->mpData))
            return NULL;
        pPage->mSize = size;
    }

    mPageFree = mPagePool->mNextFree;

    return pPage;
}

dtSection* dtBufferStream::Map(bbU64 offset, bbU32 size, dtMAP const accesshint)
{
    if ((offset + size) >= mBufSize)
    {
        if (((offset + size) > mBufSize) || (offset == mBufSize))
        {
            bbErrSet(bbEBADPARAM);
            return NULL;
        }
    }

    //
    // Shortcut: Check if complete map lies within a segment
    //
    dtSection* pMap = MapSeq(offset, 0, dtMAP_READONLY);
    if (!pMap)
    {
        bbASSERT(bbErrGet() != bbEEOF);
        return NULL;
    }

    if (pMap && (size <= pMap->mSize))
    {
        pMap->mSize = size;
        pMap->mType = dtSECTIONTYPE_MAP;
        pMap->mOpt  = dtSECTIONOPT_MAP_SEQ;

        bbASSERT((accesshint == dtMAP_READONLY) || (pMap->mOpt |= dtSECTIONOPT_MAP_WRITE));

        //
        // Undo
        //
        if ((accesshint != dtMAP_READONLY) && (!mUndoActive))
        {
            bbU8* const pUndo = mHistory.Push(dtCHANGE_OVERWRITE, offset, size, mUndoPoint!=0);
            if (!pUndo)
            {
                Discard(pMap);
                return NULL;
            }
            bbMemMove(pUndo, pMap->mpData, size);
            mUndoPoint = 0;
            UpdateCanUndoState();
        }

        return pMap;
    }

    bbU8* pDst;

    //
    // Map area is split accross multiple segments, collect them into one dtPage
    //
    dtSection* const pSection = SectionAlloc();
    dtPage* const pPage = PageAlloc(size);

    if (!pSection || !pPage)
        goto dtBufferStream_Map_err;

    pSection->mType  = dtSECTIONTYPE_MAP;
    pSection->mOpt   = dtSECTIONOPT_MAP_PAGE;
    pSection->mpData = pPage->mpData;
    pSection->mOffset = offset;
    pSection->mSize  = size;
    pSection->mpPage = pPage;

    pDst = pPage->mpData;
    for(;;)
    {
        bbU32 tocopy = pMap->mSize;
        if (tocopy > size)
            tocopy = size;

        bbMemMove(pDst, pMap->mpData, tocopy);
        Discard(pMap);
        pDst += tocopy;
        size -= tocopy;
        offset += tocopy;

        if (size == 0)
            break;
        
        if ((pMap = MapSeq(offset, 0, dtMAP_READONLY)) == NULL)
        {
            bbASSERT(bbErrGet() != bbEEOF);
            goto dtBufferStream_Map_err;
        }
    }

    bbASSERT((accesshint == dtMAP_READONLY) || (pSection->mOpt |= dtSECTIONOPT_MAP_WRITE));
    return pSection;

    dtBufferStream_Map_err:
    PageFree(pPage);
    SectionFree(pSection);
    Discard(pMap);
    return NULL;
}

dtSection* dtBufferStream::MapSeq(bbU64 const offset, bbUINT minsize, dtMAP const accesshint)
{
    if ((offset + minsize) >= mBufSize)
    {
        if (offset >= mBufSize)
        {
            bbErrSet(bbEEOF);
            return NULL;
        }

        minsize = (bbUINT)mBufSize - (bbUINT)offset;
    }

    bbU64 segmentstart;
    bbU32 idx;
    dtSegment* pSegment;

    dtSection* const pSection = SectionAlloc();
    if (!pSection)
        return NULL;

    //
    // - Shortcut: caches repeated access to the same segment
    //
    idx = mSegmentLastMapped;
    if (idx != (bbU32)-1)
    {
        segmentstart = mSegmentLastOffset;
        pSegment = mSegments.GetPtr(idx);
        if ((offset - segmentstart) < pSegment->mSize)
        {
            goto dtBufferStream_MapSeq_usecached;
        }

        mSegmentLastMapped = (bbU32)-1;
    }

    idx = FindSegment(offset, &segmentstart, 0);
    pSegment = mSegments.GetPtr(idx);

    if (pSegment->mType == dtSEGMENTTYPE_NULL)
    {
        if (pSegment->mFileSize > dtBUFFERSTREAM_SEGMENTSIZE)
        {
            bbU64 splitoffset = (offset - segmentstart) &~ (bbU64)(dtBUFFERSTREAM_SEGMENTSIZE-1);

            if (splitoffset) // don't split at segment start
            {
                segmentstart += splitoffset;

                bbU32 right = SplitNullSegment(idx, splitoffset);
                if (right == (bbU32)-1)
                    goto dtBufferStream_MapSeq_err;

                idx = right;
                pSegment = mSegments.GetPtr(idx);
            }

            if (pSegment->mFileSize > dtBUFFERSTREAM_SEGMENTSIZE)
            {
                if (SplitNullSegment(idx, dtBUFFERSTREAM_SEGMENTSIZE) == (bbU32)-1)
                    goto dtBufferStream_MapSeq_err;
                pSegment = mSegments.GetPtr(idx);
            }
        }

        bbU8* const pData = (bbU8*)bbMemAlloc((bbU32)pSegment->mFileSize);
        if (pData == NULL)
            goto dtBufferStream_MapSeq_err;

        if ((bbFileSeek(mhFile, pSegment->mFileOffset, bbFILESEEK_SET) != bbEOK) ||
            (bbFileRead(mhFile, pData, (bbU32)pSegment->mFileSize) != bbEOK))
        {
            bbMemFree(pData);
            goto dtBufferStream_MapSeq_err;
        }

        pSegment->mType    = dtSEGMENTTYPE_MAP;
        pSegment->mSize    = (bbU32)pSegment->mFileSize;
        pSegment->mpData   = pData;
        pSegment->mChanged = 0;
    }

    mSegmentLastMapped = idx; // cache
    mSegmentLastOffset = segmentstart;
    dtBufferStream_MapSeq_usecached:

    //
    // Check if segment size satisfies minsize requirement
    //
    if (pSegment->mSize < minsize)
    {
        SectionFree(pSection); // could catch this condition earlier to avoid pSection alloc
        return Map(offset, minsize, accesshint);
    }

    pSection->mSegment = idx;

    idx = (bbU32)offset - (bbU32)segmentstart;
    bbASSERT(pSegment->mSize > idx);

    pSection->mpData  = pSegment->mpData + idx;
    pSection->mOffset = offset;
    pSection->mSize   = pSegment->mSize - idx;
    pSection->mType   = dtSECTIONTYPE_MAPSEQ;
    #ifdef bbDEBUG
    pSection->mOpt    = (bbU8)accesshint;
    #endif

    //
    // Undo
    //
    if ((accesshint != dtMAP_READONLY) && (!mUndoActive))
    {
        bbU8* const pUndo = mHistory.Push(dtCHANGE_OVERWRITE, offset, pSection->mSize, mUndoPoint!=0);
        if (!pUndo)
            goto dtBufferStream_MapSeq_err;
        bbMemMove(pUndo, pSection->mpData, pSection->mSize);
        mUndoPoint = 0;
        UpdateCanUndoState();
    }

    return pSection;

    dtBufferStream_MapSeq_err:
    SectionFree(pSection);
    return NULL;
}

bbERR dtBufferStream::Commit(dtSection* const pSection, void* const user)
{
    bbERR      err;
    dtSegment* pSegment;
    bbU8*      pUndo;

    switch (pSection->mType)
    {
    case dtSECTIONTYPE_INSERT:
        if (!mUndoActive)
        {
            pUndo = mHistory.Push(dtCHANGE_INSERT, pSection->mOffset, pSection->mSize, mUndoPoint!=0);
            if (!pUndo)
            {
                err = bbELAST;
                goto dtBufferStream_Commit_err;
            }
            mUndoPoint = 0;
            UpdateCanUndoState();
        }

        pSegment = mSegments.GetPtr(pSection->mSegment);

        bbASSERT(mSegmentLastMapped == (bbU32)-1);

        switch (pSection->mOpt)
        {
        case dtSECTIONOPT_INSERT_ENLARGE:
        {
            bbASSERT(pSegment->mType == dtSEGMENTTYPE_MAP);

            // adjust relative offsets in tree
            bbU64 const segmentstart = pSection->mOffset - pSegment->mSize;
            pSegment->mSize += pSection->mSize;
            NodeSubstractOffset(pSection->mSegment, segmentstart, -(bbS64)pSection->mSize);
            break;
        }

        case dtSECTIONOPT_INSERT_CREATE:
        {
            bbASSERT(pSegment->mType == dtSEGMENTTYPE_MAP);

            //
            // Complete linking into mSegments[] list
            //
            bbU32 const insert = pSection->mSegment;
            bbU32 const next = pSegment->mNext;
            bbU32 const prev = pSegment->mPrev;
            mSegments[prev].mNext = insert;
            mSegments[next].mPrev = insert;

            //
            // update first/last pointers for linked list
            //
            if (next == mSegmentUsedFirst) // new segment created at buffer start or end?
            {
                bbASSERT(mSegmentUsedLast == prev);

                if (pSection->mOffset == 0) // insert at buffer start?
                {
                    mSegmentUsedFirst = insert;

                    // set new tree root (alternatively we could create a new child at the bottom left of the tree)
                    mSegments[mSegmentUsedRoot].mOffset += pSection->mSize;
                    pSegment->mSize = pSection->mSize;
                    pSegment->mOffset = 0;
                    pSegment->mLT = (bbU32)-1;
                    pSegment->mGE = mSegmentUsedRoot;
                    mSegmentUsedRoot = insert;
                    goto dtBufferStream_Commit_insert_noleftneighbour;
                }
                else
                {
                    mSegmentUsedLast = insert;
                }
            }

            //
            // Link and adjust offsets in tree
            //
            bbASSERT((next == mSegmentUsedFirst) || (mSegmentUsedLast != prev));

            // link inserted segment to left neighbour
            pSegment->mSize = pSection->mSize;
            NodeInsert(insert, pSection->mOffset);

            dtBufferStream_Commit_insert_noleftneighbour:

            // xxx rebalance tree here
            break;
        }

        case dtSECTIONOPT_INSERT_REPLACE:
            bbASSERT((pSegment->mType != dtSEGMENTTYPE_NULL) || (pSegment->mFileSize == 0));
            bbASSERT((pSegment->mType != dtSEGMENTTYPE_MAP)  || (pSegment->mpData == NULL));

            pSegment->mType  = dtSEGMENTTYPE_MAP;
            pSegment->mpData = pSection->mpData;
            pSegment->mSize  = pSection->mSize;

            NodeSubstractOffset(pSection->mSegment, pSection->mOffset, -(bbS64)pSection->mSize);
            break;
        }

        #ifdef bbDEBUG
        CheckTree();
        #endif

        mBufSize += pSection->mSize;
        pSegment->mChanged = 1;
        NotifyChange(dtCHANGE_INSERT, pSection->mOffset, pSection->mSize, user);
        break;

    case dtSECTIONTYPE_MAP:
        bbASSERT(pSection->mOpt & dtSECTIONOPT_MAP_WRITE); // assert dtMAP_WRITE usage

        if ((pSection->mOpt & dtSECTIONOPT_MAP_TYPEMASK) != dtSECTIONOPT_MAP_SEQ)
        {
            dtPage* const pPage  = pSection->mpPage;
            bbU8*         pTmp   = pSection->mpData;
            bbU32         size   = pSection->mSize;
            bbU64         offset = pSection->mOffset;

            pUndo = NULL;
            if (!mUndoActive)
            {
                pUndo = mHistory.Push(dtCHANGE_OVERWRITE, offset, size, mUndoPoint!=0);
                if (!pUndo)
                {
                    err = bbELAST;
                    goto dtBufferStream_Commit_err;
                }
                mUndoPoint = 0;
                UpdateCanUndoState();
            }

            while (size > 0)
            {
                dtSection* pMapSeq = MapSeq(offset, 0, dtMAP_READONLY);
                bbASSERT(!pMapSeq || (pMapSeq->mOpt = dtMAP_WRITE));

                if (!pMapSeq)
                {
                    bbASSERT(bbErrGet() != bbEEOF);
                    if (pUndo)
                        mHistory.PushRevert();
                    goto dtBufferStream_Commit_err; //xxx not atomic
                }

                bbU32 tocopy = pMapSeq->mSize;
                if (size < tocopy)
                    tocopy = size;
                if (pUndo)
                {
                    bbMemMove(pUndo, pMapSeq->mpData, tocopy);
                    pUndo += tocopy;
                }
                bbMemMove(pMapSeq->mpData, pTmp, tocopy);

                if ((err = Commit(pMapSeq, user)) != bbEOK)
                {
                    if (pUndo)
                        mHistory.PushRevert();
                    goto dtBufferStream_Commit_err; //xxx not atomic
                }

                pTmp += tocopy;
                offset += tocopy;
                size -= tocopy;
            }
            break;
        }
        //fall through

    case dtSECTIONTYPE_MAPSEQ:
        bbASSERT((pSection->mOpt == dtMAP_WRITE) || (pSection->mType==dtSECTIONTYPE_MAP));
        pSegment = mSegments.GetPtr(pSection->mSegment);
        pSegment->mChanged = 1;
        NotifyChange(dtCHANGE_OVERWRITE, pSection->mOffset, pSection->mSize, user);
        break;

    default:
        bbASSERT(0);
    }



//OnSave(bbT("c:/xx"), dtBUFFERSAVETYPE_NEW);

    err = bbEOK;
    dtBufferStream_Commit_err:

    #ifdef bbDEBUG
    CheckTree();
    #endif

    SectionFree(pSection);
    return err;
}

void dtBufferStream::Discard(dtSection* const pSection)
{
    if (!pSection)
        return;

    dtSegment* pSegment;

    switch (pSection->mType)
    {
    case dtSECTIONTYPE_INSERT:
        pSegment = mSegments.GetPtr(pSection->mSegment);
        bbASSERT(pSegment->mType == dtSEGMENTTYPE_MAP);

        if (pSection->mOpt == dtSECTIONOPT_INSERT_REPLACE)
        {
            bbMemFree(pSection->mpData);
        }
        else
        {
            bbMemRealloc(pSegment->mSize, (void**)&pSegment->mpData); // restore or free heap block size

            if (pSection->mOpt == dtSECTIONOPT_INSERT_CREATE)
            {
                bbASSERT(pSegment->mpData == NULL); // ensure it was really freed
            
                // return segment to free pool
                pSegment->mNext = mSegmentFree;
                mSegmentFree = pSection->mSegment;
            }
        }

        break;

    case dtSECTIONTYPE_MAP:
        bbASSERT((pSection->mOpt & dtSECTIONOPT_MAP_WRITE) == 0); // assert dtMAP_READONLY usage
        if ((pSection->mOpt & dtSECTIONOPT_MAP_TYPEMASK) != dtSECTIONOPT_MAP_SEQ)
        {
            PageFree(pSection->mpPage);
            break;
        }
        // fall through

    case dtSECTIONTYPE_MAPSEQ:
        bbASSERT(pSection->mOpt == dtMAP_READONLY);
        bbASSERT(mSegments[pSection->mSegment].mType == dtSEGMENTTYPE_MAP);
        break;

    default:
        bbASSERT(0);
    }

    SectionFree(pSection);
}
#ifdef bbDEBUG

void dtBufferStream::DumpSavedTree()
{
    bbCHAR* pPath = bbPathTemp(bbT("c:\\"));
    if (pPath)
    {
        bbFILEH fh = bbFileOpen(pPath, bbFILEOPEN_TRUNC|bbFILEOPEN_READWRITE);
        if (fh)
        {
            bbFileWriteLE(fh, sizeof(gSavedClass), 4);
            bbFileWriteLE(fh, sizeof(dtSegment), 4);
            bbFileWriteLE(fh, gSavedSegmentSize, 4);
            bbFileWriteLE(fh, gSavedOp, 4);
            bbFileWrite(fh, &gSavedOffset, 8);
            bbFileWrite(fh, &gSavedSize, 8);
            bbFileWrite(fh, &gSavedClass, sizeof(gSavedClass));
            bbFileWrite(fh, gpSavedSegments, sizeof(dtSegment) * gSavedSegmentSize);
            bbFileClose(fh);
        }
    }
}

void dtBufferStream::LoadSavedTree(const bbCHAR* const pFileName, int const execute)
{
    bbFILEH fh = bbFileOpen(pFileName, bbFILEOPEN_READ);
    bbU32 sizeofClass, sizeofSegment;
    bbERR err;
    dtSegment* pSegment;
    dtSection* pSection;

    bbASSERT(IsOpen());

    if (fh)
    {
        bbFileRead(fh, &sizeofClass, 4);
        bbFileRead(fh, &sizeofSegment, 4);
        bbFileRead(fh, &gSavedSegmentSize, 4);
        bbFileRead(fh, &gSavedOp, 4);
        bbFileRead(fh, &gSavedOffset, 8);
        bbFileRead(fh, &gSavedSize, 8);

        bbASSERT((sizeofClass == sizeof(gSavedClass)) && (sizeofSegment == sizeof(dtSegment)));

        err = bbMemRealloc(gSavedSegmentSize * sizeof(dtSegment), (void**)&gpSavedSegments);
        bbASSERT(err == bbEOK);

        bbFileRead(fh, &gSavedClass, sizeof(gSavedClass));
        bbFileRead(fh, gpSavedSegments, gSavedSegmentSize * sizeof(dtSegment));

        ClearSegments();
        mSegments.SetSize(gSavedSegmentSize);
        bbMemMove(mSegments.GetPtr(), gpSavedSegments, gSavedSegmentSize * sizeof(dtSegment));
        mSegmentFree        = gSavedClass.mSegmentFree;
        mSegmentUsedFirst   = gSavedClass.mSegmentUsedFirst;
        mSegmentUsedLast    = gSavedClass.mSegmentUsedLast;
        mSegmentUsedRoot    = gSavedClass.mSegmentUsedRoot;
        mSegmentLastMapped  = -1;
        mBufSize            = gSavedClass.mBufSize;

        bbU32 walk = mSegmentUsedFirst;
        do
        {
            pSegment = mSegments.GetPtr(walk);

            if (pSegment->mType == dtSEGMENTTYPE_MAP)
            {
                pSegment->mpData = (bbU8*)bbMemAlloc(pSegment->mSize);
                bbMemClear(pSegment->mpData, pSegment->mSize);
            }
            
            walk = pSegment->mNext;
            
        } while (walk != mSegmentUsedFirst);

        if (execute)
        {
            switch(gSavedOp)
            {
            case DEL:
                err = Delete(gSavedOffset, gSavedSize, NULL);
                break;
            case INS_DISCARD:
                pSection = Insert(gSavedOffset, (bbU32)gSavedSize);
                if (pSection)
                    Discard(pSection);
                break;
            case INS_COMMIT:
                pSection = Insert(gSavedOffset, (bbU32)gSavedSize);
                if (pSection)
                    err = Commit(pSection, NULL);
                break;
            default:
                bbASSERT(0);
                break;
            }
        }
    }
}


void dtBufferStream::SaveTree(dtOP op, bbU64 offset, bbU64 size)
{
    if (mSegments.GetSize() > gSavedSegmentSize )
    {
        gSavedSegmentSize = mSegments.GetSize();
        bbMemRealloc(gSavedSegmentSize * sizeof(dtSegment), (void**)&gpSavedSegments);
    }
    bbMemMove(gpSavedSegments, mSegments.GetPtr(), sizeof(dtSegment) * mSegments.GetSize());
    gSavedClass = *this;
    gSavedOp = op;
    gSavedOffset = offset;
    gSavedSize = size;
}

void dtBufferStream::CheckTree()
{
    bbU64 offset = 0;
    dtSegment* pSegment;
    dtSegment* pStack = mSegments.GetPtr();
    bbU32 sp = 0;
    bbU32 walk, i;

    if (mSegments.GetSize() > gOffsetSize)
    {
        gOffsetSize = mSegments.GetSize();
        bbMemRealloc(8*gOffsetSize, (void**)&gpOffsets);
        bbMemRealloc(8*gOffsetSize, (void**)&gpOffsetStack);
    }

    if ((gCheckTreeDisable == 0) && (mSegmentUsedRoot != (bbU32)-1))
    {
        walk = mSegmentUsedFirst;
        do
        {
            bbASSERT(walk < mSegments.GetSize());
            gpOffsets[walk] = offset;
            if ((mSegments[walk].mType != dtSEGMENTTYPE_MAP) || (mSegments[walk].mChanged != 255))
                offset += mSegments[walk].GetSize();
            walk = mSegments[walk].mNext;
        } while (walk != mSegmentUsedFirst);

        offset = 0, i = 0, walk = mSegmentUsedRoot;
        do
        {
            pSegment = mSegments.GetPtr(walk);
            offset += pSegment->mOffset;

            if (offset != gpOffsets[walk])
            {
                DumpSavedTree();
                bbASSERT(offset == gpOffsets[walk]);
            }

            if (pSegment->mLT != (bbU32)-1)
            {
                bbASSERT(i < mSegments.GetSize());
                gpOffsetStack[i] = offset;
                mSegments[i++].mCapacity = pSegment->mLT;
            }

            walk = pSegment->mGE;

            if ((walk == (bbU32)-1) && i)
            {
                walk = mSegments[--i].mCapacity;
                offset = gpOffsetStack[i];
            }

        } while (walk != (bbU32)-1);
    }   
}

bbU32 dtBufferStream::DebugCheck()
{
    bbASSERT(mSegmentUsedFirst < mSegments.GetSize());
    bbASSERT(mSegmentUsedLast < mSegments.GetSize());

    //
    // Check for orphans or doubles in mSegments[],
    // verify total buffer size, calc CRC of mapped data
    //

    bbU32 prev, idx, freecount=0, usedcount=0, crc=0;
    bbU64 bufsize = 0;
    dtSegment* p = mSegments.GetPtr();
    dtSegment* p_end = mSegments.GetPtrEnd();

    while (p < p_end)
    {
        p->mOpt = 0;
        p++;
    }

    // - free list
    idx = mSegmentFree;
    while (idx < mSegments.GetSize())
    {
        bbASSERT(idx <= mSegments.GetSize());
        p = mSegments.GetPtr(idx);
        bbASSERT(!p->mOpt); // doubles?
        p->mOpt = 1;
        idx = p->mNext;
        freecount++;
    }

    // - used list
    idx = mSegmentUsedFirst;
    prev = mSegmentUsedLast;
    do
    {
        bbASSERT(idx <= mSegments.GetSize());

        p = mSegments.GetPtr(idx);

        bbASSERT(!p->mOpt); // doubles?
        p->mOpt = 1;

        bufsize += p->GetSize();

        if (p->mType == dtSEGMENTTYPE_MAP)
        {
            bbU8* pData = p->mpData;
            bbU8* const pDataEnd = pData + p->mSize;
            while (pData < pDataEnd)
                crc += (bbU32)*(pData++);
        }

        bbU32 const tmp = prev;
        prev = idx;
        idx = p->mNext;
        usedcount++;
    } while (idx != mSegmentUsedFirst);

    bbASSERT((freecount + usedcount) == mSegments.GetSize()); // orphans?
    bbASSERT(bufsize == mBufSize);

    return crc;
}

void dtBufferStream::DumpSegments()
{
    bbU32 prev = mSegmentUsedLast;
    bbU32 walk = mSegmentUsedFirst;
    bbU64 fileoffset = 0;
    bbU32 segments = 0;

    do
    {
        dtSegment* pSeg = mSegments.GetPtr(walk);

        bbPrintf(bbT("Type %d: fileoffs 0x%I64X, filesize 0x%I64X, size 0x%X, changed %d\n"),
            pSeg->mType,
            fileoffset,
            pSeg->mFileSize,
            pSeg->GetSize(),
            pSeg->mChanged);

        fileoffset += pSeg->mFileSize;
        segments++;

        bbU32 const tmp = prev;
        prev = walk;
        walk = pSeg->mNext;

    } while (walk != mSegmentUsedFirst);

    bbPrintf(bbT("Buffer size 0x%I64X, org filesize 0x%I64X, %d segments\n"), mBufSize, fileoffset, segments);
}

#endif


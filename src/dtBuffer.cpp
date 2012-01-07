#include <babel/mem.h>
#include <babel/str.h>
#include <babel/file.h>
#include "dtBuffer.h"

bbUINT dtBuffer::mNewBufferCount = 0;

void dtBufferNotify::OnBufferChange(dtBuffer* const pBuf, dtBufferChange* const pChange)
{
}

void dtBufferNotify::OnBufferMetaChange(dtBuffer* const pBuf, dtMETACHANGE const type)
{
}

dtBuffer::dtBuffer()
{
    mState = dtBUFFERSTATE_INIT;
    mOpt = 0;
    mUndoActive = 0;
    mSyncPt = 0;
    mpName = NULL;
    mRefCt = 0;

    //
    // - Init section index
    //
    bbMemClear(mSections, sizeof(mSections));
    mSectionFree = 0;
    for (bbUINT i=0; i<dtBUFFER_MAXSECTIONS; i++)
    {
        mSections[i].mIndex = (bbU8)i;
        mSections[i].mNextFree = (bbU8)(i+1);
    }
}

dtBuffer::~dtBuffer()
{
    bbASSERT(mRefCt == 0);
    bbASSERT(mState == dtBUFFERSTATE_INIT);
}

bbERR dtBuffer::AddNotifyHandler(dtBufferNotify* const pNotify)
{
    return mNotifyHandlers.Append(pNotify) ? bbEOK : bbELAST;
}

void dtBuffer::RemoveNotifyHandler(dtBufferNotify* const pNotify)
{
    dtBufferNotify** ppNot            = mNotifyHandlers.GetPtrEnd();
    dtBufferNotify** const ppNotStart = mNotifyHandlers.GetPtr();

    while (ppNot > ppNotStart)
    {
        --ppNot;
        if (*ppNot == pNotify)
        {
            *ppNot = *mNotifyHandlers.GetPtrLast();
            mNotifyHandlers.Grow(-1);
            return;
        }
    }
}

void dtBuffer::NotifyChange(dtCHANGE const type, bbU64 offset, bbU64 length, void* const user)
{
    mSyncPt++;

    if (!IsModified() && (mSyncPt != mSyncPtNoMod))
    {
        mOpt |= dtBUFFEROPT_MODIFIED;
        NotifyMetaChange(dtMETACHANGE_MODIFIED);
    }
    bbASSERT((mSyncPt == mSyncPtNoMod) || IsModified());

    dtBufferChange change;

    change.type      = (bbU8)type;
    change.undo      = mUndoActive;
    change.undopoint = mUndoPointRec;
    change.user      = user;
    change.offset    = offset;
    change.length    = length;
    
    bbUINT i = mNotifyHandlers.GetSize();
    while (i) mNotifyHandlers[--i]->OnBufferChange(this, &change);
}

void dtBuffer::NotifyMetaChange(dtMETACHANGE const type)
{
    bbUINT i = mNotifyHandlers.GetSize();
    while (i) mNotifyHandlers[--i]->OnBufferMetaChange(this, type);
}

void dtBuffer::AttachName(bbCHAR* const pName)
{
    if (!mpName && !pName)
        return;

    bbMemFree(mpName);
    mpName = pName;
    NotifyMetaChange(dtMETACHANGE_NAME);
}

void dtBuffer::ClearUndo()
{
    mHistory.Clear();
    SetUndo();
    UpdateCanUndoState();
}

void dtBuffer::UpdateCanUndoState()
{
    bbUINT opt = 0;

    if (mHistory.CanUndo())
        opt |= dtBUFFEROPT_CANUNDO;

    if (mHistory.CanRedo())
        opt |= dtBUFFEROPT_CANREDO;

    if ((mOpt & (dtBUFFEROPT_CANUNDO|dtBUFFEROPT_CANREDO)) ^ opt)
    {
        mOpt = (bbU8)((mOpt &~ (dtBUFFEROPT_CANUNDO|dtBUFFEROPT_CANREDO)) | opt);
        NotifyMetaChange(dtMETACHANGE_CANUNDO);
    }
}

bbERR dtBuffer::Undo(void* const user)
{
    bbERR err;
    bbUINT syncdiff;
    dtSection* pSection;
    dtBufferChange change;

    if (!mHistory.CanUndo())
        return bbErrSet(bbEEND);

    const bbU32 syncpt = mSyncPt;
    mUndoActive = 1;

    syncdiff = 0;
    do
    {
        int changeLength = mHistory.Peek(dtHistory::PEEKPREV, &change);
        change.undo = mUndoActive;
        mUndoPointRec = change.undopoint;
        err = bbEOK;

        switch (change.type)
        {
        case dtCHANGE_INSERT:
            if ((err = Read((bbU8*)change.user, change.offset, (bbU32)change.length)) == bbEOK)
            {
                err = Delete(change.offset, change.length, user);
            }
            break;

        case dtCHANGE_DELETE:
            if ((pSection = Insert(change.offset, (bbU32)change.length)) == NULL)
            {
                err = bbELAST;
                break;
            }

            bbASSERT(change.length <= 0xFFFFFFFFUL);
            bbMemMove(pSection->mpData, change.user, (bbU32)change.length);
            err = Commit(pSection, user);
            break;

        case dtCHANGE_OVERWRITE:
            bbASSERT(change.length <= 0xFFFFFFFFUL);
            if ((pSection = Map(change.offset, (bbU32)change.length, dtMAP_WRITE)) == NULL)
            {
                err = bbELAST;
                break;
            }
            bbMemSwap(pSection->mpData, change.user, (bbU32)change.length);
            err = Commit(pSection, user);
            break;
        }

        if (err != bbEOK)
            break; //xxx not atomic

        syncdiff++;
        mHistory.Seek(-changeLength);

    } while (mHistory.CanUndo() && (!change.undopoint));

    mUndoActive = 0;

    if ((err == bbEOK) && ((mSyncPt = syncpt - syncdiff) == mSyncPtNoMod))
    {
        bbASSERT(IsModified());
        mOpt = (bbU8)((bbUINT)mOpt &~ dtBUFFEROPT_MODIFIED);
        NotifyMetaChange(dtMETACHANGE_MODIFIED);
    }

    UpdateCanUndoState();
    return err;
}

bbERR dtBuffer::Redo(void* const user)
{
    bbERR err = bbEOK;
    dtSection* pSection;
    dtBufferChange change;
    dtBufferChange changeNext;

    if (!mHistory.CanRedo())
        return bbErrSet(bbEEND);

    const bbU32 syncpt = mSyncPt;
    mUndoActive = 2;

    bbUINT syncdiff = 0;
    bbUINT changeLength = mHistory.Peek(dtHistory::PEEKNEXT, &changeNext);
    changeNext.undo = mUndoActive;
    do
    {
        change = changeNext;
        mHistory.Seek(changeLength);

        if (mHistory.CanRedo())
        {
            changeLength = mHistory.Peek(dtHistory::PEEKNEXT, &changeNext);
            changeNext.undo = mUndoActive;
            mUndoPointRec = changeNext.undopoint;
        }
        else
        {
            mUndoPointRec = 1;
        }

        switch (change.type)
        {
        case dtCHANGE_INSERT:
            if ((pSection = Insert(change.offset, (bbU32)change.length)) == NULL)
            {
                err = bbELAST;
                break;
            }
            bbMemMove(pSection->mpData, change.user, (bbU32)change.length);
            err = Commit(pSection, user);
            break;

        case dtCHANGE_DELETE:
            err = Delete(change.offset, change.length, user);
            break;

        case dtCHANGE_OVERWRITE:
            if ((pSection = Map(change.offset, (bbU32)change.length, dtMAP_WRITE)) == NULL)
            {
                err = bbELAST;
                break;
            }
            bbMemSwap(pSection->mpData, change.user, (bbU32)change.length);
            err = Commit(pSection, user);
            break;
        }

        if (err != bbEOK)
            break; //xxx not atomic

        syncdiff++;

    } while (!mUndoPointRec);

    mUndoActive = 0;

    if ((err == bbEOK) && ((mSyncPt = syncpt + syncdiff) == mSyncPtNoMod))
    {
        bbASSERT(IsModified());
        mOpt = (bbU8)((bbUINT)mOpt &~ dtBUFFEROPT_MODIFIED);
        NotifyMetaChange(dtMETACHANGE_MODIFIED);
    }

    UpdateCanUndoState();
    return err;
}

bbCHAR* dtBuffer::PathNorm(const bbCHAR* const pPath)
{
    return bbPathNorm(pPath);
}

bbERR dtBuffer::Open(const bbCHAR* pPath)
{
    const bbCHAR* pPathUsed;
    bbCHAR autoname[12];

    bbASSERT(mState == dtBUFFERSTATE_INIT);

    if (pPath == NULL)
    {
        mOpt |= dtBUFFEROPT_NEW;

        bbSprintf(autoname, bbT("file%04u"), mNewBufferCount);
        if (++mNewBufferCount == 10000) mNewBufferCount=0;
        pPathUsed = autoname;

        if ((pPathUsed = bbStrDup(pPathUsed)) == NULL)
            goto dtBuffer_mem_Open_err;
    }
    else
    {
        mOpt &= ~dtBUFFEROPT_NEW;

        if ((pPathUsed = PathNorm(pPath)) == NULL)
            goto dtBuffer_mem_Open_err;
    }

    AttachName((bbCHAR*)pPathUsed);

    if (bbEOK != OnOpen(pPathUsed, (int)mOpt & dtBUFFEROPT_NEW))
        goto dtBuffer_mem_Open_err;

    SetState(dtBUFFERSTATE_OPEN);

    bbASSERT(mHistory.IsEmpty());
    SetUndo();
    mSyncPtNoMod = mSyncPt + 1;

    NotifyChange(dtCHANGE_ALL, 0, 0, NULL);
    NotifyMetaChange(dtMETACHANGE_MODIFIED);
    NotifyMetaChange(dtMETACHANGE_ISNEW);
    NotifyMetaChange(dtMETACHANGE_CANUNDO);

    return bbEOK;

    dtBuffer_mem_Open_err:
    AttachName(NULL);
    return bbELAST;
}

bbERR dtBuffer::Save(const bbCHAR* const pPath)
{
    dtBUFFERSAVETYPE savetype;
    bbCHAR* pPathNew = NULL;

    bbASSERT(mState == dtBUFFERSTATE_OPEN);

    if (pPath) // normalize path for new or saveas
    {
        if ((pPathNew = bbPathNorm(pPath)) == NULL)
            goto dtBuffer_Save_err;

        if (mOpt & dtBUFFEROPT_NEW)
        {
            savetype = dtBUFFERSAVETYPE_NEW;
        }
        else
        {
            if (bbStrCmp(mpName, pPathNew)==0)
            {
                savetype = dtBUFFERSAVETYPE_INPLACE;
                bbMemFreeNull((void**)&pPathNew);
            }
            else
            {
                savetype = dtBUFFERSAVETYPE_SAVEAS;
            }
        }
    }
    else
    {
        savetype = dtBUFFERSAVETYPE_INPLACE;
        bbASSERT(!(mOpt & dtBUFFEROPT_NEW));
    }

    if (OnSave((savetype == dtBUFFERSAVETYPE_INPLACE) ? mpName : pPathNew, savetype) != bbEOK)
        goto dtBuffer_Save_err;

    if (savetype != dtBUFFERSAVETYPE_INPLACE)
    {
        AttachName((bbCHAR*)pPathNew);
    }

    mSyncPtNoMod = mSyncPt;
    mOpt = (bbU8)((bbUINT)mOpt &~ (dtBUFFEROPT_NEW|dtBUFFEROPT_MODIFIED));
    NotifyMetaChange(dtMETACHANGE_MODIFIED);
    NotifyMetaChange(dtMETACHANGE_ISNEW);

    return bbEOK;

    dtBuffer_Save_err:
    bbMemFree(pPathNew);
    return bbELAST;
}

void dtBuffer::Close()
{
    if (mState != dtBUFFERSTATE_INIT)
    {
        OnClose();

        ClearUndo();
        AttachName(NULL);
        mOpt = 0;
        SetState(dtBUFFERSTATE_INIT);
    }
}

dtSection* dtBuffer::SectionAlloc()
{
    bbUINT const i = mSectionFree;

    if (i >= dtBUFFER_MAXSECTIONS)
    {
        bbErrSet(bbEFULL);
        return NULL;
    }

    dtSection* const pSec = mSections + i;
    mSectionFree = pSec->mNextFree;

    bbASSERT(pSec->mType == dtSECTIONTYPE_NONE);
    return pSec;
}

bbU32 dtBuffer::Read(bbU8* pDst, bbU64 offset, bbU32 size)
{
    dtSection* pSection;

    while (size > 0)
    {
        if ((pSection = MapSeq(offset, 0, dtMAP_READONLY)) == NULL)
            break;

        bbU32 tocopy = pSection->mSize;
        if (size < tocopy)
            tocopy = size;
        size -= tocopy;
        offset += tocopy;
        bbMemMove(pDst, pSection->mpData, tocopy);
        pDst += tocopy;
        Discard(pSection);
    }

    return size;
}

bbERR dtBuffer::Write(bbU64 offset, bbU8* pData, bbU32 size, int overwrite, void* user)
{
    bbU64 bufsize = GetSize();
    bbU64 enlarge = 0;
    dtSection* pSec = NULL;

    if (offset > bufsize)
        return bbErrSet(bbEBADPARAM);

    if (size == 0)
        return bbEOK;

    if ((!pData) || ((offset + size - 1) < offset))
        return bbErrSet(bbEBADPARAM);

    if (overwrite && ((offset + size) > bufsize))
    {
        enlarge = (offset + size) - bufsize;
        pSec = Insert(bufsize, (bbU32)enlarge);

        if (!pSec || (bbEOK != Commit(pSec, user)))
            return bbELAST;
    }

    pSec = overwrite ? Map(offset, size, dtMAP_WRITE) : Insert(offset, size);
    if (!pSec)
        goto dtBuffer_Write_err;

    bbMemCpy(pSec->mpData, pData, size);

    if (bbEOK != Commit(pSec, user))
        goto dtBuffer_Write_err;

    return bbEOK;

    dtBuffer_Write_err:
    if (enlarge)
        Delete(bufsize, enlarge, user);
    return bbELAST;
}



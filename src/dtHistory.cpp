#include "dtHistory.h"

dtHistory::dtHistory()
{
    mHistSize = mHistPos = 0;
}

dtHistory::~dtHistory()
{
}

void dtHistory::Clear()
{
    mHistPos = 0;
    Trunc();
    mHist.Clear();
}

void dtHistory::Trunc()
{
    bbUINT len;
    bbU32 pos = mHistSize;
    dtBufferChange change;

    while (pos > mHistPos)
    {
        len = Peek(pos, &change);

        if (change.length > 8)
            bbMemFree(change.user);

        pos -= len;
    }

    bbASSERT(pos == mHistPos);
    mHistSize = pos;

    bbU32 const capacity = mHist.GetSize() >> 1;
    if ((pos <= (capacity>>1)) && (capacity >= 64))
    {
        mHist.SetSize(capacity);
    }
}

bbUINT dtHistory::Peek(bbU32 const pos, dtBufferChange* const pChange) const
{
    bbASSERT(mHistSize);
    bbASSERT((pos <= mHistSize) || (pos == dtHistory::PEEKNEXT));

    bbUINT len;
    const bbU8* pTmp;
    
    if (pos == dtHistory::PEEKNEXT)
    {
        pTmp = mHist.GetPtr(mHistPos);
    }
    else
    {
        pTmp = mHist.GetPtr((pos == dtHistory::PEEKPREV) ? mHistPos : pos);
        len = pTmp[-1];
        pTmp -= len;
    }

    bbUINT const header = *(pTmp++);
    pChange->undo      = 0;
    pChange->undopoint = header & 4;
    pChange->type      = header & 3;
    pChange->user      = NULL;

    switch ((header >> 4) & 3)
    {
    case 0: pChange->offset = bbLD32(pTmp); pTmp+=4; break;
    case 1: pChange->offset = bbLD16(pTmp); pTmp+=2; break;
    case 2: pChange->offset = (bbU64)bbLD32(pTmp) | ((bbU64)bbLD32(pTmp+4)<<32); pTmp+=8; break;
    }
    
    switch ((header >> 6) & 3)
    {
    case 0:
        if ((pChange->length = *(pTmp++)) <= 8)
        {
            pChange->user = const_cast<bbU8*>(pTmp);
            pTmp += (bbUINT)pChange->length + 1;
            goto dtBuffer_HistPeek_out;
        }
        break;
    case 1: pChange->length = bbLD32(pTmp); pTmp+=4; break;
    case 2: pChange->length = (bbU64)bbLD32(pTmp) | ((bbU64)bbLD32(pTmp+4)<<32); pTmp+=8; break;
    }

    #if bbSIZEOF_UPTR > 4
    pChange->user = (void*)((bbUPTR)bbLD32(pTmp) | ((bbUPTR)bbLD32(pTmp)<<32));
    pTmp+=8+1;
    #else
    pChange->user = (void*)bbLD32(pTmp);
    pTmp+=4+1;
    #endif

    dtBuffer_HistPeek_out:
    if (pos == dtHistory::PEEKNEXT)
        len = (bbUINT)pTmp - (bbUINT)mHist.GetPtr(mHistPos);
    bbASSERT(len == pTmp[-1]);
    return len;
}


void dtHistory::PushRevert()
{
    dtBufferChange change;
    bbASSERT(mHistSize && (mHistSize == mHistPos));

    bbUINT len = Peek(mHistSize, &change);

    if (change.length > 8)
        bbMemFree(change.user);

    mHistSize = mHistPos = mHistSize - len;
}

bbU8* dtHistory::Push(dtCHANGE const type, bbU64 const offset, bbU64 const length, bool isUndoPoint)
{
    if (length > (1UL<<26)) // 256 MB
    {
        bbErrSet(bbENOMEM);
        return NULL;//xxx
    }

    if (mHistPos < mHistSize)
        Trunc();

    bbU32 pos = mHistPos;
    bbU8* pData = NULL;
    bbU32 capacity = mHist.GetSize();
    bbU8* pTmp;
    bbU8* pStart;
    bbUINT header, len;

    if ((pos+26) > capacity)
    {
        if (capacity == 0)
            capacity = 32;
        else
            capacity <<= 1;

        if (mHist.SetSize(capacity) != bbEOK)
            goto dtBuffer_HistPush_exit;
    }

    header = type;
    if (isUndoPoint)
        header |= 4;

    pTmp = mHist.GetPtr(pos + 1);

    if (((offset>>32) & 0xFFFFFFFFUL) == 0)
    {
        if (offset < 0x10000)
        {
            header |= 1<<4;
            bbST16(pTmp, (bbU32)offset);
            pTmp += 2;
        }
        else
        {
            bbST32(pTmp, (bbU32)offset);
            pTmp += 4;
        }
    }
    else
    {
        header |= 2<<4;
        bbST32(pTmp, (bbU32)offset);
        bbST32(pTmp+4, (bbU32)(offset>>32));
        pTmp += 8;
    }

    if (((length>>32) & 0xFFFFFFFFUL) == 0)
    {
        if (length < 0x100)
        {
            *(pTmp++) = (bbU8)length;

            if (length <= 8)
            {
                pData = pTmp;
                pTmp += length;
                goto dtBuffer_HistAdd_skip;
            }
        }
        else
        {
            header |= 1<<6;
            bbST32(pTmp, (bbU32)length);
            pTmp += 4;
        }
    }
    else
    {
        header |= 2<<6;
        bbST32(pTmp, (bbU32)length);
        bbST32(pTmp+4, (bbU32)(length>>32));
        pTmp += 8;
    }

    if ((pData = (bbU8*)bbMemAlloc((bbU32)length)) == NULL)
        goto dtBuffer_HistPush_exit;

    bbST32(pTmp, (bbU32)pData); pTmp+=4;
    #if bbSIZEOF_UPTR > 4
    bbST32(pTmp, (bbU32)((bbU64)pData>>32)); pTmp+=4;
    #endif

    dtBuffer_HistAdd_skip:

    pStart = mHist.GetPtr(pos);
    *pStart = (bbU8)header;
    len = (bbUINT)pTmp - (bbUINT)pStart + 1;
    *pTmp = (bbU8)len;

    mHistSize = mHistPos = pos + len;

    dtBuffer_HistPush_exit:
    return pData;
}


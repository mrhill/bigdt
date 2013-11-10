#include "dtRef.h"

#define dtBUFREF_MINSCANSIZE 4096

dtRefStore::dtRefStore()
{
    mpNodes = NULL;
    mTreeSize = 0;
    mNodeRoot = (bbU32)-1;
    mNodeFree = 0;

    SetMaxScanSize(1024*512);
}

dtRefStore::~dtRefStore()
{
    bbMemFree(mpNodes);
}

void dtRefStore::Clear()
{
    bbMemFreeNull((void**)&mpNodes);
    mTreeSize = 0;
    mNodeRoot = (bbU32)-1;
    mNodeFree = 0;
}

bbU32 dtRefStore::NewNode()
{
    bbU32 i;

    if (mNodeFree >= mTreeSize)
    {
        bbASSERT(mNodeFree == mTreeSize);

        const bbUINT newsize = mTreeSize + 32;
        if (bbEOK != bbMemRealloc(newsize * sizeof(dtRefNode), (void**)&mpNodes))
            return (bbU32)-1;
        mTreeSize = newsize;

        i = mNodeFree;
        bbU32 const i_end = mTreeSize;
        dtRefNode* pNode = GetNode(i);
        do
        {
            pNode->mLT = ++i;
            pNode++;

        } while (i < i_end);
    }

    i = mNodeFree;
    mNodeFree = GetNode(i)->mLT;
    return i;
}

void dtRefStore::BufferChange(const dtBufferChange* /*pChange*/)
{
    Clear();//xxx
}

bbERR dtRefStore::SaveRef(const dtRefPt* const pRef)
{
    bbU32 idx;

    if (pRef->mOpt & dtREFOPT_ESTIMATED)
        return bbEOK;

    if (pRef->mColumn)
        return bbErrSet(bbEBADPARAM);

    bbASSERT(!(pRef->mOpt & (dtREFOPT_ESTIMATED|dtREFOPT_ESTLOGLINE)));
    bbASSERT(!(pRef->mOpt & dtREFOPT_EOB));
    bbASSERT(pRef->mColumn == 0);

    if (mNodeFree >= mTreeSize) // tree capacity full?
    {
        if (mTreeSize >= GetOptimumTreeSize())
        {
            // Do not let tree grow above optimum capacity, and
            // delete the oldest node

            //xxx
        }
    }

    if ((idx = NewNode()) == (bbU32)-1)
        return bbELAST;

    dtRefNode* pNode = GetNode(idx);
    pNode->mLT =
    pNode->mGE = (bbU32)-1;
    pNode->ref = *pRef;

    bbU32* pParent = &mNodeRoot;
    bbU32 parent = mNodeRoot;
    while (parent != (bbU32)-1)
    {
        dtRefNode* pNode = GetNode(parent);

        if (pRef->mOffset < pNode->ref.mOffset)
            pParent = &pNode->mLT;
        else
            pParent = &pNode->mGE;

        parent = *pParent;
    }

    *pParent = idx;

    return bbEOK;
}

void dtRefStore::FindNodeFromOffset(bbU64 const offset, dtRefPt* const pRef)
{
    dtRefNode* pFound = NULL;

    bbU32 idx = mNodeRoot;

    if (idx != (bbU32)-1)
    {
        dtRefNode* pNode;
        do
        {
            pNode = GetNode(idx);

            if (offset >= pNode->ref.mOffset)
            {
                pFound = pNode;
                idx = pNode->mGE;
            }
            else
            {
                idx = pNode->mLT;
            }

        } while (idx != (bbU32)-1);
    }

    if (pFound)
    {
        *pRef = pFound->ref;
    }
    else
    {
        bbMemClear(pRef, sizeof(dtRefPt));
        pRef->mLogLine = mpIf->RefGetLogLineStart();
    }
}

void dtRefStore::FindNodeFromLine(bbU64 const line, dtRefPt* const pRef)
{
    dtRefNode* pFound = NULL;

    bbU32 idx = mNodeRoot;

    if (idx != (bbU32)-1)
    {
        /* Tree is sorted by offs, but sorting order for line is the same. */

        dtRefNode* pNode;
        do
        {
            pNode = GetNode(idx);

            if (line >= pNode->ref.mLine)
            {
                pFound = pNode;
                idx = pNode->mGE;
            }
            else
            {
                idx = pNode->mLT;
            }

        } while (idx != (bbU32)-1);
    }

    if (pFound)
    {
        *pRef = pFound->ref;
    }
    else
    {
        bbMemClear(pRef, sizeof(dtRefPt));
        pRef->mLogLine = mpIf->RefGetLogLineStart();
    }
}

bbU32 dtRefStore::GetBytesPerLine(dtRefPt* const pRef)
{
    bbU32 BytesPerLine = 100;

    if (pRef->mLine)
    {
        BytesPerLine = (bbU32)(pRef->mOffset / pRef->mLine);
        if (!BytesPerLine)
            BytesPerLine = 1;
    }

    return BytesPerLine;
}

bbERR dtRefStore::Offset2Ref(bbU64 offset, dtRefPt* const pRef)
{
    bbERR err;

    bbU64 const bufsize = mpIf->RefGetBufSize();
    if (offset > bufsize)
        offset = bufsize;

    FindNodeFromOffset(offset, pRef);

    bbU64 diff = offset - pRef->mOffset;

    if (diff == 0)
        return bbEOK;

    if (diff > mMaxScanSize)
    {
        if (pRef->mLine == 0)
        {
            //
            // the reference tree seems empty, scan start of buffer to
            // estimate BytesPerLine
            //
            bbASSERT((pRef->mOffset == 0) && (pRef->mColumn== 0));

            if (bbEOK != mpIf->RefSkip(pRef, mMaxScanSize>>2, dtREFSKIP_OFFSET))
            {
                bbASSERT(0); // this should succeed unless I/O or memory error
            }
            else
            {
                SaveRef(pRef);
            }
        }

        bbU32 const BytesPerLine = GetBytesPerLine(pRef);
        bbASSERT(offset > pRef->mOffset);

        dtRefPt const knownRef = *pRef;

        pRef->mLine    += (offset - pRef->mOffset) / BytesPerLine;
        pRef->mOffset   = offset;
        pRef->mBitOffs  = 0;
        pRef->mOpt      = dtREFOPT_ESTIMATED;
        pRef->mEncState = 0;
        pRef->mColumn   = 0;

        err = mpIf->RefSyncLine(pRef, &knownRef, mMaxScanSize);

        if (err == bbEOK)
            SaveRef(pRef);

        return err;
    }

    err = mpIf->RefSkip(pRef, offset, dtREFSKIP_OFFSET);

    if (err == bbEOK)
    {
        if (diff > dtBUFREF_MINSCANSIZE)
            SaveRef(pRef); // ignore error
    }

    return err;
}

bbERR dtRefStore::Line2Ref(bbU64 const line, dtRefPt* const pRef)
{
    bbERR err;

    FindNodeFromLine(line, pRef);

    bbU64 diff = line - pRef->mLine;

    if (diff == 0)
        return bbEOK;

    bbU32 BytesPerLine = GetBytesPerLine(pRef);

    diff *= BytesPerLine;
    if (diff > mMaxScanSize)
    {
        dtRefPt const knownRef = *pRef;

        bbASSERT(line > pRef->mLine);
        pRef->mOffset  += diff;
        pRef->mLine     = line;
        pRef->mBitOffs  = 0;
        pRef->mOpt      = dtREFOPT_ESTIMATED; //xxx
        pRef->mEncState = 0;

        bbU64 const bufsize = mpIf->RefGetBufSize();

        if (pRef->mOffset > bufsize)
        {
            if ((err = Offset2Ref(bufsize, pRef)) == bbEOK)
                err = bbErrSet(bbEEOF);
        }
        else
        {
            err = mpIf->RefSyncLine(pRef, &knownRef, mMaxScanSize);

            if (err == bbEOK)
                SaveRef(pRef);
        }

        return err;
    }

    err = mpIf->RefSkip(pRef, line, dtREFSKIP_LINE);

    if (err == bbEOK)
    {
        if (diff > dtBUFREF_MINSCANSIZE)
            SaveRef(pRef); // ignore error
    }

    return err;
}


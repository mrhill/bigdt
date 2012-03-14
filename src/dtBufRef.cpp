#include "dtBufRef.h"

#define dtBUFREF_MINSCANSIZE 4096

dtBufRef::dtBufRef() : bbTree(sizeof(dtBufRefPt), (bbUINT)(bbUPTR)&((dtBufRefPt*)(bbUPTR)0)->mLevel)
{
    SetMaxScanSize(1024*512);
    bbMemClear(&mHint, sizeof(dtBufRefPt));
}

void dtBufRef::Clear()
{
    bbTree::Clear();
    bbMemClear(&mHint, sizeof(dtBufRefPt));
}

bbERR dtBufRef::SaveRef(const dtBufRefPt* const pRef)
{
    bbU32 idx;

    if (pRef->mOpt & dtBUFREFOPT_ESTIMATED)
    {
        mHint = *pRef;
        return bbEOK;
    }

    if (pRef->mColumn)
        return bbErrSet(bbEBADPARAM);

    bbASSERT(!(pRef->mOpt & (dtBUFREFOPT_ESTIMATED|dtBUFREFOPT_ESTLOGLINE)));
    bbASSERT(!(pRef->mOpt & dtBUFREFOPT_EOB));
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

    dtBufRefNode* pNode = GetNode(idx);
    pNode->mLT =
    pNode->mGE = (bbU32)-1;
    pNode->ref = *pRef;

    bbU32* pParent = &mNodeRoot;
    bbU32 parent = mNodeRoot;
    while (parent != (bbU32)-1)
    {
        dtBufRefNode* pNode = GetNode(parent);

        if (pRef->mOffset < pNode->ref.mOffset)
            pParent = &pNode->mLT;
        else 
            pParent = &pNode->mGE;

        parent = *pParent;
    }

    *pParent = idx;

    return bbEOK;
}

void dtBufRef::FindNodeFromOffset(bbU64 const offset, dtBufRefPt* const pRef)
{
    dtBufRefNode* pFound = NULL;

    bbU32 idx = mNodeRoot;

    if (idx != (bbU32)-1)
    {
        dtBufRefNode* pNode;
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
        bbMemClear(pRef, sizeof(dtBufRefPt));
        pRef->mLogLine = mpIf->BufRefGetLogLineStart();
    }
}

void dtBufRef::FindNodeFromLine(bbU64 const line, dtBufRefPt* const pRef)
{
    dtBufRefNode* pFound = NULL;

    bbU32 idx = mNodeRoot;

    if (idx != (bbU32)-1)
    {
        /* Tree is sorted by offs, but sorting order for line is the same. */

        dtBufRefNode* pNode;
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
        bbMemClear(pRef, sizeof(dtBufRefPt));
        pRef->mLogLine = mpIf->BufRefGetLogLineStart();
    }
}

bbU32 dtBufRef::GetBytesPerLine(dtBufRefPt* const pRef)
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

bbERR dtBufRef::Offset2Ref(bbU64 offset, dtBufRefPt* const pRef)
{
    bbERR err;
    
    bbU64 const bufsize = mpIf->BufRefGetSize();
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
        
            if (bbEOK != mpIf->BufRefSkip(pRef, mMaxScanSize>>2, dtBUFREFSKIP_OFFSET))
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

        dtBufRefPt const knownRef = *pRef;
        
        pRef->mLine    += (offset - pRef->mOffset) / BytesPerLine;
        pRef->mOffset   = offset;
        pRef->mBitOffs  = 0;
        pRef->mOpt      = dtBUFREFOPT_ESTIMATED;
        pRef->mEncState = 0;
        pRef->mColumn   = 0;

        err = mpIf->BufRefSyncLine(pRef, &knownRef, mMaxScanSize);

        if (err == bbEOK)
            SaveRef(pRef);

        return err;
    }

    err = mpIf->BufRefSkip(pRef, offset, dtBUFREFSKIP_OFFSET);

    if (err == bbEOK)
    {
        if (diff > dtBUFREF_MINSCANSIZE)
            SaveRef(pRef); // ignore error
    }

    return err;
}

bbERR dtBufRef::Line2Ref(bbU64 const line, dtBufRefPt* const pRef)
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
        dtBufRefPt const knownRef = *pRef;

        bbASSERT(line > pRef->mLine);
        pRef->mOffset  += diff;
        pRef->mLine     = line;
        pRef->mBitOffs  = 0;
        pRef->mOpt      = dtBUFREFOPT_ESTIMATED; //xxx
        pRef->mEncState = 0;

        bbU64 const bufsize = mpIf->BufRefGetSize();

        if (pRef->mOffset > bufsize)
        {
            if ((err = Offset2Ref(bufsize, pRef)) == bbEOK)
                err = bbErrSet(bbEEOF);
        }
        else
        {
            err = mpIf->BufRefSyncLine(pRef, &knownRef, mMaxScanSize);

            if (err == bbEOK)
                SaveRef(pRef);
        }

        return err;
    }

    err = mpIf->BufRefSkip(pRef, line, dtBUFREFSKIP_LINE);

    if (err == bbEOK)
    {
        if (diff > dtBUFREF_MINSCANSIZE)
            SaveRef(pRef); // ignore error
    }

    return err;
}


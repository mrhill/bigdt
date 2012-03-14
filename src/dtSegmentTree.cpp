#include "babel/file.h"
#include "dtSegmentTree.h"

dtSegmentTree::dtSegmentTree()
{
    mSegmentUsedFirst =
    mSegmentUsedLast =
    mSegmentUsedRoot = 0;

    ClearSegments();
}

dtSegmentTree::~dtSegmentTree()
{
}

void dtSegmentTree::ClearSegments()
{
    mSegments.Clear();
    mSegmentFree = 0;
}

bbU32 dtSegmentTree::NewSegment()
{
    bbU32 i;

    if (mSegmentFree >= mSegments.GetSize())
    {
        if (!mSegments.Grow(dtSEGMENTTREE_IDXENLARGE))
            return (bbU32)-1;

        i = mSegmentFree;
        bbU32 const i_end = mSegments.GetSize();
        do
        {
            dtSegment* const pNode = mSegments.GetPtr(i);
            pNode->mNext = ++i;
        } while (i < i_end);
    }

    i = mSegmentFree;
    mSegmentFree = mSegments[i].mNext;
    return i;
}

bbU32 dtSegmentTree::FindSegment(bbU64 const offset, bbU64* const pSegmentStart, int const wrap)
{
    bbU32 walk = mSegmentUsedRoot, found;
    bbU64 segmentstart = mSegments[walk].mOffset;

    // in debug build init with NIL
    // if there are no bugs, a node will always be found
    bbASSERT(found = (bbU32)-1); 

    for (;;)
    {
        if (offset >= segmentstart)
        {
            found = walk;
            *pSegmentStart = segmentstart;
            walk = mSegments[walk].mGE;
        }
        else
        {
            walk = mSegments[walk].mLT;
        }

        if (walk == (bbU32)-1)
        {
            bbASSERT(found != (bbU32)-1);

            if (wrap && (found == mSegmentUsedLast) && ((offset-segmentstart) >= mSegments[found].GetSize()))
            {
                bbASSERT(mSegments[found].mNext == mSegmentUsedFirst);
                *pSegmentStart = 0;
                return mSegmentUsedFirst;
            }
            else
            {
                return found;
            }
        }

        segmentstart += mSegments[walk].mOffset;
    }
}

void dtSegmentTree::NodeLinkRight(bbU32 const idx, dtSegment* pSubTree, bbS64 offset)
{
    bbASSERT(pSubTree->GetSize() == (bbU64)offset);

    bbU32 walk;
    if ((walk = pSubTree->mGE) != (bbU32)-1)
    {
        for(;;)
        {
            pSubTree = mSegments.GetPtr(walk);

            offset -= pSubTree->mOffset;
    
            if (pSubTree->mLT == (bbU32)-1)
                break;
    
            walk = pSubTree->mLT;
        }
        pSubTree->mLT = idx;
    }
    else
    {
        pSubTree->mGE = idx;
    }

    dtSegment* const pSegment = mSegments.GetPtr(idx);

    pSegment->mOffset = offset;
    pSegment->mLT =
    pSegment->mGE = (bbU32)-1;
}

void dtSegmentTree::NodeInsert(bbU32 const idx, bbU64 const segmentstart)
{
    bbASSERT(segmentstart);

    dtSegment* const pSegment = mSegments.GetPtr(idx);
    dtSegment*       pLeft    = mSegments.GetPtr(pSegment->mPrev);
    bbU64 const size = pSegment->GetSize();
    bbU64 offset = pLeft->GetSize();

    // adjust offsets of all crossing anchestor nodes
    NodeSubstractOffset(pSegment->mPrev, segmentstart - offset, -(bbS64)size);

    if (pLeft->mGE != (bbU32)-1)
    {
        pLeft = mSegments.GetPtr(pLeft->mGE);
        for(;;)
        {
            offset -= pLeft->mOffset;
    
            if (pLeft->mLT == (bbU32)-1)
                break;
    
            pLeft = mSegments.GetPtr(pLeft->mLT);
        }
        pLeft->mLT = idx;
    }
    else
    {
        pLeft->mGE = idx;
    }

    pSegment->mOffset = offset;
    pSegment->mLT =
    pSegment->mGE = (bbU32)-1;
}

void dtSegmentTree::NodeSubstractOffset(bbU32 const idx, bbU64 const segmentstart, bbS64 const diff)
{
    dtSegment* const pSegment = mSegments.GetPtr(idx);
    
    bbASSERT(diff); // not required, but should not happen either

    if ((bbS64)pSegment->mOffset < 0)               // +size on self offset if self is left child
    {
        pSegment->mOffset += diff;
        bbASSERT(((bbS64)pSegment->mOffset < 0) || (pSegment->mChanged==255));
    }

    if (pSegment->mGE != (bbU32)-1)                 // -size on right child
    {
        mSegments[pSegment->mGE].mOffset -= diff;
        bbASSERT((bbS64)mSegments[pSegment->mGE].mOffset >= 0);
    }
    
    bbU32 walk = mSegmentUsedRoot;                  // -size on all anchestors crossing idx
    bbU64 offset = 0;
    int dir = 1;
    for(;;)
    {
        if (walk == idx)
            break;
        
        offset += mSegments[walk].mOffset;

        if (dir)
        {
            if (offset > segmentstart) // jumped across resized segment?
            {
                bbASSERT((bbS64)(mSegments[walk].mOffset - diff) >= 0);
                mSegments[walk].mOffset -= diff;
                dir = 0;
                walk = mSegments[walk].mLT;
                bbASSERT(walk != (bbU32)-1); // must hit self
            }
            else
            {
                walk = mSegments[walk].mGE;
                bbASSERT(walk != (bbU32)-1); // must hit self
            }
        }
        else
        {
            if (offset <= segmentstart)
            {
                bbASSERT((bbS64)(mSegments[walk].mOffset + diff) < 0);
                mSegments[walk].mOffset += diff;
                dir = 1;
                walk = mSegments[walk].mGE;
                bbASSERT(walk != (bbU32)-1); // must hit self
            }
            else
            {
                walk = mSegments[walk].mLT;
                bbASSERT(walk != (bbU32)-1); // must hit self
            }
        }
    }
}

void dtSegmentTree::NodeDelete(bbU32 const idx, bbU64 const segmentstart)
{
    bbU32* pParentLink = &mSegmentUsedRoot;
    bbU32  walk = mSegmentUsedRoot;
    bbU64  offset = 0;
    dtSegment* pSegment;

    //
    // Find pParentLink
    //
    for(;;)
    {
        pSegment = mSegments.GetPtr(walk);
        offset += pSegment->mOffset;

        if (walk == idx)
            break;

        if (segmentstart < offset)
        {
            pParentLink = &pSegment->mLT;
            walk = pSegment->mLT;
        }
        else
        {
            pParentLink = &pSegment->mGE;
            walk = pSegment->mGE;
        }
        bbASSERT(walk != (bbU32)-1);
    }
    bbASSERT(offset == segmentstart);

    //
    // Update relative offsets
    //
    bbASSERT((pSegment->mChanged != 255) && (pSegment->mChanged = 255)); // for assert in NodeSubstractOffset: mark segment as to be deleted
    #ifdef bbDEBUG
    bbS64 dbg_savedoffset = pSegment->mOffset;
    #endif

    if ((offset = pSegment->GetSize()) != 0)
    {
        NodeSubstractOffset(idx, segmentstart, offset);
    }

    //
    // Delete node by replacing it with appropriate subtree
    //
    if ((walk = pSegment->mGE) == (bbU32)-1)
    {
        // Case A) no GE subtree -> move up LT subtree (or -1)
        //      (parent)
        //         . . <- pParentLink
        //        /   \ 
        //           (del)
        //           /   x
        //          o
        //         / \ 
        //
        if ((*pParentLink = pSegment->mLT) != (bbU32)-1)
        {
            bbASSERT((bbS64)mSegments[pSegment->mLT].mOffset < 0);
            mSegments[pSegment->mLT].mOffset += pSegment->mOffset;
            bbASSERT((bbS64)(mSegments[pSegment->mLT].mOffset ^ dbg_savedoffset) >= 0); // sign must not change
        }        
    }
    else
    {
        // find smallest node in GE subtree

        offset = mSegments[walk].mOffset;

        if (mSegments[walk].mLT != (bbU32)-1)
        {
            bbU32 walk_parent;
            do
            {
                walk_parent = walk;
                walk = mSegments[walk].mLT;
                offset += mSegments[walk].mOffset;

            } while (mSegments[walk].mLT != (bbU32)-1);

            bbASSERT(offset == 0); //xxx
            bbASSERT(walk == pSegment->mNext);

            // walk is now smallest node in subtree
            // if it has a GE subtree, we need to move that up, to isolate walk

            if (mSegments[walk].mGE != (bbU32)-1)
            {
                mSegments[mSegments[walk].mGE].mOffset += mSegments[walk].mOffset;
                bbASSERT((bbS64)mSegments[mSegments[walk].mGE].mOffset < 0);
            }
            mSegments[walk_parent].mLT = mSegments[walk].mGE;

            bbASSERT(pSegment->mGE != (bbU32)-1);
            mSegments[pSegment->mGE].mOffset += offset;
            bbASSERT((bbS64)mSegments[pSegment->mGE].mOffset >= 0);
            mSegments[walk].mGE = pSegment->mGE;
        }
        bbASSERT((bbS64)offset >= 0);

        if (pSegment->mLT != (bbU32)-1)
        {
            mSegments[pSegment->mLT].mOffset -= offset;
            bbASSERT((bbS64)mSegments[pSegment->mLT].mOffset < 0);
        }
        mSegments[walk].mLT = pSegment->mLT;
        mSegments[walk].mOffset = pSegment->mOffset;
        
        bbASSERT((bbS64)(mSegments[walk].mOffset ^ dbg_savedoffset) >= 0); // sign must not change

        *pParentLink = walk;
    }
}

bbU32 dtSegmentTree::SplitNullSegment(bbU32 const idx, bbU64 segmentoffset)
{
    dtSegment* pSegmentLeft = mSegments.GetPtr(idx);
    bbU32 right;

    bbASSERT(segmentoffset); // 0-size segments must not be created
    bbASSERT(pSegmentLeft->mType == dtSEGMENTTYPE_NULL);
    bbASSERT(segmentoffset <= pSegmentLeft->mFileSize);

    if (pSegmentLeft->mFileSize == segmentoffset)
    {
        // split position is on segment end, no need to split
        right = pSegmentLeft->mNext;
    }
    else
    {
        // split NULL segment into two
        if ((right = NewSegment()) == (bbU32)-1)
            return (bbU32)-1;

        if (idx == mSegmentUsedLast)
            mSegmentUsedLast = right;

        pSegmentLeft = mSegments.GetPtr(idx);
        dtSegment* const pSegmentRight = mSegments.GetPtr(right);

        pSegmentRight->mType       = dtSEGMENTTYPE_NULL;
        pSegmentRight->mChanged    = 0;
        pSegmentRight->mFileOffset = pSegmentLeft->mFileOffset + segmentoffset;
        pSegmentRight->mFileSize   = pSegmentLeft->mFileSize - segmentoffset;
        pSegmentLeft->mFileSize    = segmentoffset;

        bbU32 const next      = pSegmentLeft->mNext;
        pSegmentRight->mPrev  = idx;
        pSegmentRight->mNext  = next;
        pSegmentLeft->mNext   = right;
        mSegments[next].mPrev = right;

        NodeLinkRight(right, pSegmentLeft, segmentoffset);
        #ifdef bbDEBUG
        CheckTree();
        #endif
        // xxx rebalance tree here
    }

    return right;
}

bbU32 dtSegmentTree::SplitMapSegment(bbU32 const idx, bbU32 const segmentoffset)
{
    bbU32 right;

    bbASSERT(segmentoffset); // 0-size segments must not be created
    bbASSERT(mSegments[idx].mType == dtSEGMENTTYPE_MAP);
    bbASSERT(segmentoffset <= mSegments[idx].mSize);

    dtSegment* const pSegmentLeft  = mSegments.GetPtr(idx);

    if (pSegmentLeft->mSize == segmentoffset)
    {
        // split position is on segment end, no need to split
        return pSegmentLeft->mNext;
    }

    // split segment into two
    if ((right = NewSegment()) == (bbU32)-1)
        return (bbU32)-1;

    dtSegment* const pSegmentRight = mSegments.GetPtr(right);

    bbU32 rightsize = pSegmentLeft->mSize - segmentoffset;
    if ((pSegmentRight->mpData = (bbU8*) bbMemAlloc(rightsize)) == NULL)
    {
        UndoSegment(right);
        return (bbU32)-1;
    }
    bbMemMove(pSegmentRight->mpData, pSegmentLeft->mpData + segmentoffset, rightsize);
    bbMemRealloc(segmentoffset, (void**)&pSegmentLeft->mpData);

    if (idx == mSegmentUsedLast)
        mSegmentUsedLast = right;

    pSegmentRight->mType = dtSEGMENTTYPE_MAP;
    pSegmentRight->mSize = rightsize;
    pSegmentLeft->mSize = segmentoffset;

    if ((pSegmentRight->mChanged = pSegmentLeft->mChanged) == 0)
    {
        bbASSERT(pSegmentLeft->mFileSize == (pSegmentLeft->mSize + rightsize));
        pSegmentLeft->mFileSize = segmentoffset;
        pSegmentRight->mFileSize = rightsize;
    }
    else
    {
        pSegmentRight->mFileSize = 0;
    }

    bbU32 const next      = pSegmentLeft->mNext;
    pSegmentRight->mPrev  = idx;
    pSegmentRight->mNext  = next;
    pSegmentLeft->mNext   = right;
    mSegments[next].mPrev = right;

    NodeLinkRight(right, pSegmentLeft, segmentoffset);
    #ifdef bbDEBUG
    CheckTree();
    #endif
    // xxx rebalance tree here

    return right;
}


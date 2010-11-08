#ifndef dtHistory_H_
#define dtHistory_H_

#include "dtdefs.h"
#include "babel/Arr.h"

class dtHistory
{
    // Serialized changes history format (max 1+8+8+8+1=26 bytes)
    // 1 byte     : bit 0..1 dtCHANGE type
    //              bit 2    1 = Undo point
    //              bit 4..5 offset byte length (0=>4, 1=>2, 2=>8)
    //              bit 6..7 length byte length (0=>1, 1=>4, 2=>8)
    // 1..8 bytes : offset
    // 1..8 bytes : length
    // 0..8 bytes : if dtCHANGE_INSERT 0 bytes, else if length<=8: data bytes, else serialized pointer (native size)
    // 1 byte     : length of change, for reverse walk

    bbArrU8 mHist;     //!< Change history
    bbU32   mHistSize; //!< Next write position in change history
    bbU32   mHistPos;  //!< Next read position in change history

public:
    dtHistory();
    ~dtHistory();

    void   Clear();
    void   Trunc();
    bbU8*  Push(dtCHANGE const type, bbU64 const offset, bbU64 const length, bool isUndoPoint);
    void   PushRevert();

    static const bbU32 PEEKPREV = 0;
    static const bbU32 PEEKNEXT = (bbU32)-1;

    bbUINT Peek(bbU32 const pos, dtBufferChange* const pChange) const;

    inline void Seek(int const size)
    {
        bbASSERT((bbU32)(mHistPos+size) <= mHistSize);
        mHistPos += size;
    }


    inline bool IsEmpty() const { return mHist.GetSize() == 0; }
    inline bool CanUndo() const { return mHistPos > 0; }
    inline bool CanRedo() const { return mHistPos < mHistSize; }
};

#endif /* dtHistory_H_ */


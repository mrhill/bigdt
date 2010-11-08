#ifndef dtSTREAM_H_
#define dtSTREAM_H_

#include "dtdefs.h"

struct dtStream
{
    bbU64 mOffs;

    /** Get buffer size in bytes.
        @return Size in bytes, or (bbU64)-1 on error
    */
    virtual bbU64 GetSize() = 0;

    /** Seek read/write position to buffer offset.
        If the specified offset is beyound the buffer size bbEEOF error is returned.
        Seeking exactly to the buffer end (offset equal to buffer size) succeeds.
        @param offset Byte offset relative to buffer start
    */
    virtual bbERR Seek(bbU64 offset)
    {
        mOffs = offset;
        return bbEOK;
    }
    
    /** Seek to buffer by offset relative to current read/write position.
        If the specified offset is beyound the buffer size bbEEOF error is returned.
        Seeking exactly to the buffer end (offset equal to buffer size) succeeds.
        @param offset Byte offset relative to buffer start
    */
    virtual bbERR Skip(bbS64 offset)
    {
        mOffs += offset;
        return bbEOK;
    }

    /** Return current read/write position.
        @return Byte offset relative to buffer start
    */
    inline bbU64 Tell() const
    {
        return mOffs;
    }

    /** Read block from buffer.
        If an error is returned, including bbEEOF, the current read/write position will become undefined.
        @param pBuf Pointer to buffer to receive data
        @param size Number of bytes to read
    */
    virtual bbERR Read(bbU8* pBuf, bbU32 size) = 0;

    /** Write block to buffer.
        If an error is returned, including bbEEOF, the current read/write position will become undefined.
        @param pBuf Pointer to buffer to holding data to write
        @param size Number of bytes to write
    */
    virtual bbERR Write(bbU8* pBuf, bbU32 size) = 0;
};

#endif /* dtSTREAM_H_ */


#ifndef dtSTREAMFILE_H_
#define dtSTREAMFILE_H_

#include "dtStream.h"
#include <babel/file.h>

struct dtStreamFile : dtStream
{
    bbFILEH mhFile;

    dtStreamFile()
    {
        mhFile = NULL;
    }

    ~dtStreamFile()
    {
        bbFileClose(mhFile);
    }

    /** Attach opened file to buffer.
        @param hFile Handle to file open for reading
    */
    void Attach(bbFILEH hFile)
    {
        mhFile = hFile;
    }

    /** Open file and attach to buffer.
        See bbFileOpen for parameter description.
    */
    inline bbFILEH Open(const bbCHAR* pFilename, const bbUINT flags)
    {
        return mhFile = bbFileOpen(pFilename, flags);
    }

    inline void Close()
    {
        bbFileClose(mhFile);
        mhFile = NULL;
    }

    virtual bbU64 GetSize();
    virtual bbERR Seek(bbU64 offset);
    virtual bbERR Skip(bbS64 offset);
    virtual bbERR Read(bbU8* pBuf, bbU32 size);
    virtual bbERR Write(bbU8* pBuf, bbU32 size);
};

#endif /* dtSTREAMFILE_H_ */


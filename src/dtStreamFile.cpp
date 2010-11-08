#include "dtStreamFile.h"
#include "babel/file.h"

bbU64 dtStreamFile::GetSize()
{
    return bbFileExt(mhFile);
}

bbERR dtStreamFile::Seek(bbU64 offset)
{
    mOffs = offset;
    return bbFileSeek(mhFile, offset, bbFILESEEK_SET);
}

bbERR dtStreamFile::Skip(bbS64 offset)
{
    mOffs += offset;
    return bbFileSeek(mhFile, offset, bbFILESEEK_CUR);
}

bbERR dtStreamFile::Read(bbU8* pBuf, bbU32 size)
{
    mOffs += size;
    return bbFileRead(mhFile, pBuf, size);
}

bbERR dtStreamFile::Write(bbU8* pBuf, bbU32 size)
{
    mOffs += size;
    return bbFileWrite(mhFile, pBuf, size);
}


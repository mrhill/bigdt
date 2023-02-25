#include <babel/babel.h>
#include <dt/dtBufferStream.h>
#include <time.h>

void syntax()
{
    printf("dtBuffer implementation unit test\n");
    printf("Syntax: buffertest [options] <filename>\n");
    printf("-l <logfile>   Replay buffer access log\n");
    printf("-t <classname> Test named dtBuffer implementation, default is dtBufferStream\n");
}

struct Param
{
    bbCHAR* pFile;          //!< Datafile to open for test, managed heap block
    bbCHAR* pLogFile;       //!< Buffer log to replay, or NULL, managed heap block
    const char* pBufferClass; //!< Buffer classname to test, or NULL for dtBufferStream

    Param()
    {
        bbMemClear(this, sizeof(Param));
    }

    ~Param()
    {
        bbMemFree(pLogFile);
        bbMemFree(pFile);
    }
};

bbERR test1(Param* pParams, dtBuffer& buffer)
{
    bbERR err;
    dtSection* pSection;

    printf("test1: new/open/close/save as\n");

    if ((err = buffer.Open(NULL)) != bbEOK)
    {
        printf("Error %d on buffer open for new file\n", err);
        goto test1_err;
    }

    buffer.Close();
    
    if ((err = buffer.Open(pParams->pFile)) != bbEOK)
    {
        printf("Error %d on buffer open for file %s\n", err, pParams->pFile);
        goto test1_err;
    }

    printf("Is Open: %d, is modified: %d\n", buffer.IsOpen(), buffer.IsModified());

    if (!(pSection = buffer.MapSeq(0, 0, dtMAP_WRITE)))
        goto test1_err;
    buffer.Commit(pSection, 0);

    if (pParams->pBufferClass && !strcmp(pParams->pBufferClass, "dtBufferStream"))
    {
        dtBufferStream* pStreamBuf = static_cast<dtBufferStream*>(&buffer);

        #ifdef bbDEBUG
        pStreamBuf->DebugCheck();
        pStreamBuf->DumpSegments();
        #endif
    }

    /*
    if ((err = buffer.Save(bbT("saved_as"))) != bbEOK)
    {
        printf("Error %d on buffer save as\n", err);
        goto test1_err;
    }
    */

    buffer.Close();

    return bbEOK;
    
    test1_err:
    if (buffer.IsOpen())
        buffer.Close();
    return bbELAST;
}

bbERR test2(Param* pParams, dtBuffer& buffer)
{
    int i;
    bbERR err;
    dtSection* pSection;

    printf("test2: random MapSeq\n");

    if ((err = buffer.Open(pParams->pFile)) != bbEOK)
    {
        printf("Error %d on buffer open for new file\n", err);
        goto test2_err;
    }

    for (i=0; i<10000; i++)
    {
        bbU64 offset = (rand()^(rand()<<16)) % buffer.GetSize();

        if (!(pSection = buffer.MapSeq(offset, 0, dtMAP_WRITE)))
            goto test2_err;

        buffer.Discard(pSection);
    }

    if (pParams->pBufferClass && !strcmp(pParams->pBufferClass, "dtBufferStream"))
    {
        dtBufferStream* pStreamBuf = static_cast<dtBufferStream*>(&buffer);
        #ifdef bbDEBUG
        pStreamBuf->DumpSegments();
        #endif
    }

    buffer.Close();
    return bbEOK;

    test2_err:
    if (buffer.IsOpen())
        buffer.Close();
    return bbELAST;
}

bbERR test3(Param* pParams, dtBuffer& buffer)
{
    bbU32 i, j;
    bbERR err;
    dtSection* pSection;

    printf("test3: sequential MapSeq\n");

    for (j=0; j<2; j++)
    {
        if ((err = buffer.Open(pParams->pFile)) != bbEOK)
        {
            printf("Error %d on buffer open for new file\n", err);
            goto test3_err;
        }

        bbU32 size = 1024*512 + 1;
        if (size > buffer.GetSize())
            size = (bbU32)buffer.GetSize();

        if (j==0)
        {
            printf("Forward...\n");
            for (i=0; i<size; ++i)
            {
                if (!(pSection = buffer.Map(i, 1, dtMAP_WRITE)))
                    goto test3_err;
                buffer.Discard(pSection);
            }
        }
        else
        {
            printf("Backward...\n");
            for (i=size-1; (bbS32)i>=0; --i)
            {
                if (!(pSection = buffer.Map(i, 1, dtMAP_WRITE)))
                    goto test3_err;
                buffer.Discard(pSection);
            }
        }

        if (pParams->pBufferClass && !strcmp(pParams->pBufferClass, "dtBufferStream"))
        {
            dtBufferStream* pStreamBuf = static_cast<dtBufferStream*>(&buffer);
            #ifdef bbDEBUG
            pStreamBuf->DumpSegments();
            #endif
        }

        buffer.Close();
    }

    return bbEOK;

    test3_err:
    if (buffer.IsOpen())
        buffer.Close();
    return bbELAST;
}

bbERR testLog(Param* pParams)
{
    return bbEOK;
}

bbERR ParseParams(int argc, char** argv, Param& params)
{
    for (int i=1; i<argc; i++)
    {
        if (*argv[i] == '-')
        {
            if (++i >= argc)
                return bbEBADPARAM;

            char option = argv[i-1][1] & 0xDFU;

            switch (option)
            {
            case 'L': params.pLogFile = bbStrConvMemFrom(bbCE_ISO8859_1, 0, (bbU8*)argv[i]); break;
            case 'T': params.pBufferClass = argv[i]; break;
            default:
                printf("Unknown option %c\n", option);
                return bbEBADPARAM;
            }
        }
        else
        {
            if (params.pFile)
            {
                printf("Unkown argument '%s'\n", argv[i]);
                return bbEBADPARAM;
            }

            params.pFile = bbStrConvMemFrom(bbCE_ISO8859_1, 0, (bbU8*)argv[i]);
        }
    }

    if (!params.pFile)
    {
        printf("No filename given\n");
        return bbEBADPARAM;
    }

    if (!params.pBufferClass)
    {
        params.pBufferClass = "dtBufferStream";
    }

    return bbEOK;
}

int main(int argc, char** argv)
{
    Param params;

    //
    // - Parse command line parameters
    //
    if (bbEOK != ParseParams(argc, argv, params))
    {
        syntax();
        return -1;
    }

    //
    // - Test
    //
    dtBuffer* pBuffer = NULL;

    if (!strcmp(params.pBufferClass, "dtBufferStream"))
        pBuffer = new dtBufferStream;

    printf("Testing buffer class '%s'\n", params.pBufferClass);

    if (!pBuffer)
    {
        printf("Cannot instanciate buffer\n");
        return bbEUK;
    }

    bbERR err = bbEOK;

    clock_t starttime, endtime;
    starttime = clock();

    if (params.pLogFile)
    {
        if (bbEOK != testLog(&params))
        {
            err = bbErrGet();
            printf("buffertest failed with code %d\n", err);
        }
    }
    else
    {
        if ((bbEOK != test1(&params, *pBuffer)) ||
            (bbEOK != test2(&params, *pBuffer)) ||
            (bbEOK != test3(&params, *pBuffer)))
        {
            err = bbErrGet();
            printf("buffertest failed with code %d\n", err);
        }
    }

    endtime = clock();
    printf("Running time: %f s\n", ((double)endtime - (double)starttime)/CLOCKS_PER_SEC);

    delete pBuffer;
    return err;
}

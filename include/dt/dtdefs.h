#ifndef dtDEFS_H_
#define dtDEFS_H_

#include <babel/defs.h>
#include <babel/Arr.h>

struct dtStream;
class  dtBuffer;
struct dtSegment;
struct dtPage;
struct dtBufferNotify;
class e7WinDbg;

bbDECLAREARRPTR(dtBufferNotify*, dtArrPBufferNotify);
bbDECLAREARRPTR(dtBuffer*, dtArrPBuffer);

enum dtERR
{
    dtENOCMD = bbEBASE_DT,  /**< Error code, no more commands in undo history */
};

/** Change IDs for dtBufferNotify::OnBufferChange */
enum dtCHANGE
{
    dtCHANGE_ALL = 0,       //!< Entire buffer contents changed
    dtCHANGE_INSERT,        //!< Data section was inserted
    dtCHANGE_DELETE,        //!< Data section was deleted
    dtCHANGE_OVERWRITE      //!< Data section was modified
};

/** Parameter struct for dtBufferNotify::OnBufferChange(). */
struct dtBufferChange
{
    bbU8     type;      //!< Type of change, see dtCHANGE
    bbU8     undo;      //!< 1 if change caused by undo, 2 by redo operation
    bbU8     undopoint; //!< !=0 if this change reached an undo point, valid only if \a undo != 0
    void*    user;      //!< User context
    bbU64    offset;    //!< Buffer offset of change
    bbU64    length;    //!< Byte length of change
};

#endif /* dtDEFS_H_ */


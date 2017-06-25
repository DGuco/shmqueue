#include "messagequeue.h"

#include <string.h>

BYTE* CMssageQueue::mpCurrAddr = nullptr;

CMssageQueue::CMssageQueue()
{
    miBegin = 0;
    miEnd = 0;
    miOffset = sizeof(this);
}

CMssageQueue::~CMssageQueue()
{

}

void *CMssageQueue::operator new(size_t size)
{
    return (void*)mpCurrAddr;
}

CMssageQueue* CMssageQueue::CreateInstance()
{
    return new CMssageQueue();
}
void CMssageQueue::SendMessage(char *message,int length)
{
    int nTempMaxLength = 0;
    int nTempRt = -1;
    BYTE *pbyCodeBuf;

    BYTE *pTempSrc = NULL;
    BYTE *pTempDst = NULL;
    unsigned int i;

    if( !message || length <= 0 )
    {
        return;
    }

    pbyCodeBuf = MessageBeginAddr();
    // 首先判断是否队列已满
    if(IsMemFull())		//if( m_stQueueHead.m_nFullFlag )
    {
        return;
    }

    nTempMaxLength  = GetFreeSize();

    nTempRt = miEnd;

    //空间不足
    if((length + sizeof(unsigned short) )> nTempMaxLength)
    {
        return;
    }

    unsigned short usInLength = (unsigned short) length;

    pTempDst = &pbyCodeBuf[0];
    pTempSrc = (BYTE*) (&usInLength);

    //写入的时候我们在数据头插上数据的长度，方便准确取数据
    for( i = 0; i < sizeof(usInLength); i++ )
    {
        pTempDst[miEnd] = pTempSrc[i];  // 拷贝 Code 的长度
        miEnd = (miEnd + 1) % GetDataMemSize();  // % 用于防止 Code 结尾的 idx 超出 codequeue
    }

    //空闲区在中间
    if( miBegin > miEnd )
    {
        memcpy((void *)&pbyCodeBuf[miEnd], (const void *)message, (size_t)usInLength );
    }
    else   //空闲区在两头
    {
        //尾部放不下需要分段拷贝的情况
        if( length > (GetDataMemSize() - miEnd) )
        {
            memcpy((void *)&pbyCodeBuf[miEnd], (const void *)&message[0], (size_t)(GetDataMemSize()- miEnd) );
            memcpy((void *)&pbyCodeBuf[0],(const void *)&message[(GetDataMemSize() - miEnd)],
                   (size_t)(length - (GetDataMemSize() - miEnd)));
        }
        else
        {
            memcpy((void *)&pbyCodeBuf[miEnd], (const void *)&message[0], (size_t)length);
        }
    }
    miEnd = (miEnd + length) % GetDataMemSize();
}

int CMssageQueue::GetMessage(BYTE *pOutCode, int *pOutLength)
{
    int nTempMaxLength = 0;
    int nTempRet = -1;
    BYTE *pTempSrc;
    BYTE *pTempDst;
    unsigned int i;
    BYTE *pbyCodeBuf;

    if( !pOutCode || !pOutLength )
    {
        return -1;
    }

    nTempMaxLength = GetDataSize();
    if (nTempMaxLength <= 0) {
        return -1;
    }

    pbyCodeBuf = MessageBeginAddr();

    nTempRet = miBegin;

    // 如果数据的最大长度不到2（存入数据时在数据头插入了数据的长度）
    if( nTempMaxLength < (int)sizeof(short) )
    {
        miBegin = miEnd;
        return -1;
    }

    unsigned short usOutLength;
    pTempDst = (BYTE *)&usOutLength;   // 数据拷贝的目的地址
    pTempSrc = (BYTE *)&pbyCodeBuf[0];  // 数据拷贝的源地址
    //取出数据的长度
    for( i = 0; i < sizeof(short); i++ )
    {
        pTempDst[i] = pTempSrc[miBegin];
        miBegin = (miBegin+1) % GetDataMemSize();
    }

    // 将数据长度回传
    *pOutLength = usOutLength;
    //数据的长度非法
    if(usOutLength > (int)(nTempMaxLength - sizeof(short)) || usOutLength < 0 )
    {
        miBegin = miEnd;
        return -1;
    }

    pTempDst = (BYTE *)&pOutCode[0];  // 设置接收 Code 的地址
    // 数据在中间
    if( miBegin < miEnd )
    {
        memcpy((void *)pTempDst, (const void *)&pTempSrc[miBegin], (size_t)(usOutLength));
    }
    else
    {
        if(GetDataMemSize() - miBegin < usOutLength)
        {
            memcpy((void *)pTempDst, (const void *)&pTempSrc[miBegin], (size_t)(GetDataMemSize() - miBegin));
            pTempDst += (GetDataMemSize() - miBegin);
            memcpy((void *)pTempDst, (const void *)&pTempSrc[0], (size_t)(usOutLength - (GetDataMemSize() - miBegin)));
        }
        else	// 否则，直接拷贝
        {
            memcpy((void *)pTempDst, (const void *)&pTempSrc[miBegin], (size_t)(usOutLength));
        }
    }
    miBegin = (miBegin + usOutLength) % GetDataMemSize();
    return usOutLength;
}

BYTE* CMssageQueue::MessageBeginAddr()
{
    return (BYTE*)(this + miOffset);
}


bool CMssageQueue::IsMemFull()
{
    return GetFreeSize() <= 0;
}

//获取空闲区大小
int CMssageQueue::GetFreeSize()
{
    int freesize = 0;
    //第一次写数据前
    if( miBegin == miEnd )
    {
        freesize = GetDataMemSize();
    }
        //数据在两头
    else if( miBegin > miEnd )
    {
        freesize = miBegin - miEnd;
    }
    else   //数据在中间
    {
        freesize = freesize - (miEnd - miBegin);
    }
    //长度应该减去预留部分长度8，保证首尾不会相接
    freesize -= EXTRA_BYTE;
    return freesize;
}

//获取数据长度
int CMssageQueue::GetDataSize()
{
    int freesize = GetDataMemSize();
    //第一次写数据前
    if( miBegin == miEnd )
    {
        return 0;
    }
        //数据在两头
    else if( miBegin > miEnd )
    {
        return freesize - (miBegin - miEnd);
    }
    else   //数据在中间
    {
        return  miEnd - miBegin;
    }
}

int CMssageQueue::GetDataMemSize()
{
    return (int)(MEM_SIZE - sizeof(this));
}


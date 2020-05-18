//
//  messagequeue_h
//
//  Created by 杜国超 on 17/6/22.
//  Copyright © 2017年 杜国超. All rights reserved.
//
#include <string.h>
#include <cstdlib>
#include <stdio.h>
#include <sys/shm.h>
#include <cmath>
#include "shmmqueue.h"

namespace shmmqueue
{
CMessageQueue::CMessageQueue(BYTE *pCurrAddr, eQueueModel module, key_t shmKey, int shmId, size_t size)
{
    m_pShm = (void*) pCurrAddr;
    m_pQueueAddr = pCurrAddr;
    m_stMemTrunk = new (m_pQueueAddr) stMemTrunk();
    m_pQueueAddr += sizeof(stMemTrunk);
    m_stMemTrunk->m_iBegin = 0;
    m_stMemTrunk->m_iEnd = 0;
    m_stMemTrunk->m_iShmKey = shmKey;
    m_stMemTrunk->m_iShmId = shmId;
    m_stMemTrunk->m_iSize = (unsigned int)size;
    m_stMemTrunk->m_eQueueModule = module;
    InitLock();
}

CMessageQueue::~CMessageQueue()
{
    if(m_stMemTrunk) {
        DestroyShareMem(m_pShm,m_stMemTrunk->m_iShmKey);
        m_stMemTrunk->~stMemTrunk();
    }
    if (m_pBeginLock) {
        delete m_pBeginLock;
        m_pBeginLock = NULL;
    }
    if (m_pEndLock) {
        delete m_pEndLock;
        m_pEndLock = NULL;
    }
}

int CMessageQueue::SendMessage(BYTE *message, MESS_SIZE_TYPE length)
{
    if (!message || length <= 0) {
        return (int) eQueueErrorCode::QUEUE_PARAM_ERROR;
    }

    CSafeShmWlock tmLock;
    //修改共享内存写锁
    if (IsEndLock() && m_pEndLock) {
        tmLock.InitLock(m_pEndLock);
    }

    // 首先判断是否队列已满
    int size = GetFreeSize();
    if (size <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_SPACE;
    }

    //空间不足
    if ((length + sizeof(MESS_SIZE_TYPE)) > size) {
        return (int) eQueueErrorCode::QUEUE_NO_SPACE;
    }

    MESS_SIZE_TYPE usInLength = length;
    BYTE *pTempDst = m_pQueueAddr;
    BYTE *pTempSrc = (BYTE *) (&usInLength);

    //写入的时候我们在数据头插上数据的长度，方便准确取数据,每次写入一个字节可能会分散在队列的头和尾
    unsigned int tmpEnd = m_stMemTrunk->m_iEnd;
    for (MESS_SIZE_TYPE i = 0; i < sizeof(usInLength); i++) {
        pTempDst[tmpEnd] = pTempSrc[i];  // 拷贝 Code 的长度
        tmpEnd = (tmpEnd + 1) & (m_stMemTrunk->m_iSize - 1);  // % 用于防止 Code 结尾的 idx 超出 codequeue
    }

    unsigned int tmpLen = SHM_MIN(usInLength, m_stMemTrunk->m_iSize - tmpEnd);
    memcpy((void *) (&pTempDst[tmpEnd]), (const void *) message, (size_t) tmpLen);
    size_t tmpLastLen = length - tmpLen;
    if(tmpLastLen > 0)
    {
        /* then put the rest (if any) at the beginning of the buffer */
        memcpy(&pTempDst[0], message + tmpLen, tmpLastLen);
    }

    /*
    * Ensure that we add the bytes to the kfifo -before-
    * we update the fifo->in index.
    * 数据写入完成修改m_iEnd，保证读端不会读到写入一半的数据
    */
    __WRITE_BARRIER__;
    m_stMemTrunk->m_iEnd = (tmpEnd + usInLength) & (m_stMemTrunk->m_iSize -1);
    return (int) eQueueErrorCode::QUEUE_OK;
}

int CMessageQueue::GetMessage(BYTE *pOutCode)
{
    if (!pOutCode) {
        return (int) eQueueErrorCode::QUEUE_PARAM_ERROR;
    }

    CSafeShmWlock tmLock;
    //修改共享内存写锁
    if (IsBeginLock() && m_pBeginLock) {
        tmLock.InitLock(m_pBeginLock);
    }

    int nTempMaxLength = GetDataSize();
    if (nTempMaxLength <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_MESSAGE;
    }

    BYTE *pTempSrc = m_pQueueAddr;
    // 如果数据的最大长度不到sizeof(MESS_SIZE_TYPE)（存入数据时在数据头插入了数据的长度,长度本身）
    if (nTempMaxLength <= (int) sizeof(MESS_SIZE_TYPE)) {
        printf("[%s:%d] ReadHeadMessage data len illegal,nTempMaxLength %d \n", __FILE__, __LINE__, nTempMaxLength);
        PrintTrunk();
        m_stMemTrunk->m_iBegin = m_stMemTrunk->m_iEnd;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    MESS_SIZE_TYPE usOutLength;
    BYTE *pTempDst = (BYTE *) &usOutLength;   // 数据拷贝的目的地址
    unsigned int tmpBegin = m_stMemTrunk->m_iBegin;
    //取出数据的长度
    for (MESS_SIZE_TYPE i = 0; i < sizeof(MESS_SIZE_TYPE); i++) {
        pTempDst[i] = pTempSrc[tmpBegin];
        tmpBegin = (tmpBegin + 1)  & (m_stMemTrunk->m_iSize -1);
    }

    // 将数据长度回传
    //取出的数据的长度实际有的数据长度，非法
    if (usOutLength > (int) (nTempMaxLength - sizeof(MESS_SIZE_TYPE)) || usOutLength < 0) {
        printf("[%s:%d] ReadHeadMessage usOutLength illegal,usOutLength: %d,nTempMaxLength %d \n",
               __FILE__, __LINE__, usOutLength, nTempMaxLength);
        PrintTrunk();
        m_stMemTrunk->m_iBegin = m_stMemTrunk->m_iEnd;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    pTempDst = &pOutCode[0];  // 设置接收 Code 的地址
    unsigned int tmpLen = SHM_MIN(usOutLength, m_stMemTrunk->m_iSize  - tmpBegin);
    memcpy(&pTempDst[0],&pTempSrc[tmpBegin], tmpLen);
    unsigned int tmpLast = usOutLength - tmpLen;
    if(tmpLast > 0)
    {
        memcpy(&pTempDst[tmpLen], pTempSrc, tmpLast);
    }

    __WRITE_BARRIER__;
    m_stMemTrunk->m_iBegin = (tmpBegin + usOutLength) & (m_stMemTrunk->m_iSize -1);
    return usOutLength;
}

/**
  *函数名          : PeekHeadCode
  *功能描述        : 查看共享内存管道（不改变读写索引）
  * Error code: -1 invalid para; -2 not enough; -3 data crashed
**/
int CMessageQueue::ReadHeadMessage(BYTE *pOutCode)
{
    if (!pOutCode) {
        return (int) eQueueErrorCode::QUEUE_PARAM_ERROR;
    }

    CSafeShmRlock tmLock;
    //修改共享内存写锁
    if (IsBeginLock() && m_pBeginLock) {
        tmLock.InitLock(m_pBeginLock);
    }

    int nTempMaxLength = GetDataSize();
    if (nTempMaxLength <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_MESSAGE;
    }

    BYTE *pTempSrc = m_pQueueAddr;
    // 如果数据的最大长度不到sizeof(MESS_SIZE_TYPE)（存入数据时在数据头插入了数据的长度,长度本身）
    if (nTempMaxLength <= (int) sizeof(MESS_SIZE_TYPE)) {
        printf("[%s:%d] ReadHeadMessage data len illegal,nTempMaxLength %d \n", __FILE__, __LINE__, nTempMaxLength);
        PrintTrunk();
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    MESS_SIZE_TYPE usOutLength;
    BYTE *pTempDst = (BYTE *) &usOutLength;   // 数据拷贝的目的地址
    unsigned int tmpBegin = m_stMemTrunk->m_iBegin;
    //取出数据的长度
    for (MESS_SIZE_TYPE i = 0; i < sizeof(MESS_SIZE_TYPE); i++) {
        pTempDst[i] = pTempSrc[tmpBegin];
        tmpBegin = (tmpBegin + 1)  & (m_stMemTrunk->m_iSize -1);
    }

    // 将数据长度回传
    //取出的数据的长度实际有的数据长度，非法
    if (usOutLength > (int) (nTempMaxLength - sizeof(MESS_SIZE_TYPE)) || usOutLength < 0) {
        printf("[%s:%d] ReadHeadMessage usOutLength illegal,usOutLength: %d,nTempMaxLength %d \n",
               __FILE__, __LINE__, usOutLength, nTempMaxLength);
        PrintTrunk();
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    pTempDst = &pOutCode[0];  // 设置接收 Code 的地址

    unsigned int tmpIndex = tmpBegin & (m_stMemTrunk->m_iSize - 1);
    unsigned int tmpLen = SHM_MIN(usOutLength, m_stMemTrunk->m_iSize  - tmpIndex);
    memcpy(pTempDst,pTempSrc+ tmpBegin, tmpLen);
    unsigned int tmpLast = usOutLength - tmpLen;
    if(tmpLast > 0)
    {
        memcpy(pTempDst + tmpLen, pTempSrc, tmpLast);
    }
    return usOutLength;
}

/**
  *函数名          : GetOneCode
  *功能描述        : 从指定位置iCodeOffset获取指定长度nCodeLength数据
  * */
int CMessageQueue::DeleteHeadMessage()
{
    CSafeShmWlock tmLock;
    //修改共享内存写锁
    if (IsBeginLock() && m_pBeginLock) {
        tmLock.InitLock(m_pBeginLock);
    }

    int nTempMaxLength = GetDataSize();
    if (nTempMaxLength <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_MESSAGE;
    }

    BYTE *pTempSrc = m_pQueueAddr;
    // 如果数据的最大长度不到sizeof(MESS_SIZE_TYPE)（存入数据时在数据头插入了数据的长度,长度本身）
    if (nTempMaxLength <= (int) sizeof(MESS_SIZE_TYPE)) {
        printf("[%s:%d] ReadHeadMessage data len illegal,nTempMaxLength %d \n", __FILE__, __LINE__, nTempMaxLength);
        PrintTrunk();
        m_stMemTrunk->m_iBegin = m_stMemTrunk->m_iEnd;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    MESS_SIZE_TYPE usOutLength;
    BYTE *pTempDst = (BYTE *) &usOutLength;   // 数据拷贝的目的地址
    unsigned int tmpBegin = m_stMemTrunk->m_iBegin;
    //取出数据的长度
    for (MESS_SIZE_TYPE i = 0; i < sizeof(MESS_SIZE_TYPE); i++) {
        pTempDst[i] = pTempSrc[tmpBegin];
        tmpBegin = (tmpBegin + 1)  & (m_stMemTrunk->m_iSize -1);
    }

    // 将数据长度回传
    //取出的数据的长度实际有的数据长度，非法
    if (usOutLength > (int) (nTempMaxLength - sizeof(MESS_SIZE_TYPE)) || usOutLength < 0) {
        printf("[%s:%d] ReadHeadMessage usOutLength illegal,usOutLength: %d,nTempMaxLength %d \n",
               __FILE__, __LINE__, usOutLength, nTempMaxLength);
        PrintTrunk();
        m_stMemTrunk->m_iBegin = m_stMemTrunk->m_iEnd;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    m_stMemTrunk->m_iBegin = (tmpBegin + usOutLength) & (m_stMemTrunk->m_iSize -1);
    return usOutLength;
}

void CMessageQueue::PrintTrunk()
{
    printf("Mem trunk address 0x%p,shmkey %d ,shmid %d, size %d, begin %d, end %d, queue module %d \n",
           m_stMemTrunk,
           m_stMemTrunk->m_iShmKey,
           m_stMemTrunk->m_iShmId,
           m_stMemTrunk->m_iSize,
           m_stMemTrunk->m_iBegin,
           m_stMemTrunk->m_iEnd,
           m_stMemTrunk->m_eQueueModule);
}

//获取空闲区大小
unsigned int CMessageQueue::GetFreeSize()
{
    //长度应该减去预留部分长度8，保证首尾不会相接
    return GetQueueLength() - GetDataSize() - EXTRA_BYTE;
}

//获取数据长度
unsigned int CMessageQueue::GetDataSize()
{
    //第一次写数据前
    if (m_stMemTrunk->m_iBegin == m_stMemTrunk->m_iEnd) {
        return 0;
    }
        //数据在两头
    else if (m_stMemTrunk->m_iBegin > m_stMemTrunk->m_iEnd) {

        return  (unsigned int)(m_stMemTrunk->m_iEnd + m_stMemTrunk->m_iSize  - m_stMemTrunk->m_iBegin);
    }
    else   //数据在中间
    {
        return m_stMemTrunk->m_iEnd - m_stMemTrunk->m_iBegin;
    }
}

unsigned int CMessageQueue::GetQueueLength()
{
    return (unsigned int) m_stMemTrunk->m_iSize;
}

void CMessageQueue::InitLock()
{
    if (IsBeginLock()) {
        m_pBeginLock = new CShmRWlock((key_t) (m_stMemTrunk->m_iShmKey + 1));
    }

    if (IsEndLock()) {
        m_pEndLock = new CShmRWlock((key_t) (m_stMemTrunk->m_iShmKey + 2));
    }
}

bool CMessageQueue::IsBeginLock()
{
    return (m_stMemTrunk->m_eQueueModule == eQueueModel::MUL_READ_MUL_WRITE ||
        m_stMemTrunk->m_eQueueModule == eQueueModel::MUL_READ_ONE_WRITE);
}

bool CMessageQueue::IsEndLock()
{
    return (m_stMemTrunk->m_eQueueModule == eQueueModel::MUL_READ_MUL_WRITE ||
        m_stMemTrunk->m_eQueueModule == eQueueModel::ONE_READ_MUL_WRITE);
}

/**
  *函数名          : CreateShareMem
  *功能描述        : 创建共享内存块
  *参数			 :  iKey：共享内存块唯一标识key vSize：大小
  *返回值         ： 共享内存块地址
**/
BYTE *CMessageQueue::CreateShareMem(key_t iKey, long vSize, enShmModule &shmModule,int& shmId)
{
    size_t iTempShmSize;

    if (iKey < 0) {
        printf("[%s:%d] CreateShareMem failed. [key %d]errno:%s \n", __FILE__, __LINE__, iKey,strerror(errno));
        exit(-1);
    }

    iTempShmSize = (size_t) vSize;
    printf("Try to malloc share memory of %d bytes... \n", iTempShmSize);
    shmId = shmget(iKey, iTempShmSize, IPC_CREAT | IPC_EXCL | 0666);
    if (shmId < 0) {
        if (errno != EEXIST) {
            printf("[%s:%d] Alloc share memory failed, [iKey:%d] , size:%d , error:%s \n",
                   __FILE__, __LINE__, iKey, iTempShmSize, strerror(errno));
            exit(-1);
        }

        printf("Same shm seg [key= %d] exist, now try to attach it... \n", iKey);
        shmId = shmget(iKey, iTempShmSize, IPC_CREAT | 0666);
        if (shmId < 0) {
            printf("Attach to share memory [key= %d,shmId %d] failed,maybe the size of share memory changed,%s .now try to touch it again \n",
                    iKey, shmId, strerror(errno));
            //先获取之前的shmId
            shmId = shmget(iKey, 0, 0666);
            if (shmId < 0) {
                printf("[%s:%d] Fatel error, touch to shm [key= %d,shmId %d] failed, %s.\n", __FILE__, __LINE__, iKey, shmId,strerror(errno));
                exit(-1);
            }
            else {
                //先删除之前的share memory
                printf("First remove the exist share memory [key= %d,shmId %d] \n", iKey,shmId);
                if (shmctl(shmId, IPC_RMID, NULL)) {
                    printf("[%s:%d] Remove share memory [key= %d,shmId %d] failed, %s \n", __FILE__, __LINE__, iKey,shmId,strerror(errno));
                    exit(-1);
                }
                //重新创建
                shmId = shmget(iKey, iTempShmSize, IPC_CREAT | IPC_EXCL | 0666);
                if (shmId < 0) {
                    printf("[%s:%d] Fatal error, alloc share memory [key= %d,shmId %d] failed, %s \n",
                           __FILE__, __LINE__, iKey,shmId,strerror(errno));
                    exit(-1);
                }
            }
        }
        else {
            shmModule = enShmModule::SHM_RESUME;
            printf("Attach to share memory [key= %d,shmId %d] succeed.\n",iKey,shmId);
        }
    }
    else {
        shmModule = enShmModule::SHM_INIT;
    }

    printf("Successfully alloced share memory block,[key= %d,shmId %d] size = %d \n", iKey, shmId, iTempShmSize);
    BYTE *tpShm = (BYTE *) shmat(shmId, NULL, 0);

    if ((void *) -1 == tpShm) {
        printf("[%s:%d] create share memory failed, shmat failed, [key= %d,shmId %d], error = %s. \n",
               __FILE__, __LINE__,iKey, shmId, strerror(errno));
        exit(0);
    }

    return tpShm;
}

/************************************************
  函数名          : DestroyShareMem
  功能描述        : 销毁共享内存块
  参数			:  iKey：共享内存块唯一标识key
  返回值         : 成功0 错误：错误码
************************************************/
int CMessageQueue::DestroyShareMem(const void *shmaddr,key_t iKey)
{
    int iShmID;

    if (iKey < 0) {
        printf("[%s:%d] Error in ftok, %s. \n", __FILE__, __LINE__, strerror(errno));
        return -1;
    }
    printf("Touch to share memory [key = %d]... \n", iKey);
    iShmID = shmget(iKey, 0, 0666);
    if (iShmID < 0) {
        printf("[%s:%d] Error, touch to shm [key= %d,shmId %d] failed, %s \n", __FILE__, __LINE__, iKey, iShmID, strerror(errno));
        return -1;
    }
    else {
        printf("Now disconnect the exist share memory [key= %d,shmId %d] \n",  iKey, iShmID);
        if(shmdt(shmaddr)){
            printf("[%s:%d] Disconnect share memory [key= %d,shmId %d] failed, %s \n", __FILE__, __LINE__,iKey, iShmID,strerror(errno));
        } else{
            printf("Disconnect the exist share memory [key= %d,shmId %d] succeed \n", iKey, iShmID);
        }
        printf("Now remove the exist share memory [key= %d,shmId %d] \n", iKey, iShmID);
        if (shmctl(iShmID, IPC_RMID, NULL)) {
            printf("[%s:%d] Remove share memory [key= %d,shmId %d] failed, %s \n", __FILE__, __LINE__, iKey, iShmID,strerror(errno));
            return -1;
        } else{
            printf("Remove shared memory [key= %d,shmId %d] succeed. \n", iShmID, iKey);
        }
    }
    return 0;
}

bool CMessageQueue::IsPowerOfTwo(size_t size) {
    if(size < 1)
    {
        return false;//2的次幂一定大于0
    }
    return ((size & (size -1)) == 0);
}


int CMessageQueue::Fls(size_t size) {
    int position;
    int i;
    if(0 != size)
    {
        for (i = (size >> 1), position = 0; i != 0; ++position)
            i >>= 1;
    }
    else
    {
        position = -1;
    }
    return position + 1;
}

size_t CMessageQueue::RoundupPowofTwo(size_t size) {
    return 1UL << Fls(size - 1);
}

CMessageQueue *CMessageQueue::CreateInstance(key_t shmkey,
                                             size_t queuesize,
                                             eQueueModel queueModule)
{
    if(queuesize <= 0)
    {
        return NULL;
    }

    queuesize = IsPowerOfTwo(queuesize) ? queuesize : RoundupPowofTwo(queuesize);
    if(queuesize <= 0) {
        return NULL;
    }
    enShmModule shmModule;
    int shmId = 0;
    BYTE * tmpMem = CMessageQueue::CreateShareMem(shmkey, queuesize + sizeof(stMemTrunk), shmModule,shmId);
    CMessageQueue *messageQueue = new CMessageQueue(tmpMem,queueModule, shmkey,shmId, queuesize);
    messageQueue->PrintTrunk();
    return messageQueue;
}

}


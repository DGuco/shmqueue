//
//  messagequeue_h
//
//  Created by 杜国超 on 17/6/22.
//  Copyright © 2017年 杜国超. All rights reserved.
//
#include <string.h>
#include <cstdlib>
#include <stdio.h>
#include <memory>
#include <sys/shm.h>
#include "shmmqueue.h"

namespace shmmqueue
{
BYTE *CMessageQueue::m_pCurrAddr = nullptr;

CMessageQueue::CMessageQueue()
{
    m_stMemTrunk = new stMemTrunk();
    InitLock();
}

CMessageQueue::CMessageQueue(eQueueModule module, key_t shmid, size_t size)
{
    m_stMemTrunk = new stMemTrunk();
    m_stMemTrunk->m_iBegin = 0;
    m_stMemTrunk->m_iEnd = 0;
    m_stMemTrunk->m_iKey = shmid;
    m_stMemTrunk->m_iSize = (int) size;
    m_stMemTrunk->m_eQueueModule = module;
    InitLock();
}

CMessageQueue::~CMessageQueue()
{
    if (m_pReadLock) {
        delete m_pReadLock;
        m_pReadLock = nullptr;
    }
    if (m_pWriteLock) {
        delete m_pWriteLock;
        m_pWriteLock = nullptr;
    }
}

int CMessageQueue::SendMessage(BYTE *message, MESS_SIZE_TYPE length)
{
    if (!message || length <= 0) {
        return (int) eQueueErrorCode::QUEUE_PARAM_ERROR;
    }


    std::shared_ptr<CSafeShmWlock> lock = nullptr;
    //修改共享内存写锁
    if (IsWriteLock()) {
        lock.reset(new CSafeShmWlock(m_pWriteLock));
    }

    // 首先判断是否队列已满
    int size = GetFreeSize();
    if (size <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_SPACE;;
    }

    //空间不足
    if ((length + sizeof(MESS_SIZE_TYPE)) > size) {
        return (int) eQueueErrorCode::QUEUE_NO_SPACE;
    }

    MESS_SIZE_TYPE usInLength = length;

    BYTE *pbyCodeBuf = QueueBeginAddr();
    BYTE *pTempDst = &pbyCodeBuf[0];
    BYTE *pTempSrc = (BYTE *) (&usInLength);

    //写入的时候我们在数据头插上数据的长度，方便准确取数据
    long long tmpEnd = m_stMemTrunk->m_iEnd;
    for (MESS_SIZE_TYPE i = 0; i < sizeof(usInLength); i++) {
        pTempDst[tmpEnd] = pTempSrc[i];  // 拷贝 Code 的长度
        tmpEnd = (tmpEnd + 1) % GetQueueLength();  // % 用于防止 Code 结尾的 idx 超出 codequeue
    }

    //空闲区在中间
    if (m_stMemTrunk->m_iBegin > tmpEnd) {
        memcpy((void *) &pbyCodeBuf[tmpEnd], (const void *) message, (size_t) usInLength);
    }
    else   //空闲区在两头
    {
        //队列尾部剩余长度
        unsigned long endLen = QueueEndAddr() - (QueueBeginAddr() + tmpEnd);
        //尾部放不下需要分段拷贝的情况
        if (length > endLen) {
            memcpy((void *) &pbyCodeBuf[tmpEnd], (const void *) &message[0], endLen);
            memcpy((void *) &pbyCodeBuf[0], (const void *) &message[endLen], (size_t) (length - endLen));
        }
        else {
            memcpy((void *) &pbyCodeBuf[tmpEnd], (const void *) &message[0], (size_t) length);
        }
    }
    //数据写入完成修改m_iEnd，保证读端不会读到写入一半的数据
    m_stMemTrunk->m_iEnd = (tmpEnd + length) % GetQueueLength();
    return (int) eQueueErrorCode::QUEUE_OK;
}

int CMessageQueue::GetMessage(BYTE *pOutCode)
{
    if (!pOutCode) {
        return (int) eQueueErrorCode::QUEUE_PARAM_ERROR;
    }

    std::shared_ptr<CSafeShmWlock> lock = nullptr;
    //修改共享内存写锁
    if (IsReadLock()) {
        lock.reset(new CSafeShmWlock(m_pReadLock));
    }

    int nTempMaxLength = GetDataSize();
    if (nTempMaxLength <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_MESSAGE;
    }

    BYTE *pbyCodeBuf = QueueBeginAddr();
    // 如果数据的最大长度不到2（存入数据时在数据头插入了数据的长度,长度本身）
    if (nTempMaxLength <= (int) sizeof(MESS_SIZE_TYPE)) {
        printf("[%s:%d] ReadHeadMessage data len illegal,nTempMaxLength %d \n", __FILE__, __LINE__, nTempMaxLength);
        PrintfTrunk();
        m_stMemTrunk->m_iBegin = m_stMemTrunk->m_iEnd;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    MESS_SIZE_TYPE usOutLength;
    BYTE *pTempDst = (BYTE *) &usOutLength;   // 数据拷贝的目的地址
    BYTE *pTempSrc = &pbyCodeBuf[0];  // 数据拷贝的源地址
    //取出数据的长度
    for (MESS_SIZE_TYPE i = 0; i < sizeof(short); i++) {
        pTempDst[i] = pTempSrc[m_stMemTrunk->m_iBegin];
        m_stMemTrunk->m_iBegin = (m_stMemTrunk->m_iBegin + 1) % GetQueueLength();
    }

    // 将数据长度回传
    //取出的数据的长度实际有的数据长度，非法
    if (usOutLength > (int) (nTempMaxLength - sizeof(MESS_SIZE_TYPE)) || usOutLength < 0) {
        printf("[%s:%d] ReadHeadMessage usOutLength illegal,usOutLength: %d,nTempMaxLength %d \n",
               __FILE__, __LINE__, usOutLength, nTempMaxLength);
        PrintfTrunk();
        m_stMemTrunk->m_iBegin = m_stMemTrunk->m_iEnd;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    pTempDst = &pOutCode[0];  // 设置接收 Code 的地址
    // 数据在中间
    if (m_stMemTrunk->m_iBegin < m_stMemTrunk->m_iEnd) {
        memcpy((void *) pTempDst, (const void *) &pTempSrc[m_stMemTrunk->m_iBegin], (size_t) (usOutLength));
    }
    else {
        //尾部数据长度
        unsigned long endLen = QueueEndAddr() - MessageBeginAddr();
        //次数据包分布在队列的两端
        if (endLen < usOutLength) {
            memcpy((void *) pTempDst, (const void *) &pTempSrc[m_stMemTrunk->m_iBegin], (size_t) endLen);
            pTempDst += endLen;
            memcpy((void *) pTempDst, (const void *) &pTempSrc[0], (size_t) (usOutLength - endLen));
        }
        else  // 否则，直接拷贝
        {
            memcpy((void *) pTempDst, (const void *) &pTempSrc[m_stMemTrunk->m_iBegin], (size_t) (usOutLength));
        }
    }
    m_stMemTrunk->m_iBegin = (m_stMemTrunk->m_iBegin + usOutLength) % GetQueueLength();
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

    std::shared_ptr<CSafeShmRlock> lock = nullptr;
    //修改共享内存写锁
    if (IsReadLock()) {
        lock.reset(new CSafeShmRlock(m_pReadLock));
    }

    int nTempMaxLength = GetDataSize();
    if (nTempMaxLength <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_MESSAGE;
    }

    BYTE *pbyCodeBuf = QueueBeginAddr();
    // 如果数据的最大长度不到2（存入数据时在数据头插入了数据的长度,长度本身）
    if (nTempMaxLength < (int) sizeof(MESS_SIZE_TYPE)) {
        printf("[%s:%d] ReadHeadMessage data len illegal,nTempMaxLength %d \n", __FILE__, __LINE__, nTempMaxLength);
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    MESS_SIZE_TYPE usOutLength;
    BYTE *pTempDst = (BYTE *) &usOutLength;   // 数据拷贝的目的地址
    BYTE *pTempSrc = &pbyCodeBuf[0];  // 数据拷贝的源地址
    //取出数据的长度
    long long tmpBegin = m_stMemTrunk->m_iBegin;
    for (MESS_SIZE_TYPE i = 0; i < sizeof(short); i++) {
        pTempDst[i] = pTempSrc[tmpBegin];
        tmpBegin = (tmpBegin + 1) % GetQueueLength();
    }

    // 将数据长度回传
    //取出的数据的长度实际有的数据长度，非法
    if (usOutLength > (int) (nTempMaxLength - sizeof(MESS_SIZE_TYPE)) || usOutLength < 0) {
        printf("[%s:%d] ReadHeadMessage usOutLength illegal,usOutLength: %d,nTempMaxLength %d \n",
               __FILE__, __LINE__, usOutLength, nTempMaxLength);
        PrintfTrunk();
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    pTempDst = &pOutCode[0];  // 设置接收 Code 的地址
    // 数据在中间
    if (tmpBegin < m_stMemTrunk->m_iEnd) {
        memcpy((void *) pTempDst, (const void *) &pTempSrc[tmpBegin], (size_t) (usOutLength));
    }
    else {
        //尾部数据长度
        unsigned long endLen = QueueEndAddr() - MessageBeginAddr();
        //次数据包分布在队列的两端
        if (endLen < usOutLength) {
            memcpy((void *) pTempDst, (const void *) &pTempSrc[tmpBegin], (size_t) endLen);
            pTempDst += endLen;
            memcpy((void *) pTempDst, (const void *) &pTempSrc[0], (size_t) (usOutLength - endLen));
        }
        else  // 否则，直接拷贝
        {
            memcpy((void *) pTempDst, (const void *) &pTempSrc[tmpBegin], (size_t) (usOutLength));
        }
    }
    return usOutLength;
}

/**
  *函数名          : GetOneCode
  *功能描述        : 从指定位置iCodeOffset获取指定长度nCodeLength数据
  * */
int CMessageQueue::DeleteHeadMessage()
{
    std::shared_ptr<CSafeShmWlock> lock = nullptr;
    //修改共享内存写锁
    if (IsReadLock()) {
        lock.reset(new CSafeShmWlock(m_pReadLock));
    }

    int nTempMaxLength = GetDataSize();
    if (nTempMaxLength <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_MESSAGE;
    }

    BYTE *pbyCodeBuf = QueueBeginAddr();
    // 如果数据的最大长度不到2（存入数据时在数据头插入了数据的长度,长度本身）
    if (nTempMaxLength < (int) sizeof(MESS_SIZE_TYPE)) {
        printf("[%s:%d] ReadHeadMessage data len illegal,nTempMaxLength %d \n", __FILE__, __LINE__, nTempMaxLength);
        PrintfTrunk();
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    MESS_SIZE_TYPE usOutLength;
    BYTE *pTempDst = (BYTE *) &usOutLength;   // 数据拷贝的目的地址
    BYTE *pTempSrc = &pbyCodeBuf[0];  // 数据拷贝的源地址
    //取出数据的长度
    for (MESS_SIZE_TYPE i = 0; i < sizeof(short); i++) {
        pTempDst[i] = pTempSrc[m_stMemTrunk->m_iBegin];
        m_stMemTrunk->m_iBegin = (m_stMemTrunk->m_iBegin + 1) % GetQueueLength();
    }

    // 将数据长度回传
    //数据的长度非法
    if (usOutLength > (int) (nTempMaxLength - sizeof(MESS_SIZE_TYPE)) || usOutLength < 0) {
        m_stMemTrunk->m_iBegin = m_stMemTrunk->m_iEnd;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }

    m_stMemTrunk->m_iBegin = (m_stMemTrunk->m_iBegin + usOutLength) % GetQueueLength();
    return usOutLength;
}

void CMessageQueue::PrintfTrunk()
{
    printf("Mem trunk address 0x%p,key %lli , size %lli, begin %lli, end %lli, queue module %d \n",
           m_stMemTrunk,
           m_stMemTrunk->m_iKey,
           m_stMemTrunk->m_iSize,
           m_stMemTrunk->m_iBegin,
           m_stMemTrunk->m_iEnd,
           m_stMemTrunk->m_eQueueModule);
}

BYTE *CMessageQueue::QueueBeginAddr()
{
    return m_pCurrAddr + sizeof(stMemTrunk);
}

BYTE *CMessageQueue::QueueEndAddr()
{
    return m_pCurrAddr + m_stMemTrunk->m_iSize;
}

BYTE *CMessageQueue::MessageBeginAddr()
{
    return QueueBeginAddr() + m_stMemTrunk->m_iBegin;
}

BYTE *CMessageQueue::MessageEndAddr()
{
    return QueueBeginAddr() + m_stMemTrunk->m_iEnd;
}

//获取空闲区大小
int CMessageQueue::GetFreeSize()
{
    //长度应该减去预留部分长度8，保证首尾不会相接
    return GetQueueLength() - GetDataSize() - EXTRA_BYTE;
}

//获取数据长度
int CMessageQueue::GetDataSize()
{
    //第一次写数据前
    if (m_stMemTrunk->m_iBegin == m_stMemTrunk->m_iEnd) {
        return 0;
    }
        //数据在两头
    else if (m_stMemTrunk->m_iBegin > m_stMemTrunk->m_iEnd) {
        return (int) (m_stMemTrunk->m_iEnd + (QueueEndAddr() - MessageBeginAddr()));
    }
    else   //数据在中间
    {
        return (int) (m_stMemTrunk->m_iEnd - m_stMemTrunk->m_iBegin);
    }
}

int CMessageQueue::GetQueueLength()
{
    return (int) (m_stMemTrunk->m_iSize - sizeof(stMemTrunk));
}

void CMessageQueue::InitLock()
{
    if (IsReadLock()) {
        m_pReadLock = new CShmRWlock((key_t) (m_stMemTrunk->m_iKey + 1));
    }

    if (IsWriteLock()) {
        m_pWriteLock = new CShmRWlock((key_t) (m_stMemTrunk->m_iKey + 2));
    }
}

bool CMessageQueue::IsReadLock()
{
    return (m_stMemTrunk->m_eQueueModule == eQueueModule::MUL_READ_MUL_WRITE ||
        m_stMemTrunk->m_eQueueModule == eQueueModule::MUL_READ_ONE_WRITE);
}

bool CMessageQueue::IsWriteLock()
{
    return (m_stMemTrunk->m_eQueueModule == eQueueModule::MUL_READ_MUL_WRITE ||
        m_stMemTrunk->m_eQueueModule == eQueueModule::ONE_READ_MUL_WRITE);
}

/**
  *函数名          : CreateShareMem
  *功能描述        : 创建共享内存块
  *参数			 :  iKey：共享内存块唯一标识key vSize：大小
  *返回值         ： 共享内存块地址
**/
BYTE *CMessageQueue::CreateShareMem(key_t iKey, long vSize, enShmModule &shmModule, int &shmId)
{
    size_t iTempShmSize;

    if (iKey < 0) {
        printf("[%s:%d] CreateShareMem failed. errno:%s \n", __FILE__, __LINE__, strerror(errno));
        exit(-1);
    }

    iTempShmSize = (size_t) vSize;
    //iTempShmSize += sizeof(CSharedMem);

    printf("Try to malloc share memory of %d bytes... \n", iTempShmSize);

    shmId = shmget(iKey, iTempShmSize, IPC_CREAT | IPC_EXCL | 0666);
    if (shmId < 0) {
        if (errno != EEXIST) {
            printf("[%s:%d] Alloc share memory failed, iKey:%d , size:%d , error:%s \n",
                   __FILE__, __LINE__, iKey, iTempShmSize, strerror(errno));
            exit(-1);
        }

        printf("Same shm seg (key= %d ) exist, now try to attach it... \n", iKey);

        shmId = shmget(iKey, iTempShmSize, 0666);
        if (shmId < 0) {
            printf("Attach to share memory %d  failed, %s . Now try to touch it \n", shmId, strerror(errno));
            shmId = shmget(iKey, 0, 0666);
            if (shmId < 0) {
                printf("[%s:%d] Fatel error, touch to shm failed, %s.\n", __FILE__, __LINE__, strerror(errno));
                exit(-1);
            }
            else {
                printf("First remove the exist share memory %d \n", shmId);
                if (shmctl(shmId, IPC_RMID, NULL)) {
                    printf("[%s:%d] Remove share memory failed, %s \n", __FILE__, __LINE__, strerror(errno));
                    exit(-1);
                }
                shmId = shmget(iKey, iTempShmSize, IPC_CREAT | IPC_EXCL | 0666);
                if (shmId < 0) {
                    printf("[%s:%d] Fatal error, alloc share memory failed, %s \n",
                           __FILE__, __LINE__, strerror(errno));
                    exit(-1);
                }
            }
        }
        else {
            shmModule = enShmModule::SHM_RESUME;
            printf("Attach to share memory succeed.\n");
        }
    }
    else {
        shmModule = enShmModule::SHM_INIT;
    }

    printf("Successfully alloced share memory block, (key=%d), id = %d, size = %d \n", iKey, shmId, iTempShmSize);
    BYTE *tpShm = (BYTE *) shmat(shmId, NULL, 0);

    if ((void *) -1 == tpShm) {
        printf("[%s:%d] create share memory failed, shmat failed, iShmId = %d, error = %s. \n",
               __FILE__, __LINE__, shmId, strerror(errno));
        exit(0);
    }

    return tpShm;
}

/************************************************
  函数名          : DestroyShareMem
  功能描述        : 销毁共享内存块
  参数			 :  iKey：共享内存块唯一标识key
  返回值         ： 成功0 错误：错误码
************************************************/
int CMessageQueue::DestroyShareMem(key_t iKey)
{
    int iShmID;

    if (iKey < 0) {
        printf("[%s:%d] Error in ftok, %s. \n", __FILE__, __LINE__, strerror(errno));
        return -1;
    }
    printf("Touch to share memory key = %d... \n", iKey);
    iShmID = shmget(iKey, 0, 0666);
    if (iShmID < 0) {
        printf("[%s:%d] Error, touch to shm failed, %s \n", __FILE__, __LINE__, strerror(errno));
        return -1;
    }
    else {
        printf("Now remove the exist share memory %d \n", iShmID);
        if (shmctl(iShmID, IPC_RMID, NULL)) {
            printf("[%s:%d] Remove share memory failed, %s \n", __FILE__, __LINE__, strerror(errno));
            return -1;
        }
        printf("Remove shared memory(id = %d, key = %d) succeed. \n", iShmID, iKey);
    }
    return 0;
}

CMessageQueue *CMessageQueue::CreateInstance(key_t shmkey,
                                             size_t queuesize,
                                             eQueueModule queueModule)
{
    enShmModule shmModule;
    int shmId = 0;
    CMessageQueue::m_pCurrAddr = CMessageQueue::CreateShareMem(shmkey, queuesize, shmModule, shmId);
    CMessageQueue *messageQueue;
    if (shmModule == enShmModule::SHM_INIT) {
        messageQueue = new CMessageQueue(queueModule, shmId, queuesize);
    }
    else {
        messageQueue = new CMessageQueue();
    }
    messageQueue->PrintfTrunk();
    return messageQueue;
}

}


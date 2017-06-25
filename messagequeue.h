#ifndef messagequeue_h
#define messagequeue_h

#include <iostream>

#define MEM_SIZE  1024
#define EXTRA_BYTE 8


typedef unsigned char BYTE;
class CMssageQueue
{
protected:
    CMssageQueue();
public:
    ~CMssageQueue();
    void* operator new(size_t size);
    static CMssageQueue* CreateInstance();
    //添加消息
    void SendMessage(char *message,int length);
    //获取消息
    int GetMessage(BYTE *pOutCode, int *psOutLength);
    //获取消息数据在对象中的开始位置
    BYTE* MessageBeginAddr();
    //消息存储区是否已满
    bool IsMemFull();
    //获取空闲区大小
    int GetFreeSize();
    //获取数据长度
    int GetDataSize();
    //获取存储数据的内存取长度（空闲的和占用的）
    int GetDataMemSize();

private:
    int miBegin;
    int miEnd;
    int miOffset;
public:
    static BYTE* mpCurrAddr;

};

#endif /* messagequeue_h */

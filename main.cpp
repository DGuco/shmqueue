//
//  main.cpp
//  Demo
//
//  Created by 杜国超 on 17/6/22.
//  Copyright © 2017年 杜国超. All rights reserved.
//
#include <iostream>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <list>
#include "shmmqueue.h"

using namespace shmmqueue;

#define SHAR_ID_1 1234
#define SHAR_ID_2 12345

using namespace std;

std::atomic_int read_count;

std::atomic_int write_count;

int read_i = 0;

int write_i = 0;

void read_func(CMessageQueue *writeQueue, int threadId, const char *mes)
{
    while (1) {
        BYTE data[100] = {0};
        int len = writeQueue->GetMessage(data);
        if (len > 0) {
//            printf("read : %s,i = %d ", data, read_i);
//            if (read_i % 10 == 0) {
//                printf("\n");
//            }
            int i = atoi((const char *) data);
            if (i != read_i && i != -1) {
                printf("Read sequence error,i = %d,data = %s \n", read_i, data);
                writeQueue->PrintfTrunk();
                exit(-1);
            }
            if (i == -1) {
                break;
            }
            read_i++;
        }
        else {
            if (len != (int) eQueueErrorCode::QUEUE_NO_MESSAGE) {
                printf("Read failed ret = %d\n", len);
                writeQueue->PrintfTrunk();
            }
        }
//        usleep(1);
    }
    printf("\nRead %s ,thread %d ,read count %d\n", mes, threadId, read_i);

}

void write_func(CMessageQueue *writeQueue, int threadId, const char *mes)
{
    while (1) {
        if (write_i >= 100000) {
            break;
        }
        const string &data = to_string(write_i);
        int iRet = writeQueue->SendMessage((BYTE *) data.c_str(), data.length());
        if (iRet < 0 && iRet != (int) eQueueErrorCode::QUEUE_NO_SPACE) {
            printf("Write failed ret = %d\n", iRet);
            writeQueue->PrintfTrunk();
        }
        else {
            write_i++;
        }
//        usleep(1);
    }
    const string &data = to_string(-1);
    writeQueue->SendMessage((BYTE *) data.c_str(), data.length());
    printf("Write  %s thread %d ,write count %d\n", mes, threadId, write_i);
}

void mul_read_func(CMessageQueue *writeQueue, int threadId, const char *mes)
{
    int i = 0;
    while (1) {
        BYTE data[100] = {0};
        int len = writeQueue->GetMessage(data);
        if (len > 0) {
//            printf("read : %s,i = %d ", data, i);
//            if (i % 10 == 0) {
//                printf("\n");
//            }
            i++;
            read_count++;
        }
        else {
            if (len != (int) eQueueErrorCode::QUEUE_NO_MESSAGE) {
                printf("Read failed ret = %d\n", len);
                writeQueue->PrintfTrunk();
            }

        }
        if (read_count > 500000) {
            break;
        }

        usleep(1);
    }
    printf("\nRead %s ,thread %d ,read count %d\n", mes, threadId, i - 1);

}

void mul_write_func(CMessageQueue *writeQueue, int threadId, const char *mes)
{
    int i = 0;
    while (1) {
        const string &data = to_string(i);
        int iRet = writeQueue->SendMessage((BYTE *) data.c_str(), data.length());
        if (iRet < 0) {
            printf("Write failed ret = %d\n", iRet);
            writeQueue->PrintfTrunk();
        }
        else {
            i++;
            if (i > 100000) {
                break;
            }
            write_count++;
        }
        usleep(1);
    }
    printf("Write  %s thread %d ,write count %d\n", mes, threadId, i - 1);
}

void MulRWTest()
{
    CMessageQueue *messQueue = CMessageQueue::CreateInstance(SHAR_ID_1, 10240, eQueueModule::MUL_READ_MUL_WRITE);
    read_count.store(0);
    write_count.store(0);
    list<thread> read;
    for (int i = 0; i < 5; i++) {
        read.push_back(move(thread(mul_read_func, messQueue, i, "MulRWTest")));
    }
    list<thread> write;
    for (int i = 0; i < 5; i++) {
        write.push_back(move(thread(mul_write_func, messQueue, i, "MulRWTest")));
    }

    for (thread &thread : write) {
        thread.join();
    }

    for (thread &thread : read) {
        thread.join();
    }

    messQueue->DestroyShareMem(SHAR_ID_1);
    printf("=======================MulRWTest===============================\n");
    printf("Read read_count %d \n", read_count.load());
    printf("Write write_count %d \n", write_count.load());
    if (read_count.load() == write_count.load()) {
        printf("MulRWTest ok %d \n");
    }
    else {
        printf("MulRWTest failed %d \n");
    }
}

void SingleRWTest()
{
    CMessageQueue *messQueue = CMessageQueue::CreateInstance(SHAR_ID_2, 10240, eQueueModule::ONE_READ_ONE_WRITE);
    thread read_thread(read_func, messQueue, 1, "SingleRWTest");
    thread write_thread(write_func, messQueue, 1, "SingleRWTest");
    read_thread.join();
    write_thread.join();
    messQueue->DestroyShareMem(SHAR_ID_2);
    printf("=======================SingleRWTest===============================\n");
    printf("Read read_count %d \n", read_i);
    printf("Write write_count %d \n", write_i);
    if (read_i == write_i) {
        printf("SingleRWTest ok %d \n");
    }
    else {
        printf("SingleRWTest failed %d \n");
    }
}

int main(int argc, const char *argv[])
{
    SingleRWTest();
//    MulRWTest();
}

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

#define SHAR_KEY_1 100010
#define SHAR_KEY_2 100020

using namespace std;

std::atomic_int read_count;

std::atomic_int write_count;

int read_i = 0;

int write_i = 0;

atomic_bool done_flag;

#define SING_TEST_NUM 1000000
#define  THREAD_NUM 5
#define THREAD_SEND_NUM 100000

long long getCurrentTime()
{
    auto time_now = chrono::system_clock::now();
    auto duration_in_ms = chrono::duration_cast<chrono::milliseconds>(time_now.time_since_epoch());
    return duration_in_ms.count();
}

void read_func(CMessageQueue *writeQueue, int threadId, const char *mes)
{
    while (1) {
        BYTE data[100] = {0};
        int len = writeQueue->GetMessage(data);
        if (len > 0) {
            int i = atoi((const char *) data);
            if (i != read_i && i != -1) {
                printf("------------Read sequence error,i = %d,read = %s ------------\n", read_i, data);
                writeQueue->PrintTrunk();
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
                writeQueue->PrintTrunk();
                exit(-1);
            }
        }
    }
    printf("Read %s ,thread %d ,read count %d\n", mes, threadId, read_i);

}

void write_func(CMessageQueue *writeQueue, int threadId, const char *mes)
{
    while (1) {
        if (write_i >= SING_TEST_NUM) {
            break;
        }
        const string &data = to_string(write_i);
        int iRet = writeQueue->SendMessage((BYTE *) data.c_str(), data.length());
        if (iRet == 0) {
            write_i++;
        }
        else {
            if (iRet != (int) eQueueErrorCode::QUEUE_NO_SPACE) {
                printf("Write failed data = %d,ret = %d\n", write_i, iRet);
                writeQueue->PrintTrunk();
                exit(-1);
            }
        }

    }

    //over
    while (true) {
        const string &data = to_string(-1);
        int iRet = writeQueue->SendMessage((BYTE *) data.c_str(), data.length());
        if (iRet == 0) {
            break;
        }
    }
    printf("Write  %s thread %d ,write count %d\n", mes, threadId, write_i);
}

void mul_read_func(CMessageQueue *writeQueue, int threadId, const char *mes)
{
    int i = 1;
    while (1) {
        BYTE data[100] = {0};
        int len = writeQueue->GetMessage(data);
        if (len > 0) {
            i++;
            read_count++;
        }
        else {
            if (done_flag) {
                break;
            }
            if (len != (int) eQueueErrorCode::QUEUE_NO_MESSAGE) {
                printf("Read failed ret = %d\n", len);
                writeQueue->PrintTrunk();
                exit(-1);
            }
        }
    }
    printf("Read %s ,thread %d ,read count %d\n", mes, threadId, i);

}

void mul_write_func(CMessageQueue *writeQueue, int threadId, const char *mes)
{
    int i = 0;
    while (1) {
        if (i >= THREAD_SEND_NUM) {
            break;
        }
        const string &data = to_string(i);
        int iRet = writeQueue->SendMessage((BYTE *) data.c_str(), data.length());
        if (iRet == 0) {
            i++;
            write_count++;
            if (write_count >= THREAD_SEND_NUM * THREAD_NUM) {
                done_flag.store(true);
            }
        }
        else {
            if (iRet != (int) eQueueErrorCode::QUEUE_NO_SPACE) {
                printf("Write failed data = %d,ret = %d\n", write_i, iRet);
                writeQueue->PrintTrunk();
                exit(-1);
            }
        }
    }
    printf("Write  %s thread %d ,write count %d\n", mes, threadId, i);
}

void SingleRWTest()
{
    CMessageQueue *messQueue = CMessageQueue::CreateInstance(SHAR_KEY_2, 10240, eQueueModel::ONE_READ_ONE_WRITE);
    long begin = getCurrentTime();
    thread read_thread(read_func, messQueue, 1, "SingleRWTest");
    thread write_thread(write_func, messQueue, 1, "SingleRWTest");
    read_thread.join();
    write_thread.join();
    long end = getCurrentTime();
    printf("=======================SingleRWTest=============================\n");
    printf("SingleRWTest cost time %d ms\n", (int) (end - begin));
    printf("Read read_count %d \n", read_i);
    printf("Write write_count %d \n", write_i);
    if (read_i == write_i) {
        printf("SingleRWTest ok %d \n");
    }
    else {
        printf("SingleRWTest failed %d \n");
    }
    delete  messQueue;
    messQueue = nullptr;
}

void MulRWTest()
{
    CMessageQueue *messQueue = CMessageQueue::CreateInstance(SHAR_KEY_1, 10240, eQueueModel::MUL_READ_MUL_WRITE);
    read_count.store(0);
    write_count.store(0);
    done_flag.store(false);
    printf("===============================================================\n");
    long begin = getCurrentTime();
    list<thread> read;
    for (int i = 0; i < THREAD_NUM; i++) {
        read.push_back(move(thread(mul_read_func, messQueue, i, "MulRWTest")));
    }
    list<thread> write;
    for (int i = 0; i < THREAD_NUM; i++) {
        write.push_back(move(thread(mul_write_func, messQueue, i, "MulRWTest")));
    }

    for (thread &thread : write) {
        thread.join();
    }

    for (thread &thread : read) {
        thread.join();
    }
    long end = getCurrentTime();
    printf("=======================MulRWTest===============================\n");
    printf("MulRWTest cost time %d ms\n", (int) (end - begin));
    printf("Read read_count %d \n", read_count.load());
    printf("Write write_count %d \n", write_count.load());
    if (read_count.load() == write_count.load()) {
        printf("MulRWTest ok\n");
    }
    else {
        printf("MulRWTest failed\n");
    }
    delete  messQueue;
    messQueue = nullptr;
}

int main(int argc, const char *argv[])
{
    printf("===============================================================\n");
    SingleRWTest();
    printf("===============================================================\n");
    MulRWTest();
}

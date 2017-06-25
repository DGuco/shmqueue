//
//  main.cpp
//  Demo
//
//  Created by 杜国超 on 17/6/22.
//  Copyright © 2017年 杜国超. All rights reserved.
//

#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <iostream>
#include <unistd.h>
#include <string.h>
#include "messagequeue.h"

int main(int argc, const char * argv[]) {
    int shmid;
    int size = MEM_SIZE;
    // shmid就是共享内存的标识
    shmid = shmget((key_t)1235, (size_t)(size),IPC_CREAT|0666);
//    shmid = shmget((key_t)SHM_KEY, (size_t)MEM_SIZE, 0666|IPC_CREAT);
    if(shmid == -1)
    {
        printf("CreateShareMem failed. errno: %s.", strerror(errno));
        exit(1);
    }
    printf("shmid is %d\n",shmid);
    CMssageQueue::mpCurrAddr = (BYTE*)shmat(shmid,NULL,0);
    CMssageQueue *writeQueue = CMssageQueue::CreateInstance();
    int i = 0;
    while(1)
    {
        //////////////////////////////write///////////////////////////////
//        char data[100] = {0};
//        printf("Input your string:");
//        scanf(" %s",data);
//        int len = sizeof(data);
//        char* sendData = data;
//        writeQueue->SendMessage(data, len);
//        printf("write : %s, len = %d \n  ",sendData,len);
//        i++;

        ///////////////////////////read//////////////////////////////
        BYTE data[100] = {0};
        int len;
        int iRet = writeQueue->GetMessage(data, &len);
        if (iRet > 0){
            i++;
            printf("read : %s, len = %d \n  ",data,len);
        }else{
            printf("read error :errorno = %d \n  ",iRet);
        }
        if (i > 10)
        {
            break;
        }
        sleep(1);

    }
    if(shmdt(CMssageQueue::mpCurrAddr)<0)//解除链接
        exit(1);
    return 0;
}

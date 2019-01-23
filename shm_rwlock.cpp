//
// Created by dguco on 19-1-22.
//

#include <cstddef>
#include <cerrno>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <sys/ipc.h>
#include "shm_rwlock.h"

/*
struct sembuf {
    short semnum; -----  信号量集合中的信号量编号，0代表第1个信号量,1代表第二.....

    short val;    -----  若val>0进行V操作信号量值加val，表示进程释放控制的资源
                         若val<0进行P操作信号量值减val，若(semval-val)<0（semval为该信号量值），则调用进程阻塞，直到资源可用；若设置IPC_NOWAIT不会睡眠，
                         进程直接返回EAGAIN错误
                         若val==0时阻塞等待信号量为0，调用进程进入睡眠状态，直到信号值为0；若设置IPC_NOWAIT，进程不会睡眠，直接返回EAGAIN错误

    short flag;   -----  0 设置信号量的默认操作
                         IPC_NOWAIT 设置信号量操作不等待
                         SEM_UNDO  选项会让内核记录一个与调用进程相关的UNDO记录，如果该进程崩溃，则根据这个进程的UNDO记录自动恢复相应信号量的计数值
};
*/

using namespace std;
namespace shmmqueue
{
CShmRWlock::CShmRWlock()
    : m_iSemID(-1), m_iSemKey(-1)
{

}

CShmRWlock::CShmRWlock(key_t iKey)
{
    init(iKey);
}

void CShmRWlock::init(key_t iKey)
{
#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
    /* union semun is defined by including <sys/sem.h> */
#else
    /* according to X/OPEN we have to define it ourselves */
    union semun
    {
        int val;                  /* value for SETVAL */
        struct semid_ds *buf;     /* buffer for IPC_STAT, IPC_SET */
        unsigned short *array;    /* array for GETALL, SETALL */
        /* Linux specific part: */
        struct seminfo *__buf;    /* buffer for IPC_INFO */
    };
#endif
    int iSemID;
    union semun arg;
    //包含两个信号量,第一个为写信号两，第二个为读信号两
    u_short array[2] = {0, 0};
    //生成信号量集
    if ((iSemID = semget(iKey, 2, IPC_CREAT | IPC_EXCL | 0666)) != -1) {
        arg.array = &array[0];

        //将所有信号量的值设置为0
        if (semctl(iSemID, 0, SETALL, arg) == -1) {
            throw std::runtime_error("semctlt error: " + string(strerror(errno)));
        }
    }
    else {
        //如果失败，判断信号量是否已经存在
        if (errno != EEXIST) {
            throw std::runtime_error("sem has exist error: " + string(strerror(errno)));
        }

        //连接信号量
        if ((iSemID = semget(iKey, 2, 0666)) == -1) {
            throw std::runtime_error("semget error : " + string(strerror(errno)));
        }
    }

    m_iSemKey = iKey;
    m_iSemID = iSemID;
}

int CShmRWlock::Rlock() const
{
    /*包含两个信号量,第一个为写信号量，第二个为读信号量
     *获取读锁
     *等待写信号量（第一个）变为0：{0, 0, SEM_UNDO},并且把读信号量（第一个）加一：{1, 1, SEM_UNDO}
     **/
    struct sembuf sops[2] = {{0, 0, SEM_UNDO}, {1, 1, SEM_UNDO}};
    size_t nsops = 2;
    int ret = -1;

    do {
        ret = semop(m_iSemID, &sops[0], nsops);
    }
    while ((ret == -1) && (errno == EINTR));

    return ret;
}

int CShmRWlock::UnRlock() const
{

    /*包含两个信号量,第一个为写信号量，第二个为读信号量
     *解除读锁
     *把读信号量（第二个）减一：{1, -1, SEM_UNDO}
     **/
    struct sembuf sops[1] = {{1, -1, SEM_UNDO}};
    size_t nsops = 1;

    int ret = -1;

    do {
        ret = semop(m_iSemID, &sops[0], nsops);

    }
    while ((ret == -1) && (errno == EINTR));

    return ret;
}

bool CShmRWlock::TryRlock() const
{
    /*包含两个信号量,第一个为写信号量，第二个为读信号量
     *获取读锁
     *尝试等待写信号量（第一个）变为0：{0, 0,SEM_UNDO | IPC_NOWAIT},
     *把读信号量（第一个）加一：{1, 1,SEM_UNDO | IPC_NOWAIT}
     **/
    struct sembuf sops[2] = {{0, 0, SEM_UNDO | IPC_NOWAIT}, {1, 1, SEM_UNDO | IPC_NOWAIT}};
    size_t nsops = 2;

    int iRet = semop(m_iSemID, &sops[0], nsops);
    if (iRet == -1) {
        if (errno == EAGAIN) {
            //无法获得锁
            return false;
        }
        else {
            throw std::runtime_error("semop error : " + string(strerror(errno)));
        }
    }
    return true;
}

int CShmRWlock::Wlock() const
{
    /*包含两个信号量,第一个为写信号量，第二个为读信号量
     *获取写锁
     *尝试等待写信号量（第一个）变为0：{0, 0, SEM_UNDO},并且等待读信号量（第二个）变为0：{0, 0, SEM_UNDO}
     *把写信号量（第一个）加一：{0, 1, SEM_UNDO}
     **/
    struct sembuf sops[3] = {{0, 0, SEM_UNDO}, {1, 0, SEM_UNDO}, {0, 1, SEM_UNDO}};
    size_t nsops = 3;

    int ret = -1;

    do {
        ret = semop(m_iSemID, &sops[0], nsops);

    }
    while ((ret == -1) && (errno == EINTR));

    return ret;
}

int CShmRWlock::UnWlock() const
{
    /*包含两个信号量,第一个为写信号量，第二个为读信号量
     *解除写锁
     *把写信号量（第一个）减一：{0, -1, SEM_UNDO}
     **/
    struct sembuf sops[1] = {{0, -1, SEM_UNDO}};
    size_t nsops = 1;

    int ret = -1;

    do {
        ret = semop(m_iSemID, &sops[0], nsops);

    }
    while ((ret == -1) && (errno == EINTR));

    return ret;

    //return semop( m_iSemID, &sops[0], nsops);

}

bool CShmRWlock::TryWlock() const
{
    /*包含两个信号量,第一个为写信号量，第二个为读信号量
     *尝试获取写锁
     *尝试等待写信号量（第一个）变为0：{0, 0, SEM_UNDO | IPC_NOWAIT},并且尝试等待读信号量（第二个）变为0：{0, 0, SEM_UNDO | IPC_NOWAIT}
     *把写信号量（第一个）加一：{0, 1, SEM_UNDO | IPC_NOWAIT}
     **/
    struct sembuf sops[3] = {{0, 0, SEM_UNDO | IPC_NOWAIT},
                             {1, 0, SEM_UNDO | IPC_NOWAIT},
                             {0, 1, SEM_UNDO | IPC_NOWAIT}};
    size_t nsops = 3;

    int iRet = semop(m_iSemID, &sops[0], nsops);
    if (iRet == -1) {
        if (errno == EAGAIN) {
            //无法获得锁
            return false;
        }
        else {
            throw std::runtime_error("semop error : " + string(strerror(errno)));
        }
    }

    return true;
}

int CShmRWlock::Lock() const
{
    return Wlock();
}

//Unlock 释放锁
int CShmRWlock::Unlock() const
{
    return UnWlock();
}

bool CShmRWlock::trylock() const
{
    return TryWlock();
}

//获取sem key
key_t CShmRWlock::Getkey() const
{
    return m_iSemKey;
}

int CShmRWlock::getid() const
//获取sem id
{
    return m_iSemID;
}
}
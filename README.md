# shmqueue
- 基于c++内存池,共享内存和信号量实现高速的进程间通信队列,支持单进程读单进程写，单进程多进程写,多进程读单进程写,多进程读多进程写
# 实现原理
- 把消息队列对象生成在实现分配好的共享内存区中（共享内存去的大小远远大于消息队列管理对象messagequeue），对象中记录者共享内存去
  剩余内存的数据情况，消息内存区为一个环形内存区。如下图：
  ![原理图](https://github.com/DGuco/shmqueue/raw/master/ringbuff.png)
  [详细介绍](http://blog.csdn.net/suhuaiqiang_janlay/article/details/51194984),
- 写的时候移动end索引,读的时候移动begin索引,保证了在单进程读和单进程写的时候是线
  程安全的，多进程读躲进成写时利用信号量集一个读信号和写信号实现进程见共享内存读写锁来保证多进程安全。
# 测试用例
- 源码见main.cpp
- 测试结果：
      ===============================================================
      Try to malloc share memory of 10240 bytes...
      Successfully alloced share memory block, (key=12345), id = 11272200, size = 10240
      Mem trunk address 0x0x7ffff7ff4000,key 11272200 , size 10240, begin 0, end 0, queue module 0
      Write  SingleRWTest thread 1 ,write count 1000000
      Read SingleRWTest ,thread 1 ,read count 1000000
      Touch to share memory key = 12345...
      Now remove the exist share memory 11272200
      Remove shared memory(id = 11272200, key = 12345) succeed.
      =======================SingleRWTest=============================
      SingleRWTest cost time 3046 ms
      Read read_count 1000000
      Write write_count 1000000
      SingleRWTest ok 0
      ===============================================================
      Try to malloc share memory of 10240 bytes...
      Successfully alloced share memory block, (key=1234), id = 11304977, size = 10240
      Mem trunk address 0x0x7ffff7ff1000,key 11304977 , size 10240, begin 0, end 0, queue module 3
      ===============================================================
      Write  MulRWTest thread 4 ,write count 100000
      Write  MulRWTest thread 1 ,write count 100000
      Write  MulRWTest thread 0 ,write count 100000
      Write  MulRWTest thread 3 ,write count 100000
      Write  MulRWTest thread 2 ,write count 100000
      Read MulRWTest ,thread 3 ,read count 99981
      Read MulRWTest ,thread 1 ,read count 104765
      Read MulRWTest ,thread 2 ,read count 104513
      Read MulRWTest ,thread 4 ,read count 95279
      Read MulRWTest ,thread 0 ,read count 95467
      Touch to share memory key = 1234...
      Now remove the exist share memory 11304977
      Remove shared memory(id = 11304977, key = 1234) succeed.
      =======================MulRWTest===============================
      MulRWTest cost time 24639 ms
      Read read_count 500000
      Write write_count 500000
      MulRWTest ok
      为了操作方便，读写操作用线程代替进程
# 注意事项
- 优先考虑单进程读单进程写，无锁读写效率更快，多进程读在一定程度上会降低收发效率
- 如果数据来自网络，注意字节序列,大小端的问题
- 进程意外退出时可以不卸载共享内存区，重新启动时共享内存中的消息不会丢失，会重新因射到内存中，
  可以继续读取
- 创建共享内存队列时,注意读写模式
   - ONE_READ_ONE_WRITE,   //一个进程读消息一个进程写消息（推荐)
   - ONE_READ_MUL_WRITE,   //一个进程读消息多个进程写消息
   - MUL_READ_ONE_WRITE,   //多个进程读消息一个进程写消息
   - MUL_READ_MUL_WRITE,   //多个进程读消息多个进程写消息
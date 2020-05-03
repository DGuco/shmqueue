# 共享内存消息队列shmqueue
- 基于c++内存池,共享内存和信号量实现高速的进程间通信队列,支持单进程读单进程写，单进程多进程写,多进程读单进程写,多进程读多进程写
# 实现原理
- 把消息队列对象生成在实现分配好的共享内存区中（共享内存去的大小远远大于消息队列管理对象messagequeue），对象中记录者共享内存去
  剩余内存的数据情况，消息内存区为一个环形内存区。如下图：<br>
  ![原理图](https://github.com/DGuco/shmqueue/raw/master/ringbuff.png)<br>
  [详细介绍](http://blog.csdn.net/suhuaiqiang_janlay/article/details/51194984)<br>
  [参考示例](https://elixir.bootlin.com/linux/v3.8.13/source/kernel/kfifo.c)<br>
- 写的时候移动end索引,读的时候移动begin索引,保证了在单进程读和单进程写的时候是线
  程安全的，多进程读多进成写时利用信号量集(一个读信号和写信号)实现进程间的共享内存读写锁来保证多进程安全。
# 测试用例
- [源码main.cpp](https://github.com/DGuco/shmqueue/blob/master/main.cpp)
- 测试结果：
      ===============================================================<br>
      Try to malloc share memory of 10240 bytes...<br>
      Successfully alloced share memory block, (key=12345), id = 11272200, size = 10240<br>
      Mem trunk address 0x0x7ffff7ff4000,key 11272200 , size 10240, begin 0, end 0, queue module 0<br>
      Write  SingleRWTest thread 1 ,write count 1000000<br>
      Read SingleRWTest ,thread 1 ,read count 1000000<br>
      Touch to share memory key = 12345...<br>
      Now remove the exist share memory 11272200<br>
      Remove shared memory(id = 11272200, key = 12345) succeed.<br>
      =======================SingleRWTest=============================<br>
      SingleRWTest cost time 3046 ms<br>
      Read read_count 1000000<br>
      Write write_count 1000000<br>
      SingleRWTest ok 0<br>
      ===============================================================<br>
      Try to malloc share memory of 10240 bytes...<br>
      Successfully alloced share memory block, (key=1234), id = 11304977, size = 10240<br>
      Mem trunk address 0x0x7ffff7ff1000,key 11304977 , size 10240, begin 0, end 0, queue module 3<br>
      ===============================================================<br>
      Write  MulRWTest thread 4 ,write count 100000<br>
      Write  MulRWTest thread 1 ,write count 100000<br>
      Write  MulRWTest thread 0 ,write count 100000<br>
      Write  MulRWTest thread 3 ,write count 100000<br>
      Write  MulRWTest thread 2 ,write count 100000<br>
      Read MulRWTest ,thread 3 ,read count 99981<br>
      Read MulRWTest ,thread 1 ,read count 104765<br>
      Read MulRWTest ,thread 2 ,read count 104513<br>
      Read MulRWTest ,thread 4 ,read count 95279<br>
      Read MulRWTest ,thread 0 ,read count 95467<br>
      Touch to share memory key = 1234...<br>
      Now remove the exist share memory 11304977<br>
      Remove shared memory(id = 11304977, key = 1234) succeed.<br>
      =======================MulRWTest===============================<br>
      MulRWTest cost time 24639 ms<br>
      Read read_count 500000<br>
      Write write_count 500000<br>
      MulRWTest ok<br>
- 为了操作方便，这里的读写操作用全部用线程代替进程，从目的上来说效果是一样的
# 注意事项
- 优先考虑单进程读单进程写，无锁读写效率更快，多进程读在一定程度上会降低收发效率，注意这里所说的多进程读写，如果有两个进程
  一个进程只读不写一个进程只写不读,此时为单进程读单进程写，而不是多进程读写
- 如果数据来自网络，注意字节序列,大小端的问题
- 进程意外退出时可以不卸载共享内存区，重新启动时共享内存中的消息不会丢失，会重新因射到内存中，
  可以继续读取
- 创建共享内存队列时,注意读写模式
   - ONE_READ_ONE_WRITE,   //一个进程读消息一个进程写消息（推荐)
   - ONE_READ_MUL_WRITE,   //一个进程读消息多个进程写消息
   - MUL_READ_ONE_WRITE,   //多个进程读消息一个进程写消息
   - MUL_READ_MUL_WRITE,   //多个进程读消息多个进程写消息
- 如果需要创建多个共享内存队列且共享内存需要加锁，那么共创建共享内存的自定义的key不要连续，不同的key之间至少相隔2,读写锁分别用共享
　内存的key的key+1和key+2作为读写锁信号量集的key
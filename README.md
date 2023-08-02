## muduo-memcache

基于muduo库的简易版memcache   
memcache 本质上是一个远程的hash表，存储key value数据，即 hash_map<key, value>  
这个程序主要是用来展示，处理并发请求的非阻塞I/O网络编程 

#### 数据结构的设计
    不是用C语言完全定制数据结构，是用C++半定制数据结构  
    Item数据结构的设计和memcached中类似  
    hash table的设计采用unordered_set<shared_ptr<const Item>>  
    存放shared_ptr，利用引用计数可以减少临界区的长度  
    const Item 表明item存放到 hash table之后，就不允许再改变了  
    set一个已有的item，会先删除之前的item，再存储相同key的新item  
    全局有多个hash table，每个hash table配一把锁
#### 数据结构的内存开销
    内存开销大约是memcached的1.46~1.48倍(即多46%) 
    比如，100个client（连接），每个存储10000个item，value长度100个字节，  
    muduo-memcache消耗内存261M左右，memcached消耗内存179M左右
#### 网络I/O模型
    基于muduo网络库，即事件驱动的，I/O multiplexing + nonblocking I/O，支持多线程，即One EventLoop per Thread
#### 协议([protocol](https://github.com/xy27/muduo-memcache/blob/main/protocol.pdf "protocol.pdf"))
#### 实现的命令
	set/add/replace/append/prepend/cas
	get/gets
	delete
	version/quit/shutdown
#### 未支持
    数据超时
    内存替换算法没实现（LRU Least Recently Used 最近最少使用)
    没有自己定制内存分配器，使用系统自带的malloc
    incr/decr
    二进制协议
    只支持TCP协议
#### 具体的业务逻辑(协议处理逻辑)
![这是图片](https://github.com/xy27/muduo-memcache/blob/main/mem.png "协议处理逻辑")  
#### 性能对比测试
![这是图片](https://github.com/xy27/muduo-memcache/blob/main/test.png "性能测试")  

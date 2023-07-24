## muduo-memcache

学习陈硕老师muduo C++网络库自带的一个例子，基于muduo库的简易版memcache   
memcache 本质上是一个远程的hash表，存储key value数据，即 hash_map<key, value>  
这个程序主要是用来展示，处理并发请求的，非阻塞I/O网络编程 

### 数据结构的设计
    不是用C语言完全定制数据结构，用C++半定制数据结构  
    Item数据结构的设计     和memcached中类似  
    hash table的设计      采用unordered_set<shared_ptr<const Item>>  
    存放shared_ptr，利用引用计数可以减少临界区的长度  
    const Item 表明item存放到 hash table之后，就不允许再改变了  
    set 一个已有的item，会先删除之前的item，再存储相同key的新item  
	shard（分片） 全局有多个hash table 每个hash table 配一把锁

### 数据结构的内存开销

    内存开销 比 memcached大不少，大约是memcached的1.46~1.48倍(即多46%) 
    比如 100个client（连接），每个存储10000个item，value长度100个字节，muduo-memcache消耗261M左右，memcached消耗内存179M左右

### 网络I/O模型
    基于muduo网络库，即事件驱动的，I/O multiplexing + nonblocking I/O，支持多线程，即One EventLoop per Thread

### 协议
按照memcached 文本协议

贴个pdf

### 实现的命令(功能)

#### Storage commands
	set/add /replace/append/prepend/cas

#### Retrieval command
	get/gets
#### Deletion
	delete
#### 其它的
	version/quit/shutdown
### 未支持
#### 数据超时
#### 内存替换算法没实现（LRU Least Recently Used 最近最少使用)
#### incr / decr
#### 二进制协议
#### 没有自己定制内存分配器，使用系统自带的malloc
#### 只支持 TCP协议

### 具体的业务逻辑(协议处理逻辑)

### 性能对比测试

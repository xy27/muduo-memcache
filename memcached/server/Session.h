#ifndef MUDUO_EXAMPLES_MEMCACHED_SERVER_SESSION_H
#define MUDUO_EXAMPLES_MEMCACHED_SERVER_SESSION_H

#include "Item.h"

#include <muduo/base/Logging.h>

#include <muduo/net/TcpConnection.h>

#include <boost/tokenizer.hpp>

using muduo::string;

class MemcacheServer;

// 注意继承自 enable_shared_from_this
class Session : public std::enable_shared_from_this<Session>,
                muduo::noncopyable
{
 public:
  Session(MemcacheServer* owner, const muduo::net::TcpConnectionPtr& conn)
    : owner_(owner),
      conn_(conn),
      state_(kNewCommand),
      protocol_(kAscii), // FIXME
      noreply_(false),
      policy_(Item::kInvalid),
      bytesToDiscard_(0),
      needle_(Item::makeItem(kLongestKey, 0, 0, 2, 0)),   // needle_指向一个空的item
      bytesRead_(0),
      requestsProcessed_(0)
  {
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;

    // conn的消息回调(TCP连接上有数据可读时)绑定到了Session::onMessage，而不是通常的Server::onMessage
    conn_->setMessageCallback(
        std::bind(&Session::onMessage, this, _1, _2, _3));
  }

  ~Session()
  {
    LOG_INFO << "requests processed: " << requestsProcessed_
             << " input buffer size: " << conn_->inputBuffer()->internalCapacity()
             << " output buffer size: " << conn_->outputBuffer()->internalCapacity();
  }

  // 除了ctor，dtor外，没有其它的公有函数

 private:
  // 当前session所处的状态
  enum State
  {
    kNewCommand,
    kReceiveValue,
    kDiscardValue,
  };

  // 目前支持ascii协议，实际上只支持ascii协议，ascii协议是什么意思呢？二进制协议是什么意思呢？
  enum Protocol
  {
    kAscii,
    kBinary,
    kAuto,
  };

  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,
                 muduo::Timestamp);
  
  void onWriteComplete(const muduo::net::TcpConnectionPtr& conn);     // 这个函数没有实现，它的功能是？什么时候可能会需要它？

  void receiveValue(muduo::net::Buffer* buf);
  void discardValue(muduo::net::Buffer* buf);
  // TODO: highWaterMark
  // TODO: onWriteComplete

  // returns true if finished a request
  bool processRequest(muduo::StringPiece request);
  void resetRequest();
  void reply(muduo::StringPiece msg);

  // 还不是很清楚
  // 划分token的准则，是个函数对象，需要配合boost::tokenizer这个类使用
  struct SpaceSeparator
  {
    void reset() {}
    template <typename InputIterator, typename Token>
    bool operator()(InputIterator& next, InputIterator end, Token& tok);
  };

  // boost::tokenizer将 StringPiece 划分成 StringPiece类型的 token容器？？？
  // 用迭代器遍历这个容器的时候，会调用SpaceSeparator函数对象，对源数据进行token划分
  // 解引用容器迭代器会返回token
  // 好像大致含义就是这样的，boost源码有点复杂
  typedef boost::tokenizer<SpaceSeparator,
      const char*,
      muduo::StringPiece> Tokenizer;
  struct Reader;


  bool doUpdate(Tokenizer::iterator& beg, Tokenizer::iterator end);
  void doDelete(Tokenizer::iterator& beg, Tokenizer::iterator end);

  // 数据成员
  MemcacheServer* owner_;               // 需要调用MemcacheServer中的一些函数
  muduo::net::TcpConnectionPtr conn_;
  State state_;
  Protocol protocol_;

  // current request
  string command_;
  bool noreply_;
  Item::UpdatePolicy policy_;
  ItemPtr currItem_;              // 什么命令中用到 doUpdate中用到
  size_t bytesToDiscard_;
  
  // currItem_ 与 needle_ 的区别是？
  // currItem_是与当前请求相关的item
  // needle_只是一个临时的擦写变量？方便程序编写

  // cached
  ItemPtr needle_;                // 干什么的？ 可反复擦写的变量，相当于一个临时变量？方便代码的编写
  muduo::net::Buffer outputBuf_;  // 处理get请求时，用来存储响应get命令的item的数据

  // per session stats
  size_t bytesRead_;          // 这个连接上(总共？)读取了多少数据
  size_t requestsProcessed_;  // 这个连接上（总共？）处理了多少请求

  static string kLongestKey;
};

typedef std::shared_ptr<Session> SessionPtr;

#endif  // MUDUO_EXAMPLES_MEMCACHED_SERVER_SESSION_H

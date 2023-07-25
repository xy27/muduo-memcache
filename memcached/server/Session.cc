#include "Session.h"
#include "MemcacheServer.h"

using namespace muduo;
using namespace muduo::net;

static bool isBinaryProtocol(uint8_t firstByte)
{
  return firstByte == 0x80;
}

const int kLongestKeySize = 250;
string Session::kLongestKey(kLongestKeySize, 'x');

// 其实对boost库中的东西 根本不熟
// 只能读懂大致意思
template <typename InputIterator, typename Token>
bool Session::SpaceSeparator::operator()(InputIterator& next, InputIterator end, Token& tok)
{
  while (next != end && *next == ' ')
    ++next;
  if (next == end)
  {
    tok.clear();
    return false;
  }
  InputIterator start(next);
  const char* sp = static_cast<const char*>(memchr(start, ' ', end - start));
  if (sp)
  {
    tok.set(start, static_cast<int>(sp - start));   // 这个项目中tok是StringPiece类型
    next = sp;
  }
  else
  {
    tok.set(start, static_cast<int>(end - next));
    next = end;
  }
  return true;
}

struct Session::Reader
{
  Reader(Tokenizer::iterator& beg, Tokenizer::iterator end)
      : first_(beg),
        last_(end)
  {
  }

  // 在这个项目中，Reader::read是用来读取 set xy 0 0 3 这条命令中的 0 0 3 这几个值的
  // 调用一次，读取一个值到val
  template<typename T>
  bool read(T* val)
  {
    if (first_ == last_)
      return false;
    char* end = NULL;
    uint64_t x = strtoull((*first_).data(), &end, 10);    // 这里*first_得到的是StringPiece类型的对象
    if (end == (*first_).end())
    {
      *val = static_cast<T>(x);
      ++first_;               // 这里是更新了beg？？？ 按道理更新的是first_指针是值类型，++first时做了什么，调用了SpaceSeparator，进行了token划分？
      return true;            // 成功
    }
    return false;             // 失败
  }

 private:
  Tokenizer::iterator first_;       // Tokenizer::iterator是StringPiece*类型？？？
  Tokenizer::iterator last_;;
};

// 注意这里回调函数 参数中的 conn 与 Session的成员变量conn_
// 这两个TcpConnectionPtr 应该是指向同一个 TcpConnection
// 但可能不是同一TcpConnectionPtr？？？
void Session::onMessage(const muduo::net::TcpConnectionPtr& conn,
                        muduo::net::Buffer* buf,
                        muduo::Timestamp)
{
  assert(conn == conn_);  // 实际上是比较 conn.get() == conn_.get()

  const size_t initialReadable = buf->readableBytes();

  while (buf->readableBytes() > 0)
  {
    if (state_ == kNewCommand)
    {
      if (protocol_ == kAuto)
      {
        assert(bytesRead_ == 0);
        protocol_ = isBinaryProtocol(buf->peek()[0]) ? kBinary : kAscii;
      }

      assert(protocol_ == kAscii || protocol_ == kBinary);  // 目前只支持ascii协议
      if (protocol_ == kBinary)
      {
        // FIXME
      }
      else  // ASCII protocol
      {
        const char* crlf = buf->findCRLF();
        if (crlf)
        {
          int len = static_cast<int>(crlf - buf->peek());
          StringPiece request(buf->peek(), len);
          if (processRequest(request))      // returns true if finished a request
          {
            resetRequest();
          }
          buf->retrieveUntil(crlf + 2);     // 将解析完的一条消息从buf中取走
        }
        else  // 非阻塞I/O，有数据可读时，不一定能解析到一条完整的消息，阻塞I/O没有解析到一条完整的消息就会一直阻塞
        {
          if (buf->readableBytes() > 1024)  // 一次请求的消息过长
          {
            // FIXME: check for 'get' and 'gets'
            conn_->shutdown();
            // buf->retrieveAll() ???
          }
          break;  // 直接break掉while，这个时候，目前读取的不足数据还是保存在buf中，并没有取走
        }
      }
    }
    else if (state_ == kReceiveValue)
    {
      receiveValue(buf);    // 注意准备接收buf中的数据
    }
    else if (state_ == kDiscardValue)   // 什么情况会导致这个状态？
    {                                   // 当item数据过大时，直接丢弃客户端发过来的item数据
      discardValue(buf);                // 比如 set xy 0 0 10485760（10M）
    }
    else
    {
      assert(false);
    }
  }
  bytesRead_ += initialReadable - buf->readableBytes();   // 统计这个TCP连接上总共读取了多少数据
}

void Session::receiveValue(muduo::net::Buffer* buf)
{
  assert(currItem_.get());          // 断言item内存已经分配
  assert(state_ == kReceiveValue);
  // if (protocol_ == kBinary)

  const size_t avail = std::min(buf->readableBytes(), currItem_->neededBytes());
  assert(currItem_.unique());
  currItem_->append(buf->peek(), avail);    // 向item中填充数据
  buf->retrieve(avail);
  if (currItem_->neededBytes() == 0)
  {
    if (currItem_->endsWithCRLF())    // 读取足够数据，且以\r\n结尾，才是符合协议的
    {
      bool exists = false;
      if (owner_->storeItem(currItem_, policy_, &exists))
      {
        reply("STORED\r\n");
      }
      else  // 失败，未能store
      {
        if (policy_ == Item::kCas)
        {
          if (exists)
          {
            reply("EXISTS\r\n");
          }
          else
          {
            reply("NOT_FOUND\r\n");
          }
        }
        else
        {
          reply("NOT_STORED\r\n");
        }
      }
    }
    else
    {
      reply("CLIENT_ERROR bad data chunk\r\n");
    }
    // LOG_INFO << currItem_.use_count();  // 2
    resetRequest(); // 不管是成功还是失败，都重置当前的请求，注意未成功STORED的话，item已经接收的数据，什么时候被释放？
                    // resetRequest中有currItem_.reset()

    // LOG_INFO << currItem_.use_count();  // 0

    // currItem_没有管理任何内存，断言unique也是成功的吗？？？
    assert(currItem_.unique()); // 断言能成功吗？实际上，debug版本，断言会失败，因为resetRequest()中会调用currItem_.reset()

    state_ = kNewCommand;
  }
}

void Session::discardValue(muduo::net::Buffer* buf)
{
  assert(!currItem_);
  assert(state_ == kDiscardValue);
  if (buf->readableBytes() < bytesToDiscard_)
  {
    bytesToDiscard_ -= buf->readableBytes();
    buf->retrieveAll();
  }
  else
  {
    buf->retrieve(bytesToDiscard_);
    bytesToDiscard_ = 0;
    resetRequest();
    state_ = kNewCommand;
  }
}

bool Session::processRequest(StringPiece request)
{
  // 断言当前请求(一个新请求)的状态
  assert(command_.empty());
  assert(!noreply_);
  assert(policy_ == Item::kInvalid);
  assert(!currItem_);
  assert(bytesToDiscard_ == 0);
  ++requestsProcessed_;   // 这个会话上处理的请求数++

  // check 'noreply' at end of request line
  if (request.size() >= 8)
  {
    StringPiece end(request.end() - 8, 8);
    if (end == " noreply")
    {
      noreply_ = true;
      request.remove_suffix(8);
    }
  }

  SpaceSeparator sep;
  Tokenizer tok(request.begin(), request.end(), sep);
  Tokenizer::iterator beg = tok.begin();
  if (beg == tok.end())
  {
    reply("ERROR\r\n");
    return true;
  }
  (*beg).CopyToString(&command_);   // *beg是StringPiece 将命令token 拷贝到command_中
  ++beg;  // 继续分割解析消息
  if (command_ == "set" || command_ == "add" || command_ == "replace"
      || command_ == "append" || command_ == "prepend" || command_ == "cas")
  {
    // this normally returns false
    return doUpdate(beg, tok.end());
  }
  else if (command_ == "get" || command_ == "gets")
  {
    bool cas = command_ == "gets";

    // FIXME: send multiple chunks with write complete callback.
    // 如果没有write complete callback, 写大块数据会怎么样？？？
    // server写的太快，客户端接收的很慢或不及时，会怎么样？
    // 有write complete callback 和 没有 write complete callback 分别是什么样的？
    // 可以做个简单的实验

    while (beg != tok.end())
    {
      StringPiece key = *beg;
      bool good = key.size() <= kLongestKeySize;
      if (!good)
      {
        reply("CLIENT_ERROR bad command line format\r\n");
        return true;
      }

      needle_->resetKey(key);
      ConstItemPtr item = owner_->getItem(needle_); // 查找item是根据item的key的
      ++beg;  // 继续分割解析消息
      if (item)
      {
        item->output(&outputBuf_, cas);   // 将item相关的数据添加到outputBuf_中
      }
    }
    outputBuf_.append("END\r\n");

    // conn_上的outputBuffer的可写空间 过大
    if (conn_->outputBuffer()->writableBytes() > 65536 + outputBuf_.readableBytes())
    {
      LOG_DEBUG << "shrink output buffer from " << conn_->outputBuffer()->internalCapacity();
      conn_->outputBuffer()->shrink(65536 + outputBuf_.readableBytes());
    }

    conn_->send(&outputBuf_);           // 发送数据
  }
  else if (command_ == "delete")
  {
    doDelete(beg, tok.end());
  }
  else if (command_ == "version")
  {
    reply("VERSION 0.01 muduo\r\n");
  }
  else if (command_ == "quit")    // 假如客户端是telnet的话，telnet read 返回0 便会退出？？？
  {
    conn_->shutdown();
  }
  else if (command_ == "shutdown")
  {
    // "ERROR: shutdown not enabled"
    conn_->shutdown();
    owner_->stop();
  }
  else
  {
    reply("ERROR\r\n");
    LOG_INFO << "Unknown command: " << command_;
  }
  return true;
}

void Session::resetRequest()
{
  command_.clear();
  noreply_ = false;
  policy_ = Item::kInvalid;
  currItem_.reset();
  bytesToDiscard_ = 0;
}

void Session::reply(muduo::StringPiece msg)
{
  if (!noreply_)
  {
    conn_->send(msg.data(), msg.size());
  }
}

bool Session::doUpdate(Session::Tokenizer::iterator& beg, Session::Tokenizer::iterator end)
{
  if (command_ == "set")
    policy_ = Item::kSet;
  else if (command_ == "add")
    policy_ = Item::kAdd;
  else if (command_ == "replace")
    policy_ = Item::kReplace;
  else if (command_ == "append")
    policy_ = Item::kAppend;
  else if (command_ == "prepend")
    policy_ = Item::kPrepend;
  else if (command_ == "cas")
    policy_ = Item::kCas;
  else
    assert(false);

  // FIXME: check (beg != end)
  StringPiece key = (*beg);
  ++beg;
  bool good = key.size() <= kLongestKeySize;

  uint32_t flags = 0;
  time_t exptime = 1;
  int bytes = -1;
  uint64_t cas = 0;

  Reader r(beg, end);
  good = good && r.read(&flags) && r.read(&exptime) && r.read(&bytes);

  int rel_exptime = static_cast<int>(exptime);      // 实际上该实现并没有处理超时相关的问题，能不能实现一下？？？
  if (exptime > 60*60*24*30)
  {
    rel_exptime = static_cast<int>(exptime - owner_->startTime());
    if (rel_exptime < 1)
    {
      rel_exptime = 1;
    }
  }
  else
  {
    // rel_exptime = exptime + currentTime;
  }

  if (good && policy_ == Item::kCas)
  {
    good = r.read(&cas);
  }

  if (!good)
  {
    reply("CLIENT_ERROR bad command line format\r\n");
    return true;
  }
  if (bytes > 1024*1024)  // 数据太大
  // if (bytes > 20)
  {
    reply("SERVER_ERROR object too large for cache\r\n");
    needle_->resetKey(key);
    owner_->deleteItem(needle_);    // 注意这里直接删除了这个item（有item的话删除成功，没有的话相当于空操作），原始memcache是怎么处理的？？？
    bytesToDiscard_ = bytes + 2;
    state_ = kDiscardValue;     // 丢弃接下来客户端发过来的 bytes + 2 数据？？？
    return false;               // 是的，接下来对方发过来的数据，直接丢弃
  }
  else
  {
    currItem_ = Item::makeItem(key, flags, rel_exptime, bytes + 2, cas);      // 注意这里分配了item所需的内存
    state_ = kReceiveValue;                                                   // 接下来就是接收数据，填充item
    return false;
  }
}

void Session::doDelete(Session::Tokenizer::iterator& beg, Session::Tokenizer::iterator end)
{
  assert(command_ == "delete");
  // FIXME: check (beg != end)
  StringPiece key = *beg;
  bool good = key.size() <= kLongestKeySize;
  ++beg;
  if (!good)
  {
    reply("CLIENT_ERROR bad command line format\r\n");
  }
  else if (beg != end && *beg != "0") // issue 108, old protocol
  {
    reply("CLIENT_ERROR bad command line format.  Usage: delete <key> [noreply]\r\n");
  }
  else
  {
    needle_->resetKey(key);
    if (owner_->deleteItem(needle_))
    {
      reply("DELETED\r\n");
    }
    else
    {
      reply("NOT_FOUND\r\n");
    }
  }
}

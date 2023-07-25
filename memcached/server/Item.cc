#include "Item.h"

#include <muduo/base/LogStream.h>
#include <muduo/net/Buffer.h>

#include <boost/functional/hash/hash.hpp>

#include <string.h> // memcpy
#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

Item::Item(StringPiece keyArg,
           uint32_t flagsArg,
           int exptimeArg,
           int valuelen,
           uint64_t casArg)
  : keylen_(keyArg.size()),
    flags_(flagsArg),
    rel_exptime_(exptimeArg),
    valuelen_(valuelen),
    receivedBytes_(0),
    cas_(casArg),
    hash_(boost::hash_range(keyArg.begin(), keyArg.end())),   // 求StringPiece类型变量的hash
    data_(static_cast<char*>(::malloc(totalLen())))           // 注意这个时候分配了item所需的内存空间(存放key value \r\n)
{
  assert(valuelen_ >= 2);   // 空item, value没有实际的数据, 只有\r\n    为什么valuelen_ >= 2 ？？？
                            // makeItem时 传递的valuelen_是要算上\r\n的
  assert(receivedBytes_ < totalLen());
  append(keyArg.data(), keylen_);   // 先把key填充到item的data_中
}

void Item::append(const char* data, size_t len)
{
  assert(len <= neededBytes());
  memcpy(data_ + receivedBytes_, data, len);
  receivedBytes_ += static_cast<int>(len);  // 更新receivedBytes_字段
  assert(receivedBytes_ <= totalLen());
}

// 将item相关的信息输出到out中，处理get命令时会用到这个函数
void Item::output(Buffer* out, bool needCas) const
{
  out->append("VALUE ");
  out->append(data_, keylen_);
  LogStream buf;                                  // 注意LogStream的使用
  buf << ' ' << flags_ << ' ' << valuelen_-2;     // 减去\r\n的长度
  if (needCas)
  {
    buf << ' ' << cas_;
  }
  buf << "\r\n";
  out->append(buf.buffer().data(), buf.buffer().length());
  out->append(value(), valuelen_);
}

// 重置item的key字段为k 注意StringPiece这个类型，非常有意思
void Item::resetKey(StringPiece k)              // 什么时候需要用到resetKey, item的数据什么时候被释放
{
  assert(k.size() <= 250);
  keylen_ = k.size();
  receivedBytes_ = 0;
  append(k.data(), k.size());
  hash_ = boost::hash_range(k.begin(), k.end());
}

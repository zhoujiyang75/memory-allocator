#pragma once
#include"Common.h"

//单例模式
class CentralCache
{
public:
	static CentralCache* GetInstance()//获取全局唯一实例
	{
		return &_sInst;
	}

	Span* GetOneSpan(SpanList& list, size_t byte_size);//获取一个span对象

	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);//从中心缓存区获取一定数量的对象给thread cache(分多次申请会加大锁竞争，降低效率)

	void ReleaseListToSpans(void*& start, size_t size);//接收从thread cache回收的小块内存

private:
	SpanList _spanLists[NFREELIST];
private:
	CentralCache()//构造函数私有化
	{
	}

	CentralCache(const CentralCache&) = delete;//禁用拷贝构造

	static CentralCache _sInst;//全局唯一对象
};
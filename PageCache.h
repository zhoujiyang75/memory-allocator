#pragma once
#include"Common.h"
#include"ObjectPool.h"
#include "PageMap.h"

//单例模式
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	// 向操作系统申请span以及检查有没有空闲的页
	Span* NewSpan(size_t k);

	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	//从central cache归还span
	void ReleaseSpanToPageCache(Span* span);

	std::mutex _pageMtx;
private:

	SpanList _spanLists[NPAGES];
 
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	ObjectPool<Span> _spanPool;

	PageCache()//构造函数私有化
	{ }
	
	PageCache(const PageCache&) = delete;//删掉拷贝构造

	static PageCache _sInst;
};
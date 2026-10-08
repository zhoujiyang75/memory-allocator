#include"CentralCache.h"
#include"PageCache.h"

CentralCache CentralCache::_sInst;//初始化对象

//获取一个非空的span
Span* CentralCache::GetOneSpan(SpanList& list, size_t byte_size)
{
	Span* it = list.Begin();//从头开始找
	while (it != list.end())
	{
		if (it->_freeList != nullptr)//找到不为空
		{
			return it;//直接返回该位置span
		}
		it = it->_next;
	}
	
	//先解锁,便于其他对象获取
	list._mtx.unlock();

	//span都为空，去page cache去申请(先上锁)
	PageCache* _sInst = PageCache::GetInstance();//初始化PageCache对象
	_sInst->_pageMtx.lock();//上全局锁
	Span* span = _sInst->NewSpan(SizeClass::NumMovePage(byte_size));//从page cache中获取span(需进行切分)
	span->_isUse = true;//标记为已使用
	span->_objSize = byte_size;
	_sInst->_pageMtx.unlock();//解锁

	//切分span
	char* start = (char*)(span->_pageId << PAGE_SHIFT);//找到span的起始地址
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;//span的末尾地址
	
	// 把大块内存切成自由链表链接起来
	// 1、先切一块下来去做头，方便尾插
	span->_freeList = start;
	start += byte_size;
	void* tail = span->_freeList;
	//int i = 1;
	while (start < end)
	{
		//++i;
		NextObj(tail) = start;
		tail = NextObj(tail); // tail = start;
		start += byte_size;
	}
	NextObj(tail) = nullptr;
	// 切好span以后，需要把span挂到桶里面去的时候，再加锁
	list._mtx.lock();
	list.PushFront(span);

	return span;
}                                                                                                                                             

size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)//由于只能有一个返回值,start和end作为输出型参数
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();//加锁

	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	//从span中获取batchNum个对象，如果不够，有多少拿多少
	start = span->_freeList;
	end = start;
	size_t i = 0;//计数
	size_t actualNum = 1;//记录实际拿了多少
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		i++;
		actualNum++;
	}
	span->_freeList = NextObj(end);//改变链表头节点指向
	NextObj(end) = nullptr;//切断与剩余节点的联系
	_spanLists[index]._mtx.unlock();//解锁
	return actualNum;
}

void CentralCache::ReleaseListToSpans(void*& start, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();//上锁
	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

		//头插
		NextObj(start) = span->_freeList;
		span->_freeList = start; 
		span->_useCount--;

		//所有内存块都回来了，将大块内存还给page cache
		if (span->_useCount == 0)
		{
			_spanLists[index].Erase(span);//将节点从spanLists里面拿出来

			span->_freeList = nullptr;
			span->_prev = nullptr;
			span->_next = nullptr;
			_spanLists[index]._mtx.unlock();//解锁(要去page cache去归还大块内存)

			//去page cache去归还span
			PageCache::GetInstance()->_pageMtx.lock();//上锁
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();//继续上锁
		}
		start = next;
	}
	_spanLists[index]._mtx.unlock();//解锁
}
#include"PageCache.h"

PageCache PageCache::_sInst;//初始化PageCache

Span* PageCache::NewSpan(size_t k)
{
	//1.直接从第 k 个桶里找现成的 Span
	//2.从更大的桶里找一个更大的 Span，然后切割
	//3.前面两种都没找到 → 向系统要一大块内存
	assert(k > 0);
	//大于128页直接去堆申请
	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;//算出页号
		span->_n = k;
		//_idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);
		return span;
	}

	if (!_spanLists[k].Empty())//第k号桶不为空
	{
		Span* kSpan = _spanLists[k].PopFront();

		for (size_t i = 0; i < kSpan->_n; i++)
		{
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}
		return kSpan;//返回头删节点
	}
	else
	{
		for (int i = k + 1; i < NPAGES; i++)
		{
			if (!_spanLists[i].Empty())
			{
				Span* nSpan = _spanLists[i].PopFront();//待切分的页
				Span* kSpan = _spanPool.New();

				//处理切分好的页
				kSpan->_pageId = nSpan->_pageId;
				kSpan->_n = k;

				nSpan->_pageId += k;//起始页后移
				nSpan->_n -= k;

				//将剩余页挂起
				_spanLists[nSpan->_n].PushFront(nSpan);

				//处理nspan页号和span的映射(只用关心头和尾，这里只为了负责回收内存的时候能够向上/下查找)
				_idSpanMap.set(nSpan->_pageId, nSpan);
				_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

				//处理kspan的映射(这里需要对kspan每个页进行映射，因为要传给central cache,会被切分成很多小块)
				for (size_t i = 0; i < kSpan->_n; i++)
				{
					_idSpanMap.set(kSpan->_pageId + i, kSpan);
				}

				return kSpan;
			}
		}

		//向操作系统申请128页的内存
		Span* bigSpan = _spanPool.New();
		void* ptr = SystemAlloc(NPAGES - 1);//虚拟地址
		bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;//映射为页号
		bigSpan->_n = NPAGES - 1;

		_spanLists[bigSpan->_n].PushFront(bigSpan);//插入大块页
		return NewSpan(k);
	}
}

Span* PageCache::MapObjectToSpan(void* obj)
{
	//PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);//根据虚拟地址算出页号
	//std::unique_lock<std::mutex> lock(_pageMtx);
	//if (_idSpanMap.find(id) != _idSpanMap.end())//找到对应的span了
	//{
	//	return _idSpanMap[id];
	//}
	//else
	//{
	//	assert(false);
	//	return nullptr;\\5
	//}

	PAGE_ID pageId = (PAGE_ID)obj >> PAGE_SHIFT;
	auto* ret = _idSpanMap.get(pageId);
	assert(ret);
	return (Span*)ret;
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//大于128页的直接还给堆
	if (span->_n > NPAGES - 1)
	{
		//将页号转化成虚拟地址
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		_spanPool.Delete(span);
		return;
	}
	//归还span之前优先检查能不能合并相邻页号的span，以减少外碎片的问题
	while (1)
	{
		//前面的页号
		PAGE_ID prevId = span->_pageId - 1;

		if (!_idSpanMap.get(prevId))
		{
			break;
		}
		//前面页号的span
		Span* prevSpan = (Span*)_idSpanMap.get(prevId);

		//检查是否被central cache正在使用
		if (prevSpan->_isUse)
		{
			break;
		}

		//如果合并内存大于128页，管理不了，不合并
		if (prevSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		//合并
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;
		//删除prevSpan(这里删prevSpan但不删span是为了最后统一逻辑，因为可能并不能进行合并，方便最后对span统一操作)
		_spanLists[prevSpan->_n].Erase(prevSpan);
		_spanPool.Delete(prevSpan);
	}

	//同理，这里是向后查找
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;

		if (!_idSpanMap.get(nextId))
		{
			break;
		}

		Span* nextSpan = (Span*)_idSpanMap.get(nextId);
		if (nextSpan->_isUse == true)
		{
			break;
		}

		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += nextSpan->_n;
		_spanLists[nextSpan->_n].Erase(nextSpan);
		_spanPool.Delete(nextSpan);
	}

	span->_isUse = false;//设置为空闲

	_spanLists[span->_n].PushFront(span);

	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}

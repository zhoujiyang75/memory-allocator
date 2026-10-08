#include"ThreadCache.h"
#include"CentralCache.h"

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));
	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);

	assert(actualNum >= 1);

	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}

	else
	{
		//将截下来的内存块给thread cache
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);//第一块内存返回给用户使用，剩下的链到自由链表去
		return start;
	}
}

void* ThreadCache::Allocate(size_t size)//分配对象
{
	assert(size <= MAX_BYTES);
	size_t alignSize = SizeClass::RoundUp(size);//实际分配内存大小
	size_t index = SizeClass::Index(size);//应分配到哪个索引的哈希桶
	//先从自由链表中申请
	if (!_freeLists[index].Empty())//如果自由链表不为空
	{
		return _freeLists[index].Pop();
	}
	else//自由链表为空，向CentralCache申请
	{
		return FetchFromCentralCache(index, alignSize);
	}
}

//回收对象进自由链表
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);
	size_t index = SizeClass::Index(size);//找对应自由链表索引
	_freeLists[index].Push(ptr);//头插进自由链表
	
	// 当链表长度大于一次批量申请的内存时就开始还一段list给central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}
}

void ThreadCache::ListTooLong(FreeList& list, size_t size)//释放内存回central cache
{
	void* start = nullptr;
	void* end = nullptr;

	list.PopRange(start, end, list.MaxSize());
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);//找central cache去回收
}

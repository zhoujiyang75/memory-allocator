#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <new>

#include <time.h>
#include <assert.h>

#include <thread>
#include <mutex>
#include <atomic>

using std::cout;
using std::endl;

#ifdef _WIN32
	#include <windows.h>
#else
	// ...
#endif

static const size_t MAX_BYTES = 256 * 1024;
static const size_t NFREELIST = 208;
static const size_t NPAGES = 129;
static const size_t PAGE_SHIFT = 13;

#ifdef _WIN64
	typedef unsigned long long PAGE_ID;
#elif _WIN32
	typedef size_t PAGE_ID;
#else
	// linux
#endif

// 直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux下brk mmap等
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap等
#endif
}

static void*& NextObj(void* obj)
{
	return *(void**)obj;//获取obj前4/8个字节，用来存下一个obj的地址
}

class FreeList
{
public:
	void Push(void* obj)//头插进自由链表
	{
		assert(obj);
		NextObj(obj) = _freeList;
		_size++;
		_freeList = obj;
	}

	void PushRange(void* start, void* end, size_t n)//从central cache取内存块到thread cache的范围
	{
		NextObj(end) = _freeList;
		_freeList = start;

		_size += n;
	}

	void PopRange(void*& start, void*& end, size_t n)//从thread cache弹出内存块范围
	{
		assert(n <= _size);
		start = _freeList;
		end = start;
		for (size_t i = 0; i < n - 1; i++)
		{
			end = NextObj(end);
		}
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	void* Pop()
	{
		assert(_freeList);
		void* obj = _freeList;
		void* next = NextObj(_freeList);//保存下一个节点的地址
		_freeList = next;
		_size--;
		return obj;
	}

	bool Empty()
	{
		return _freeList == nullptr;
	}

	size_t& MaxSize()
	{
		return _maxSize;
	}

	size_t& Size()
	{
		return _size;
	}

private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;//记录当前从central cache中拿多少个小块内存
	size_t _size = 0;//记录当前自由链表中有多少小块内存
};

//计算对象大小的对齐映射规则
class SizeClass
{
// 整体控制在最多10%左右的内碎片浪费
// [1,128]					8byte对齐	    freelist[0,16)     16
// [128+1,1024]				16byte对齐	    freelist[16,72)    56
// [1024+1,8*1024]			128byte对齐	    freelist[72,128)   56
// [8*1024+1,64*1024]		1024byte对齐     freelist[128,184) 56
// [64*1024+1,256*1024]		8*1024byte对齐   freelist[184,208)
public:
	static size_t _RoundUp(size_t size, size_t alignNum)//计算对齐后应分配的内存大小
	{
		size_t alignSize;
		if (size % alignNum == 0)//不需要对齐
		{
			alignSize = size;
			return alignSize;
		}
		else
		{
			alignSize = (size / alignNum + 1) * alignNum;
		}
		return alignSize;
	}

	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			return _RoundUp(size, 1024);
		}
		else if (size <= MAX_BYTES)
		{
			return _RoundUp(size, 8 * 1024);
		}
		else
		{ 
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}

	static inline size_t _Index(size_t size, size_t alignNum)//找当前自由链表的对应桶的偏移量
	{
		size_t index;
		if (size % alignNum == 0)
		{
			index = size / alignNum - 1;
			return index;
		}
		else
		{
			index = size / alignNum;
			return index;
		}
	}

	static inline size_t Index(size_t size)//计算映射的是哪个自由链表
	{
		assert(size <= MAX_BYTES);
		static int group_array[4] = { 16,56,56,56 };
		if (size <= 128)
		{
			return _Index(size, 8);
		}
		else if (size <= 1024)
		{
			return _Index(size - 128, 16) + group_array[0];
		}
		else if (size <= 8 * 1024)
		{
			return _Index(size - 1024, 128) + group_array[0] + group_array[1];
		}
		else if (size <= 64 * 1024)
		{
			return _Index(size - 8 * 1024, 1024) + group_array[0] + group_array[1] + group_array[2];
		}
		else if (size <= 256 * 1024)
		{
			return _Index(size - 64 * 1024, 8 * 1024) + group_array[0] + group_array[1] + group_array[2] + group_array[3];
		}
		else
		{
			assert(false);
		}
		return -1;
	}

	// 一次thread cache从中心缓存获取多少个
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 小对象一次批量上限低
		int num = MAX_BYTES / size;
		if (num < 2)
		{
			num = 2;
		}
		if (num > 512)
		{
			num = 512;
		}
		return num;
	}

	// 计算一次向系统获取几个页
	// 单个对象 8byte
	// ...
	// 单个对象 256KB
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;//总共需要分配多少字节
		npage >>= PAGE_SHIFT;//计算分配几页
		if (npage == 0)
		{
			return 1;
		}
		else
		{
			return npage;
		}
	}

};

//span结构
struct Span
{
	PAGE_ID _pageId = 0;//起始页页号
	size_t _n = 0;//页的数量
	size_t _objSize = 0;//切好的小对象的大小
	
	Span* _next = nullptr;
	Span* _prev = nullptr;

	size_t _useCount = 0;//记录有多少小内存块被分配
	void* _freeList = nullptr;//切好小块内存的自由链表

	bool _isUse = false;//记录当前span是否正在被central cache使用
};

//带头双向循环链表
class SpanList
{
public:
	SpanList()
	{
		_head = new Span;

		_head->_next = _head;
		_head->_prev = _head;
	}

	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;

		prev->_next = newSpan;
		newSpan->_prev = prev;

		newSpan->_next = pos;
		pos->_prev = newSpan;       
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	Span* Begin()//返回头节点
	{
		return _head->_next;
	}

	Span* end()//返回尾节点
	{
		return _head;
	}

	bool Empty()//判断链表是否为空
	{
		return _head->_next == _head;
	}

	void Erase(Span* pos)
	{
		assert(pos);
		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
	}

private:
	Span* _head;
public:
	std::mutex _mtx;//桶锁，避免竞争
};



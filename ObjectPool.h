#pragma once
#include"Common.h"
//定长内存池(为了解决项目中new/delete的性能问题)
template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;
		//首先去自由链表去找
		//自由链表为空
		if (!_freeList)
		{
			//去大块内存去找
			//剩下的内存不够了
			if (_remainBytes < sizeof(T))
			{
				//去堆重新开一大块
				_remainBytes = 128 * 1024;//待开的字节数
				_memory = (char*)SystemAlloc(_remainBytes >> 13);
			}
			obj = (T*)_memory;
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
		}
		//自由链表不为空
		else
		{
			void* next = *((void**)_freeList);
			obj = (T*)_freeList;
			_freeList = next;
		}
		//定位new,显式调用构造初始化
		new(obj)T;
		return obj;
	}

	void Delete(T* obj)
	{
		//显式调用析构
		obj->~T();
		//把内存挂到自由链表里
		*(void**)obj = _freeList;
		_freeList = obj;
	}
private:
	char* _memory = nullptr;//指向大块内存的指针
	size_t _remainBytes = 0;//大块内存的剩余字节数
	void* _freeList = nullptr;//还回来的内存接在这个自由链表上
};
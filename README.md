# 高性能分级缓存内存分配器（TCMalloc Style Memory Allocator）

参考 Google TCMalloc 设计思想实现的多线程内存分配器，解决多线程场景下原生
`malloc/free` 的锁竞争与系统调用开销问题，同时通过分层缓存与 span 合并
控制内存碎片。

## 整体架构

<img width="1125" height="635" alt="image" src="https://github.com/user-attachments/assets/8d2f6a7f-02f7-47cb-83a5-ae12258b6f22" />



## 核心参数

<img width="1086" height="465" alt="image" src="https://github.com/user-attachments/assets/5f1d6b09-0204-4ae4-92c2-6b745b61ee2f" />


## 对齐与分桶

五档对齐策略，将内碎片控制在约 10% 以内：

<img width="1057" height="513" alt="image" src="https://github.com/user-attachments/assets/8590f213-9466-4a83-a214-7a3a4ec81538" />


对齐公式：`(bytes + align - 1) & ~(align - 1)`，索引映射用分段
`ceil(段内偏移 / 粒度) - 1 + 前段桶数累计`，全部为位运算，O(1)。

## 关键设计

- **自适应批量获取**：`batchNum = min(_maxSize, NumMoveSize(size))`，
  `_maxSize` 随使用次数递增（水涨船高），`NumMoveSize = clamp(256KB/size, 2, 512)`，
  冷线程少拿少占，热线程批量搬运。
- **页号基数树映射**：地址 >> 13 得页号，`PageMap1<19>`（2^19 槽指针数组）
  实现 O(1) 无冲突的"任意地址 → 所属 span"反查；映射建立后永不删除。
- **span 合并消除外碎片**：span 归还 PageCache 时按页号前后找邻居
  （`pageId - 1` / `pageId + n`），满足三个条件才合并：邻居有映射、
  邻居 `_isUse == false`（未被 CentralCache 借用）、合并后 ≤ 128 页。
- **ObjectPool 定长池**：Span / ThreadCache / PageMap 节点等元数据走
  定长对象池（大块 VirtualAlloc + 偏移切割 + freelist 复用 + 定位 new），
  避免元数据自引用系统 malloc 造成递归。

## 构建与运行

- 环境：Windows + Visual Studio 2019 及以上（C++11 起需要 `std::thread`、`std::atomic`）
- 步骤：打开 `高并发内存池.sln` → 选择 Release/x64 → 生成并运行

## Benchmark

`Benchmark.cpp` 对比原生 malloc/free 与本内存池：默认 4 线程、10 轮、
每轮每线程 1000 次 16 字节分配与释放，分别统计 alloc / dealloc / 总耗时。

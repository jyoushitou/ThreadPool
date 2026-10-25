# ThreadPool

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![CMake](https://img.shields.io/badge/CMake-%E2%89%A5%203.15-brightgreen.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)
![Type](https://img.shields.io/badge/type-header--only-informational.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

一个仅依赖 C++17 标准库的轻量线程池，单头文件实现：固定数量的 worker 线程从任务队列取任务执行，提交任务返回 `std::future`，支持停止与重启。

## 目的

- 学习并理解线程池的内部实现：任务队列、条件变量、`packaged_task`、`future`、停机与 join
- 作为 `Common_CPP` 下的基础组件，后续被自己的项目直接复用
- 同类基础组件：[NetWorkCommunication](https://github.com/jyoushitou/NetWorkCommunication)

## 特性

- 单头文件 `include/ThreadPool.h`，无第三方依赖，要求 C++17
- 固定大小线程池，构造时一次性创建 `threadNum`（默认 4）个 worker
- 任务队列 `std::queue<std::function<void()>>` + `std::condition_variable`：空闲 worker 阻塞休眠，无轮询、无忙等
- `enques(f, args...)` 支持函数指针 / lambda / 仿函数与任意数量参数，返回 `std::future<R>`，可 `get()` 取返回值或异常
- 用 `std::packaged_task` + `std::shared_ptr` 做类型擦除：`packaged_task` 不可拷贝，放进 `std::function` 需要共享所有权
- 停机语义：`stopThread()` 置位后 worker **继续把队列中剩余任务执行完**再退出，不丢任务，随后逐个 `join`
- 提供停止后重启接口（`reStart()` / `reStart(n)`；重启路径存在已知缺陷，见[已知问题](#已知问题与修复记录)）
- `QueueSize()` 查询当前积压任务数（加锁读取）
- 析构自动调用 `stopThread()`，不会因遗漏 join 导致 `std::terminate`
- 任务中的异常被 `packaged_task` 捕获进 `future`，不会冲出 worker 线程
- MSVC 下通过 `/utf-8` 选项保证中文注释与字符串按 UTF-8 解析（避免 C4819）

## 环境依赖

- 支持 C++17 的编译器（MSVC 2017+ / GCC 7+ / Clang 5+）
- CMake >= 3.15（仅用于构建示例，库本身是头文件）
- 无第三方库；Linux 下需要 pthread
- 已验证环境：Windows + MSVC（v145 工具集）+ CMake 4.3.3

## 目录结构

```
ThreadPool/
├── include/
│   └── ThreadPool.h          # 线程池全部实现（header-only，提交）
├── example/
│   └── main.cpp              # 示例：提交 100 个 lambda + 100 个普通函数任务（提交）
├── build/                    # CMake/VS 生成目录与产物（不提交，已由 .gitignore 忽略）
├── out/                      # IDE（VS 的 CMake 集成）输出目录（不提交）
├── .vs/                      # Visual Studio 本机缓存（不提交，且被 VS 独占锁定）
├── CMakeLists.txt            # 示例目标 ExampleThreadPool 的构建脚本（提交）
├── .gitignore                # 忽略规则（当前仅忽略 build/）
├── LICENSE
└── README.md
```

> **提交约定**：`include/`、`example/`、`CMakeLists.txt`、`README.md`、`LICENSE`、`.gitignore` 属于源码，需要提交；
> `build/`、`out/`、`.vs/` 以及 `*.exe` / `*.pdb` / `*.obj` 等产物仅本地保留。
> 其中 `.vs/ThreadPool/FileContentIndex/*.vsidx` 会被 Visual Studio 以独占方式打开，执行 `git add -A` 时会因
> `Permission denied` 直接失败，建议把 `.vs/`、`out/` 补进 `.gitignore`。

## 接口说明

所有接口都在 `ThreadPool::ThreadPool` 类中（命名空间 `ThreadPool`）。

| 接口       | 签名                                                                                                                      | 说明                                                                  |
| ---------- | ------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| 构造       | `explicit ThreadPool(size_t threadNum = 4)`                                                                               | 设置线程数并立即创建 worker；参数非法时静默回退默认线程数             |
| 提交任务   | `template <typename F, typename... Arg> auto enques(F&& f, Arg&&... arg) -> std::future<std::invoke_result_t<F, Arg...>>` | 打包任务入队并唤醒一个 worker，返回 `future`                          |
| 队列长度   | `size_t QueueSize()`                                                                                                      | 当前待执行任务数（加锁读取）                                          |
| 停止       | `bool stopThread()`                                                                                                       | 置停止标志、唤醒全部 worker、等待队列排空后 `join`；当前恒返回 `true` |
| 重启       | `bool reStart()`                                                                                                          | 以当前线程数停止并重建                                                |
| 重启       | `bool reStart(int threadNum)`                                                                                             | 以新线程数停止并重建                                                  |
| 设置线程数 | `bool setThread(int threadNum)`                                                                                           | `threadNum <= 0` 返回 `false`，否则仅修改计数                         |
| 创建线程   | `bool createThread()`                                                                                                     | 按 `threadNum` 追加创建 worker                                        |
| 析构       | `~ThreadPool()`                                                                                                           | 自动调用 `stopThread()`                                               |

### 行为约定

- `enques` 在停机状态下抛 `std::runtime_error("error:ThreadPool on off")`，调用方需自行捕获。
- 任务的返回值与异常都保存在 `future` 的共享状态里；**提交后不接收 `future` 时，任务抛出的异常会被静默丢弃**，不会导致进程终止。
- `stopThread()` 只置位并等待，不丢弃队列中的任务：worker 看见 `stop == true` 后仍会继续取任务，直到队列为空才返回。
- 析构函数会等待所有已提交任务执行完，因此析构可能阻塞；析构前不要再向已销毁的池提交任务。
- `threadNum` 只是并发上限：真正同时执行的线程数受 `std::thread::hardware_concurrency()`（逻辑核数）与操作系统调度限制。
- 线程安全边界：`enques` / `QueueSize` 可被多个线程并发调用；`stopThread` / `reStart` / `setThread` 属于管理操作，**不要与 `enques` 并发调用**。
- 可调用对象与参数会被 `std::bind` 拷贝，因此需要可拷贝（移动-only 的 lambda 不能直接提交）。

## 编译与运行

### Windows (MSVC)

```powershell
cmake -B build
cmake --build build --config Debug
.\build\Debug\ExampleThreadPool.exe
```

### Linux (GCC / Clang)

```bash
g++ -std=c++17 -O2 -pthread -Iinclude example/main.cpp -o threadpool_example
./threadpool_example
```

> `CMakeLists.txt` 目前没有 `find_package(Threads)`：在 Linux 下用 CMake 构建时需自行补
> `target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)`，否则链接 `std::thread` 会失败。

生成的目标：

| 目标                | 类型       | 说明                                                        |
| ------------------- | ---------- | ----------------------------------------------------------- |
| `ExampleThreadPool` | 可执行文件 | 示例程序，提交 100 个 lambda 任务与 100 个 `test(int)` 任务 |

## 使用示例

### 提交任务并取回结果

```cpp
#include "ThreadPool.h"

#include <iostream>

int main()
{
    ThreadPool::ThreadPool pool(4);   // 4 个 worker，默认参数为 4

    // 无参 lambda，返回值经 future 取回
    auto answer = pool.enques([] { return 42; });

    // 带参 lambda
    auto sum = pool.enques([](int a, int b) { return a + b; }, 1, 2);

    std::cout << answer.get() << " " << sum.get() << std::endl;   // 42 3
    return 0;   // 析构时自动停机并 join
}
```

### 提交普通函数

```cpp
void test(int i)
{
    std::cout << "这是test的第" << i << "个" << std::endl;
}

pool.enques(test, 1);
```

### fire-and-forget 模式

```cpp
for (int i = 0; i < 100; ++i)
{
    pool.enques([i] { std::cout << "这是lamada的第" << i << "个" << std::endl; });
}
```

不接收返回的 `future` 时任务照常执行，但异常不会被观察到；同时注意 `std::cout` 的**多条 `operator<<` 不是原子操作**，多线程输出会字符级交错，需要按行输出时请自己加锁或先拼成一条字符串再一次输出。

### 停止与重启

```cpp
pool.stopThread();    // 排空队列后 join；此后 enques 会抛 std::runtime_error
pool.reStart(8);      // 以 8 个线程重新启动（当前实现有缺陷，见“已知问题”）
```

## 设计说明

### 线程模型

```
enques() ──► [ workQue ] ──cv.notify_one()──► worker[0..N-1]
                 ▲                                 │
                 └────────── mutex 保护 ───────────┘
```

没有独立的调度线程：提交者只负责入队 + 唤醒，worker 自己从队列取任务执行。

### 1. 任务表示与类型擦除

`enques` 用 `std::invoke_result_t<F, Arg...>` 推导返回值类型 `R`，再构造 `std::packaged_task<R()>`：

```cpp
using functype = std::invoke_result_t<F, Arg...>;
auto task = std::make_shared<std::packaged_task<functype()>>(
    std::bind(std::forward<F>(f), std::forward<Arg>(arg)...));
```

`packaged_task` 不可拷贝，而队列元素是 `std::function<void()>`（要求可拷贝），所以用 `shared_ptr` 包一层，lambda 只捕获这个指针。`packaged_task` 同时提供了「类型擦除的可调用包装」与「内置 promise 共享状态」：调用 `operator()` 时执行任务，把返回值或异常写进共享状态，外部通过 `future` 读取。

### 2. 队列与唤醒

worker 用一个谓词版 `cv.wait`，避免虚假唤醒与丢失唤醒：

```cpp
cv.wait(lock, [this] { return this->stop || !this->workQue.empty(); });
```

谓词在持锁状态下求值，因此不会出现「通知先于等待」导致永久阻塞的情况；`stop` 为 `std::atomic<bool>`，供停机路径无锁读取。

### 3. 停机顺序

`stopThread()` 的顺序是：置 `stop = true` → `notify_all()` → 逐个 `join()`。worker 被唤醒后先检查 `stop && workQue.empty()`，因此停机不会丢弃排队任务，代价是析构可能等待较久（队列越长等得越久）。

### 4. 异常流向

任务内抛出的异常不会穿过 `worker()`：它被 `packaged_task` 捕获并存入共享状态，只有调用 `future.get()` 时才重新抛出。因此「任务异常」与「线程池内部错误」是两条独立路径：后者（如停机后调用 `enques`）直接抛 `runtime_error`。

### 5. 并发度

线程数只是上限。100 个任务提交给 16 个 worker 时，实际同时运行的线程数不超过 `min(16, 逻辑核数)`；控制台输出类任务的耗时主要在 I/O 锁上，CPU 占用会很低，不能据此判断并行度。要确认实际并行度可打印 `std::this_thread::get_id()` 或用 `std::thread::hardware_concurrency()` 对照。

## 已知问题与修复记录

以下问题均对应当前仓库中的代码，**修复尚未合入**，按影响从大到小排列。

- **`reStart()` 之后析构 abort（已修复）**
  `stopThread()` join 完线程后没有清空 `workers`，`createThread()` 又向同一个 vector 追加，`workers.size()` 变成 `2 * threadNum`：
  ① `createThread()` 的 `if (workers.size() != threadNum)` 判断失败并返回 `false`（但新线程其实已创建）；
  ② 析构时再次 `stopThread()`，对前 16 个**已 join 过**的 `std::thread` 调用 `join()`，抛出 `std::system_error`；
  ③ 异常从析构函数逃逸（析构默认 `noexcept`），进程 `std::terminate()`。
  示例 `example/main.cpp` 正好调用了 `reStart()`，所以程序在 `return 0` 之后崩溃。
  修复方式（`stopThread()` 的 join 循环之后）：

  ```cpp
  for (auto& t : workers)
  {
      if (t.joinable())
          t.join();
  }
  workers.clear();   // 关键：保持 workers 只包含“活线程”，否则下次 createThread 后 size 翻倍
  ```

## 许可证

MIT License，见 [LICENSE](LICENSE)。

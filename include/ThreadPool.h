#pragma once

#include <thread>
#include <vector>
#include <mutex>
#include <functional>
#include <queue>
#include <future>
#include <stdexcept>

/// @brief 线程池命名空间
namespace ThreadPool
{
    /// @brief 线程池管理对象
    /// @details 当前线程池创建后容量需在停止后重新设置后再创建；允许停机
    class ThreadPool
    {
    public:
        // 唯一构造（默认创建4个线程）
        /// @brief 唯一构造
        /// @details 默认创建4个线程
        /// @param[in] threadNum 创建的线程数
        explicit ThreadPool(size_t threadNum = 4) : stop(0)
        {
            // 创建线程赋值
            setThread(threadNum);

            // 创建线程
            createThread();
        }

        /// @brief 创建线程
        /// @return 创建是否成功为返回值
        bool createThread()
        {
            // 循环num_threads此，去创建num_threads的线程数
            for (size_t i = 0; i < threadNum; i++)
            {
                // workers内部通过匿名函数指向私有worker函数构造子线程
                workers.emplace_back([this] { this->worker(); });
            }

            // 判断是否创建完全
            if (workers.size() != threadNum)
            {
                return false;
            }

            return true;
        }

        // F为单个固定的模版参数，Arg为不固定数量的模版类型
        template <typename F, typename... Arg>
        /// @brief 放入执行队列中
        /// @param[in] f 函数名
        /// @param[in] arg 不定参
        /// @return 异步返回调用结果
        auto enques(F&& f, Arg&&... arg) -> std::future<std::invoke_result_t<F, Arg...>>
        {
            // 获取得到的函数类型
            // 用类型为 F 的可调用对象，以 Args... 这些类型作为参数，按标准 INVOKE 规则调用之后，返回值是什么类型
            using functype = std::invoke_result_t<F, Arg...>;

            // 打包一个传入函数为异步任务
            // packaged_task = type-erased 可调用包装（类似 function） + 内置 promise 共享状态；调用 operator()
            // 的时候自动跑任务、把返回值 / 异常塞进共享状态，外部用 future<R> 拿结果
            // std::forward<T>(arg) 是一个有条件的强制转换，保留参数原来的值类别（左值 /
            // 右值），它只在模板推导语境下才生效，单独拿出来用没有意义。
            auto task = std::make_shared<std::packaged_task<functype()>>(
                std::bind(std::forward<F>(f), std::forward<Arg>(arg)...));

            // 获取上一步打包的异步函数的结果
            std::future<functype> retfuture = task->get_future();

            // 定义锁的作用域避免死锁
            {
                // 为线程中调用函数加一个智能锁
                std::lock_guard<std::mutex> lock_mtx(this->mtx);
                // 判断是否线程池停止
                if (stop)
                    // 抛出停止异常
                    throw std::runtime_error("error:ThreadPool on off");
                // 将异步任务的函数名解出并加入线程队列
                workQue.emplace(
                    [this, task]()
                    {
                        // 在这里可以看出一个函数指针调用对象，同时上面bind了数据和函数名，所以可以直接用(*task)()
                        (*task)();
                    });
            }

            // 唤醒一个线程去执行队列拿线程和任务执行
            cv.notify_one();

            // 返回异步执行的结果
            return retfuture;
        }

        /// @brief 重启线程
        /// @return 返回运行状态
        bool reStart()
        {
            // 关闭线程
            if (!stopThread())
            {
                return false;
            }

            //创建线程
            if (!createThread())
            {
                return false;
            }

            return true;
        }

        /// @brief 用于重启时重设线程数
        /// @param threadNum 
        /// @return 返回运行状态
        bool reStart(int threadNum)
        {
            // 关闭线程
            if (!stopThread())
            {
                return false;
            }

            // 重设线程数
            if (!setThread(threadNum))
            {
                return false;
            }

            //创建线程
            if (!createThread())
            {
                return false;
            }

            return true;
        }

        /// @brief 获取消息队列长度
        /// @return 返回size_t类型的长度
        size_t QueueSize()
        {
            std::lock_guard<std::mutex> lock(this->mtx);
            return workQue.size();
        }

        /// @brief 用于停止线程的
        /// @return 返回是否停止成功
        bool stopThread()
        {
            // 先置停止状态
            stop = true;

            // 唤醒所有的阻塞线程
            cv.notify_all();

            // 逐个关闭创建的线程
            for (auto it = workers.begin(); it != workers.end(); it++)
            {
                // 等待线程结束
                it->join();
            }

            return true;
        }

        /// @brief 设置线程数
        /// @param theardNum 传入你想设置的大小
        /// @return 是否修改成功
        bool setThread(int threadNum)
        {
            // 判断是否正确
            if (threadNum <= 0)
            {
                return false;
            }
            // 赋值
            this->threadNum = threadNum;

            return true;
        }

        /// @brief 析构函数
        /// @details 用于关闭所有的线程连接
        ~ThreadPool() // 析构函数
        {
            stopThread();
        }

    private:
        ///@brief 线程创建
        /// @details 在初期创建线程，并使其休眠，等待任务队列的唤醒
        void worker()
        {
            // 死循环
            while (1)
            {
                // 定义一临时存储异步任务的变量
                std::function<void()> task;

                {
                    // 给mtx上锁
                    std::unique_lock<std::mutex> lock(mtx);

                    // 等待条件（是否是stop变量为false，或者任务队列是否为空）
                    cv.wait(lock, [this] { return this->stop || !this->workQue.empty(); });

                    // stop==1并且任务队列为空，则关闭线程
                    if (stop && workQue.empty())
                    {
                        return;
                    }

                    // 给队列上面的赋值给task（获得函数的签名）
                    task = std::move(this->workQue.front());

                    // 出队
                    this->workQue.pop();
                }

                // 执行task
                task();
            }
        }

        /// @brief 存储的线程池数量
        int threadNum = 4;
        // 记录线程池是否停止
        std::atomic<bool> stop = false;
        // 条件变量
        std::condition_variable cv;
        // 互斥锁
        std::mutex mtx;
        // 线程集合（线程池）
        std::vector<std::thread> workers;
        // 任务队列
        std::queue<std::function<void()>> workQue;
    };

} // namespace ThreadPool
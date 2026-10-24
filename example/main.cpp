#include "ThreadPool.h"

#include <iostream>

void test(int i)
{
    std::cout << "这是test的第" << i << "个" << std::endl;
}

int main()
{
    std::unique_ptr<ThreadPool::ThreadPool> Pool = std::make_unique<ThreadPool::ThreadPool>(16);

    for (int i = 0; i < 100; i++)
    {
        Pool->enques([i]() { std::cout << "这是lamada的第" << i << "个" << std::endl; });
    }

    Pool->reStart();

    for (int i = 0; i < 100; i++)
    {
        Pool->enques(test, i);
    }

    return 0;
}
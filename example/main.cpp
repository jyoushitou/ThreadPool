#include "ThreadPool.h"

#include <iostream>

void test(int i)
{
    std::cout << "this is test function " << i << ";" << std::endl;
}

int main()
{
    std::unique_ptr<ThreadPool::ThreadPool> Pool = std::make_unique<ThreadPool::ThreadPool>(16);

    for (int i = 0; i < 100; i++)
    {
        Pool->enques(
            [i]()
            {
                std::cout << "thread is " << std::this_thread::get_id() << " this is lamada " << i << ";" << std::endl;
            });
    }

    Pool->reStart();

    for (int i = 0; i < 100; i++)
    {
        Pool->enques(test, i);
    }

    return 0;
}
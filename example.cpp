#include "thread_pool.h"
#include <iostream>

struct tt {int a;};

int main(){
    thread_pool pool(10, 5);

    auto sleepy_thread = [](auto delay){
        std::this_thread::sleep_for(delay);
    };

    auto initial_time = std::chrono::steady_clock::now();

    std::future<void> f;

    for(int i = 0; i < 40; i++){
        f = pool.add_task(sleepy_thread, std::chrono::milliseconds(100));
    }

    f.get();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - initial_time);

    std::cout << "Time: " << duration.count() << '\n';

    return 0;
}
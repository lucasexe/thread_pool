#ifndef __THREAD_POOL__
#define __THREAD_POOL__

#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <future>

class thread_pool {
public:
    thread_pool(const size_t max_threads = std::thread::hardware_concurrency(), const size_t initial_threads = 0) :
        m_idle_workers { initial_threads },
        m_max_threads { max_threads }
    {
        m_threads.reserve(max_threads);
        for(size_t i = 0; i < initial_threads; ++i){
            m_threads.emplace_back(&thread_pool::run_worker, this);
        }
    }

    ~thread_pool(){
        m_locker.lock();
        m_stop = true;
        m_locker.unlock();
        m_cond_var.notify_all();
        for(auto& worker : m_threads){
            if(worker.joinable()){
                worker.join();
            }
        }
    }

    template<class Func, class... Args>
    auto add_task(Func&& task, Args&&... args){
        std::unique_lock<decltype(m_locker)> locker(m_locker);
        using return_type = std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>;

        std::promise<return_type> promise;
        auto future = promise.get_future();

        m_tasks_queue.push([
                fake_promise=_fake_copyable_(std::move(promise)),
                task=std::forward<Func>(task),
                args=std::tuple<Args...>(std::forward<Args>(args)...)
            ](){
            auto promise = fake_promise.get();
            if constexpr (std::is_same_v<return_type, void>){
                std::apply(task, args);
                promise.set_value();
            }else{
                promise.set_value( std::apply(task, args) );
            }
        });
        wakeup_worker();
        return future;
    }

private:
    void run_worker(){
        while(true){
            std::unique_lock<decltype(m_locker)> locker(m_locker);
            m_cond_var.wait(locker, [this](){ return !m_tasks_queue.empty() || m_stop; });
            if(m_stop){
                return;
            }
            auto task = m_tasks_queue.front();
            m_tasks_queue.pop();
            --m_idle_workers;
            wakeup_worker(); // wakeup/create other(s) worker(s) if necessary
            locker.unlock();
            task(); // do the user task
            locker.lock();
            ++m_idle_workers;
        }
    }

    void wakeup_worker(){
        if(!m_tasks_queue.empty()){
            size_t new_workers = std::min(m_tasks_queue.size() - m_idle_workers, m_max_threads - m_threads.size());
            for(size_t i = 0; i < new_workers; i++){
                m_threads.emplace_back(&thread_pool::run_worker, this);
            }
            m_idle_workers += new_workers;
            if(m_idle_workers > 0){
                m_cond_var.notify_one();
            }
        }
    }

    std::mutex m_locker;
    std::condition_variable m_cond_var;
    std::vector<std::thread> m_threads;
    std::queue<std::function<void()>> m_tasks_queue;
    size_t m_idle_workers;
    size_t m_max_threads;
    bool m_stop = false;
  
    template<class T>
    struct _fake_copyable_ {
        mutable T m_store;

        _fake_copyable_(T&& arg) : m_store { std::forward<T>(arg) } { }
        _fake_copyable_(const _fake_copyable_& other) : m_store { std::move(other.m_store) } { }
        _fake_copyable_(_fake_copyable_&& other) : m_store { std::move(other.m_store) } { }
        auto get() const { return std::move(m_store); }
    };
};

#endif
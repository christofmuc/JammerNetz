#pragma  once

// See https://stackoverflow.com/questions/31090302/is-an-object-pool-pattern-of-shared-ptr-possible

#include <list>
#include <mutex>
#include <algorithm>
#include <memory>
#include <stdexcept>

template<class T, bool grow_on_demand=true>
class Pool
{
    public:
    Pool(size_t n)
        : mutex_m(), free_m(0), used_m(0)
    {
        for (size_t i=0; i<n; i++)
        {
           free_m.push_front( std::make_unique<T>() );
        }
    }

    std::shared_ptr<T> alloc()
    {
        std::unique_lock<std::mutex> lock(mutex_m);
        if (free_m.empty() )
        {
            if constexpr (grow_on_demand)
            {
                free_m.push_front( std::make_unique<T>() );
            }
            else
            {
                throw std::bad_alloc();
            }
        }
        auto it = free_m.begin();
        std::shared_ptr<T> sptr( it->get(), [this](T* ptr){ this->free(ptr); } );
        used_m.push_front(std::move(*it));
        free_m.erase(it);
        return sptr;
    }


    size_t getFreeCount()
    {
        std::unique_lock<std::mutex> lock(mutex_m);
        return free_m.size();
    }

private:

    void free(T *obj)
    {
        std::unique_lock<std::mutex> lock(mutex_m);
        auto it = std::find_if(used_m.begin(), used_m.end(), [&](std::unique_ptr<T> &p){ return p.get()==obj; } );
        if (it != used_m.end())
        {
          free_m.push_back(std::move(*it));
          used_m.erase(it);
        }
        else
        {
            throw std::runtime_error("unexpected: unknown object freed.");
        }
    }

    std::mutex mutex_m;
    std::list<std::unique_ptr<T>> free_m;
    std::list<std::unique_ptr<T>> used_m;
};

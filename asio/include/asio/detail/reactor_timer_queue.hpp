//
// reactor_timer_queue.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003, 2004 Christopher M. Kohlhoff (chris@kohlhoff.com)
//
// Permission to use, copy, modify, distribute and sell this software and its
// documentation for any purpose is hereby granted without fee, provided that
// the above copyright notice appears in all copies and that both the copyright
// notice and this permission notice appear in supporting documentation. This
// software is provided "as is" without express or implied warranty, and with
// no claim as to its suitability for any purpose.
//

#ifndef ASIO_DETAIL_REACTOR_TIMER_QUEUE_HPP
#define ASIO_DETAIL_REACTOR_TIMER_QUEUE_HPP

#include "asio/detail/push_options.hpp"

#include "asio/detail/push_options.hpp"
#include <functional>
#include <vector>
#include <boost/noncopyable.hpp>
#include "asio/detail/pop_options.hpp"

#include "asio/detail/hash_map.hpp"

namespace asio {
namespace detail {

template <typename Time, typename Comparator = std::less<Time> >
class reactor_timer_queue
  : private boost::noncopyable
{
public:
  // Constructor.
  reactor_timer_queue()
    : timers_(),
      heap_()
  {
  }

  // Add a new timer to the queue. Returns true if this is the timer that is
  // earliest in the queue, in which case the reactor's event demultiplexing
  // function call may need to be interrupted and restarted.
  template <typename Handler>
  bool enqueue_timer(const Time& time, Handler handler, void* token)
  {
    // Create a new timer object.
    timer_base* new_timer = new timer<Handler>(time, handler, token);

    // Insert the new timer into the hash.
    typedef typename hash_map<void*, timer_base*>::iterator iterator;
    typedef typename hash_map<void*, timer_base*>::value_type value_type;
    std::pair<iterator, bool> result =
      timers_.insert(value_type(token, new_timer));
    if (!result.second)
    {
      result.first->second->prev_ = new_timer;
      new_timer->next_ = result.first->second;
      result.first->second = new_timer;
    }

    // Put the timer at the correct position in the heap.
    new_timer->heap_index_ = heap_.size();
    heap_.push_back(new_timer);
    up_heap(heap_.size() - 1);
    return (heap_[0] == new_timer);
  }

  // Whether there are no timers in the queue.
  bool empty() const
  {
    return heap_.empty();
  }

  // Get the time for the timer that is earliest in the queue.
  void get_earliest_time(Time& time)
  {
    time = heap_[0]->time_;
  }

  // Dispatch the timers that are earlier than the specified time.
  void dispatch_timers(const Time& time)
  {
    Comparator comp;
    while (!heap_.empty() && comp(heap_[0]->time_, time))
    {
      timer_base* t = heap_[0];
      remove_timer(t);
      t->do_operation();
      delete t;
    }
  }

  // Cancel the timer with the given token. The handler's do_cancel operation
  // will be invoked immediately.
  void cancel_timer(void* timer_token)
  {
    typedef typename hash_map<void*, timer_base*>::iterator iterator;
    iterator it = timers_.find(timer_token);
    if (it != timers_.end())
    {
      timer_base* t = it->second;
      while (t)
      {
        timer_base* next = t->next_;
        remove_timer(t);
        t->do_cancel();
        delete t;
        t = next;
      }
    }
  }

private:
  // Base class for timer operations.
  class timer_base
  {
  public:
    // Constructor.
    timer_base(const Time& time, void* token)
      : time_(time),
        token_(token),
        next_(0),
        prev_(0),
        heap_index_(~0)
    {
    }

    // Destructor.
    virtual ~timer_base()
    {
    }

    // Perform the timer operation.
    virtual void do_operation() = 0;

    // Handle the case where the timer has been cancelled.
    virtual void do_cancel() = 0;

  private:
    friend class reactor_timer_queue<Time, Comparator>;

    // The time when the operation should fire.
    Time time_;

    // The token associated with the timer.
    void* token_;

    // The next timer known to the queue.
    timer_base* next_;

    // The previous timer known to the queue.
    timer_base* prev_;

    // The index of the timer in the heap.
    size_t heap_index_;
  };

  // Adaptor class template for using handlers in timers.
  template <typename Handler>
  class timer
    : public timer_base
  {
  public:
    // Constructor.
    timer(const Time& time, Handler handler, void* token)
      : timer_base(time, token),
        handler_(handler)
    {
    }

    // Perform the timer operation.
    virtual void do_operation()
    {
      handler_.do_operation();
    }

    // Handle the case where the timer has been cancelled.
    virtual void do_cancel()
    {
      handler_.do_cancel();
    }

  private:
    Handler handler_;
  };

  // Move the item at the given index up the heap to its correct position.
  void up_heap(size_t index)
  {
    Comparator comp;
    while (index > 0 && comp(heap_[index]->time_, heap_[index / 2]->time_))
    {
      swap_heap(index, index / 2);
      index = index / 2;
    }
  }

  // Move the item at the given index down the heap to its correct position.
  void down_heap(size_t index)
  {
    Comparator comp;
    size_t child = index * 2 + 1;
    while (child < heap_.size())
    {
      size_t min_child =
        (child + 1 == heap_.size() || heap_[child] < heap_[child + 1])
        ? child : child + 1;
      if (comp(heap_[index]->time_, heap_[min_child]->time_))
        break;
      swap_heap(index, min_child);
      index = min_child;
      child = index * 2 + 1;
    }
  }

  // Swap two entries in the heap.
  void swap_heap(size_t index1, size_t index2)
  {
    timer_base* tmp = heap_[index1];
    heap_[index1] = heap_[index2];
    heap_[index2] = tmp;
    heap_[index1]->heap_index_ = index1;
    heap_[index2]->heap_index_ = index2;
  }

  // Remove a timer from the heap and list of timers.
  void remove_timer(timer_base* t)
  {
    // Remove the timer from the heap.
    int heap_index = t->heap_index_;
    if (heap_.size() > 1)
    {
      swap_heap(heap_index, heap_.size() - 1);
      heap_.pop_back();
      Comparator comp;
      if (heap_index > 0 && comp(t->time_, heap_[heap_index / 2]->time_))
        up_heap(heap_index);
      else
        down_heap(heap_index);
    }
    else
    {
      heap_.clear();
    }

    // Remove the timer from the hash.
    typedef typename hash_map<void*, timer_base*>::iterator iterator;
    iterator it = timers_.find(t->token_);
    if (it != timers_.end())
    {
      if (it->second == t)
        it->second = t->next_;
      if (t->prev_)
        t->prev_->next_ = t->next_;
      if (t->next_)
        t->next_->prev_ = t->prev_;
      if (it->second == 0)
        timers_.erase(it);
    }
  }

  // A hash of timer token to linked lists of timers.
  hash_map<void*, timer_base*> timers_;

  // The heap of timers, with the earliest timer at the front.
  std::vector<timer_base*> heap_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_DETAIL_REACTOR_TIMER_QUEUE_HPP

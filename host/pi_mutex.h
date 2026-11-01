/* SPDX-License-Identifier: GPL-3.0-only */
/* A mutex with priority inheritance (GB-008), for the core that the audio thread and the host's state calls share.
 * While the (real-time) audio thread waits for it, its holder runs at the audio thread's priority, so no thread of a
 * priority in between can preempt the holder and stretch the wait (priority inversion): the wait is bounded by the
 * holder's own short critical section. Uncontended, it locks in user space like std::mutex. */
#pragma once
#include <pthread.h>

class PiMutex {
public:
    PiMutex()
    {
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_INHERIT);
        pthread_mutex_init(&m_, &a);
        pthread_mutexattr_destroy(&a);
    }
    ~PiMutex() { pthread_mutex_destroy(&m_); }
    PiMutex(const PiMutex&) = delete;
    PiMutex& operator=(const PiMutex&) = delete;
    void lock() { pthread_mutex_lock(&m_); }
    bool try_lock() { return pthread_mutex_trylock(&m_) == 0; }
    void unlock() { pthread_mutex_unlock(&m_); }

private:
    pthread_mutex_t m_;
};

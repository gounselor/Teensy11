#ifndef _MUTEX_H_
#define _MUTEX_H_

#include <stdint.h>
#include <util/atomic.h>

/** CRTP for mutex classes.

  This is an abstract mutex class. Classes that derive from this class are intended for use in
  other templates, such as ScopedLock. I'm not really sure why I chose the CRTP for this, but I'm
  somehow sure that there won't be many uses of polymorphic mutex containers and such, so it
  shouldn't be a big deal.
**/
template<typename impl>
class AbstractMutex
{
  public:
    /** Lock the mutex, blocking.
    **/
    void lock(void){asChild().lock_impl();}
    /** Try to lock the mutex, non-blocking.
      \return true if the mutex could be locked (i.e. was not locked before), false otherwise.
    **/
    bool tryLock(void){return asChild().tryLock_impl();}
    /** Try to lock the mutex. Blocks until the mutex can be locked or until
      a timeout occurs after the given number of milliseconds.
      \return true if the mutex was not locked before or if the mutex could be locked within the timeout, false otherwise.
    **/
    bool timedLock(const uint32_t& msTimeout) {return asChild().timedLock_impl(msTimeout);}
    /** Get the locked state.
      \return true if the mutex is locked.
    **/
    bool locked() {return asChild().locked_impl();}
    /** Unlock the mutex.
    **/
    void unlock(void){asChild().unlock_impl();}
  protected:
    virtual void lock_impl() {while(!tryLock());}
    virtual bool timedLock_impl(const uint32_t& msTimeout)
    {
      elapsedMillis elapsed;
      bool acquired = false;
      do
      {
        acquired = tryLock();
      }
      while((elapsed <= msTimeout) && (!acquired));
      return acquired;
    }
  private:
    impl& asChild(){return static_cast<impl&>(*this);}
};

/** A simple binary mutex.
**/
class Mutex : public AbstractMutex<Mutex>
{
  public:
    Mutex() : m_locked(false) {}
    virtual bool tryLock_impl()
    {
      bool acquired = false;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
      {
        if (!m_locked)
        {
          m_locked = true;
          acquired = true;
        }
      }
      return acquired;
    }
    virtual bool locked_impl() {return m_locked;}
    virtual void unlock_impl()
    {
      m_locked = false;
    }
  protected:
    volatile bool m_locked;
};

/** Used for tag dispatching in the constructor of ScopedLock.
**/
struct try_lock_t { };
/** Thanks to:
  http://stackoverflow.com/questions/20775285/how-to-properly-use-tag-dispatching-to-pick-a-constructor
**/
constexpr try_lock_t try_lock = try_lock_t();

/** A scoped lock class that unlocks a mutex on desctruction if the lock could be acquired before.
**/
template<typename MutexType>
class ScopedLock
{
  public:
    /** Blocking constructor that acquires the lock.
    **/
    ScopedLock(MutexType& mtx) : m_mtx(mtx), m_acquired(true) {m_mtx.lock();}
    /** Non-blocking constructor that tries to acquire the lock.
    **/
    ScopedLock(MutexType& mtx, try_lock_t) : m_mtx(mtx) {m_acquired = m_mtx.tryLock();}
    /** Constructor that tries to acquire the lock for the specified number of milliseconds
    **/
    ScopedLock(MutexType& mtx, const uint32_t& msTimeout) : m_mtx(mtx) {m_acquired = m_mtx.timedLock(msTimeout);}
    /** Acquire the lock, blocking.
    **/
    void lock()
    {
      if(!owns())
      {
        m_mtx.lock();
        m_acquired = true;
      }
    }
    /** Try to acquire the lock, non-blocking
    **/
    bool tryLock()
    {
      if (!owns())
      {
        m_acquired = m_mtx.tryLock();
      }
      return m_acquired;
    }
    /** Try to acquire the lock for the specified number of milliseconds
    **/
    bool timedLock(const uint32_t& msTimeout)
    {
      if(!owns())
      {
        m_acquired = m_mtx.timedLock(msTimeout);
      }
      return m_acquired;
    }
    /** Check if the lock has been acquired
    **/
    bool owns() const {return m_acquired;}
    /** Release the lock if it has been acquired before.
    **/
    void release()
    {
      if(owns())
      {
        m_mtx.unlock();
        m_acquired = false;
      }
    }
    /** Destructor. Releases the lock if it was acquired before.
    **/
    ~ScopedLock() {release();}
  private:
    MutexType& m_mtx;
    bool m_acquired;
};

#endif // _MUTEX_H_


#ifndef _SPINLOCK_H
#define _SPINLOCK_H

typedef struct spinlock {
    volatile int lock;
} spinlock_t;

static inline void spinlock_init(spinlock_t *lock) {
    lock->lock = 0;
}

static inline void spinlock_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        while (lock->lock) {
            __asm__ __volatile__("pause");
        }
    }
}

static inline void spinlock_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->lock);
}

static inline void spinlock_destroy(spinlock_t *lock) {
    lock->lock = 0;
}

#endif


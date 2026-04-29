/**
 * coco - A production-grade C coroutine library
 *
 * Supports: Linux/macOS (Windows: planned)
 * Features: Stackful coroutine, cooperative scheduling, channel, async I/O
 *
 * @file coco.h
 * @brief Main header file for the coco coroutine library
 *
 * Coco is a production-grade stackful coroutine library for C, featuring:
 * - Cooperative scheduling with multi-level priority support
 * - O(1) timer cancellation via hierarchical timing wheel
 * - Stack pool for memory reuse and reduced allocation overhead
 * - Channel-based inter-coroutine communication
 * - Non-blocking I/O with epoll/kqueue integration
 */

#ifndef COCO_H
#define COCO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Error Codes === */
/** @defgroup ErrorCodes Error Codes
 *  @brief Error codes returned by coco functions
 *  @{
 */
#define COCO_OK                 0    /**< Success */
#define COCO_ERROR             -1    /**< Generic error */
#define COCO_ERROR_NOMEM       -2    /**< Memory allocation failed */
#define COCO_ERROR_STACK_OVERFLOW -3 /**< Stack overflow detected */
#define COCO_ERROR_CHANNEL_CLOSED -4 /**< Channel is closed */
#define COCO_ERROR_INVALID     -5    /**< Invalid argument */
#define COCO_ERROR_CANCELLED   -6    /**< Operation was cancelled */
/** @} */

/* === Coroutine States === */
/** @defgroup CoroutineStates Coroutine States
 *  @brief Possible states of a coroutine
 *  @{
 */
typedef enum coco_state {
    COCO_STATE_CREATED,     /**< Coroutine created but not yet running */
    COCO_STATE_RUNNING,     /**< Coroutine is currently executing */
    COCO_STATE_WAITING,     /**< Coroutine is waiting for I/O or channel */
    COCO_STATE_READY,       /**< Coroutine is ready to be scheduled */
    COCO_STATE_DEAD,        /**< Coroutine has finished execution */
    COCO_STATE_OVERFLOW,    /**< Coroutine stack overflowed (unrecoverable) */
} coco_state_t;
/** @} */

/* === Coroutine Priorities === */
/** @defgroup CoroutinePriorities Coroutine Priorities
 *  @brief Priority levels for coroutine scheduling
 *
 *  Higher priority coroutines are scheduled before lower priority ones.
 *  Within the same priority level, scheduling is FIFO.
 *  @{
 */
typedef enum coco_priority {
    COCO_PRIORITY_HIGH   = 0,  /**< High priority: real-time tasks, critical paths */
    COCO_PRIORITY_NORMAL = 1,  /**< Normal priority: default value */
    COCO_PRIORITY_LOW    = 2,  /**< Low priority: background tasks */
    COCO_PRIORITY_IDLE   = 3,  /**< Idle priority: runs only when no other tasks */
    COCO_PRIORITY_COUNT  = 4,  /**< Number of priority levels */
} coco_priority_t;
/** @} */

/* === Forward Declarations === */
/** @defgroup Types Type Definitions
 *  @brief Forward declarations for coco types
 *  @{
 */
typedef struct coco_coro coco_coro_t;       /**< Coroutine handle */
typedef struct coco_sched coco_sched_t;     /**< Scheduler handle */
typedef struct coco_channel coco_channel_t; /**< Channel handle */
typedef struct coco_timer coco_timer_t;     /**< Timer handle */
/** @} */

/* === Error Callback === */
/**
 * @brief Error callback function type
 * @param coro The coroutine that encountered the error
 * @param error_code The error code (see @ref ErrorCodes)
 * @param msg Human-readable error message
 */
typedef void (*coco_error_cb)(coco_coro_t *coro, int error_code, const char *msg);

/* === Default Configuration === */
/** @defgroup Config Configuration Constants
 *  @brief Default configuration values
 *  @{
 */
#define COCO_DEFAULT_STACK_SIZE   (64 * 1024)   /**< 64KB - default stack size */
#define COCO_STACK_SMALL          (16 * 1024)   /**< 16KB - I/O bound tasks, use with caution */
#define COCO_STACK_MEDIUM         (32 * 1024)   /**< 32KB - general purpose */
#define COCO_STACK_LARGE          (128 * 1024)  /**< 128KB - recursive/large stack frames */
#define COCO_MAX_COROUTINES       10000         /**< Maximum number of coroutines */
/** @} */

/* === I/O Backend Selection === */
/** @defgroup IOBackend I/O Backend Selection
 *  @brief Control over I/O backend selection
 *  @{
 */

/**
 * @brief I/O backend types
 */
typedef enum coco_io_backend {
    COCO_IO_BACKEND_AUTO,    /**< Auto-select: io_uring on Linux 5.1+, epoll otherwise */
    COCO_IO_BACKEND_EPOLL,   /**< Force epoll backend (Linux only) */
    COCO_IO_BACKEND_IOURING  /**< Force io_uring backend (Linux 5.1+ only) */
} coco_io_backend_t;

/**
 * @brief Set the I/O backend for a scheduler
 * @param sched Scheduler pointer
 * @param backend Backend to use
 * @return COCO_OK on success, COCO_ERROR on failure
 *
 * Must be called before coco_sched_run(). If the requested backend
 * is unavailable, returns COCO_ERROR.
 */
int coco_sched_set_io_backend(coco_sched_t *sched, coco_io_backend_t backend);

/**
 * @brief Get the current I/O backend
 * @param sched Scheduler pointer
 * @return Current backend type
 */
coco_io_backend_t coco_sched_get_io_backend(coco_sched_t *sched);

/** @} */

/* === I/O Configuration API === */
/** @defgroup IOConfig I/O Configuration API
 *  @brief Configuration options for I/O backends
 *  @{
 */

/**
 * @brief I/O configuration options
 */
typedef struct coco_io_options {
    uint32_t queue_depth;       /**< Queue depth (default 256) */
    bool sqpoll_enabled;        /**< Enable SQPOLL (default true on Linux 5.11+) */
    int sqpoll_cpu;             /**< SQPOLL CPU affinity (-1 = no binding) */
    uint32_t sqpoll_idle_ms;    /**< SQPOLL idle timeout in ms (default 1000) */
} coco_io_options_t;

/**
 * @brief Set I/O configuration options
 * @param sched Scheduler pointer
 * @param options Configuration options
 * @return COCO_OK on success, COCO_ERROR on failure
 *
 * Must be called before coco_sched_run(). Options are applied
 * during I/O backend initialization.
 */
int coco_sched_set_io_options(coco_sched_t *sched, const coco_io_options_t *options);

/**
 * @brief Get current I/O configuration
 * @param sched Scheduler pointer
 * @param options Output configuration
 * @return COCO_OK on success, COCO_ERROR on failure
 */
int coco_sched_get_io_options(coco_sched_t *sched, coco_io_options_t *options);

/** @} */

/* === Batch I/O API === */
/** @defgroup BatchIO Batch I/O API
 *  @brief Batch I/O operations for reduced syscall overhead
 *  @{
 */

/**
 * @brief Batch I/O context handle
 */
typedef struct coco_batch_io coco_batch_io_t;

/**
 * @brief Batch operation type
 */
typedef enum coco_batch_op {
    COCO_BATCH_READ,    /**< Batch read operation */
    COCO_BATCH_WRITE    /**< Batch write operation */
} coco_batch_op_t;

/**
 * @brief Batch operation result
 */
typedef struct coco_batch_result {
    int fd;             /**< File descriptor */
    ssize_t result;     /**< Bytes transferred or error code */
} coco_batch_result_t;

/**
 * @brief Begin a batch I/O context
 * @param sched Scheduler pointer
 * @return Batch context, or NULL on failure
 *
 * Creates a batch context for accumulating I/O operations.
 * Operations are not submitted until coco_batch_submit() is called.
 */
coco_batch_io_t *coco_batch_begin(coco_sched_t *sched);

/**
 * @brief Add a read operation to batch
 * @param batch Batch context
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Maximum bytes to read
 * @return COCO_OK on success, negative error code on failure
 */
int coco_batch_add_read(coco_batch_io_t *batch, int fd, void *buf, size_t count);

/**
 * @brief Add a write operation to batch
 * @param batch Batch context
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Bytes to write
 * @return COCO_OK on success, negative error code on failure
 */
int coco_batch_add_write(coco_batch_io_t *batch, int fd, const void *buf, size_t count);

/**
 * @brief Submit batch I/O and wait for completion
 * @param batch Batch context
 * @param results Result array (optional, can be NULL)
 * @param max_results Maximum results to store
 * @return Number of completed operations, or negative error code
 *
 * Submits all accumulated operations and blocks until completion.
 * After this call, the batch context is automatically ended.
 */
int coco_batch_submit(coco_batch_io_t *batch, coco_batch_result_t *results, size_t max_results);

/**
 * @brief Cancel a batch I/O
 * @param batch Batch context
 * @return COCO_OK on success, negative error code on failure
 */
int coco_batch_cancel(coco_batch_io_t *batch);

/**
 * @brief End a batch context without submitting
 * @param batch Batch context
 *
 * Discards all accumulated operations and frees the context.
 */
void coco_batch_end(coco_batch_io_t *batch);

/** @} */

/* === Scheduler API === */
/** @defgroup Scheduler Scheduler API
 *  @brief Functions for managing the coroutine scheduler
 *  @{
 */

/**
 * @brief Create a new scheduler
 * @return Scheduler pointer, or NULL on failure
 *
 * The scheduler manages coroutine execution, I/O events, and timers.
 * Each scheduler has its own stack pool for memory reuse.
 */
coco_sched_t *coco_sched_create(void);

/**
 * @brief Destroy a scheduler and free all resources
 * @param sched Scheduler pointer
 *
 * This will terminate all running coroutines and free associated memory.
 * The stack pool is destroyed, returning all memory to the system.
 */
void coco_sched_destroy(coco_sched_t *sched);

/**
 * @brief Run the scheduler until all coroutines finish
 * @param sched Scheduler pointer
 * @return COCO_OK on success, negative error code on failure
 *
 * This is a blocking call that runs the event loop until there are
 * no more ready or waiting coroutines.
 */
int coco_sched_run(coco_sched_t *sched);

/**
 * @brief Perform a single scheduling iteration
 * @param sched Scheduler pointer
 * @return COCO_OK on success, negative error code on failure
 *
 * Processes one ready coroutine and handles any expired timers.
 * Useful for integrating with external event loops.
 */
int coco_sched_run_once(coco_sched_t *sched);

/**
 * @brief Get the current scheduler for this thread
 * @return Scheduler pointer, or NULL if no scheduler is active
 *
 * Each thread can have its own scheduler. This function returns
 * the scheduler associated with the calling thread.
 */
coco_sched_t *coco_sched_get_current(void);
/** @} */

/* === Coroutine Lifecycle API === */
/** @defgroup CoroutineLifecycle Coroutine Lifecycle API
 *  @brief Functions for creating and managing coroutine lifecycles
 *  @{
 */

/**
 * @brief Create a new coroutine
 * @param sched Scheduler pointer
 * @param entry Entry function
 * @param arg Argument passed to entry function
 * @param stack_size Stack size in bytes (0 for default)
 * @return Coroutine pointer, or NULL on failure
 *
 * The coroutine is created in COCO_STATE_CREATED state and is automatically
 * added to the ready queue. The stack is allocated from the scheduler's
 * stack pool for memory reuse.
 */
coco_coro_t *coco_create(coco_sched_t *sched, void (*entry)(void*), void *arg, size_t stack_size);

/**
 * @brief Exit the current coroutine with a result
 * @param coro Coroutine pointer (typically from coco_self())
 * @param result Return value to be retrieved by coco_join()
 *
 * Must be called from within the coroutine. After calling, the coroutine
 * enters COCO_STATE_DEAD state.
 */
void coco_exit(coco_coro_t *coro, void *result);

/**
 * @brief Yield execution to the scheduler
 *
 * Must be called from within a coroutine. The coroutine remains in
 * COCO_STATE_READY state and will be rescheduled later.
 */
void coco_yield(void);

/**
 * @brief Wait for a coroutine to finish and get its result
 * @param coro Coroutine pointer
 * @return The result passed to coco_exit(), or NULL
 *
 * Blocks the current coroutine until the target coroutine finishes.
 * Must be called from within a coroutine.
 */
void *coco_join(coco_coro_t *coro);

/**
 * @brief Destroy a finished coroutine
 * @param coro Coroutine pointer
 *
 * The coroutine must be in COCO_STATE_DEAD state.
 * Returns the stack to the scheduler's pool for reuse.
 */
void coco_destroy(coco_coro_t *coro);
/** @} */

/* === Coroutine Query API === */
/** @defgroup CoroutineQuery Coroutine Query API
 *  @brief Functions for querying coroutine state and properties
 *  @{
 */

/**
 * @brief Get the currently running coroutine
 * @return Current coroutine pointer, or NULL if not in a coroutine
 */
coco_coro_t *coco_self(void);

/**
 * @brief Get the state of a coroutine
 * @param coro Coroutine pointer
 * @return Current state (COCO_STATE_DEAD if coro is NULL)
 */
coco_state_t coco_get_state(coco_coro_t *coro);

/**
 * @brief Get the unique ID of a coroutine
 * @param coro Coroutine pointer
 * @return Unique 64-bit ID, or 0 if coro is NULL
 */
uint64_t coco_get_id(coco_coro_t *coro);

/**
 * @brief Set the error callback for a coroutine
 * @param coro Coroutine pointer
 * @param cb Error callback function
 *
 * The callback is invoked when the coroutine encounters an error
 * such as stack overflow.
 */
void coco_set_error_cb(coco_coro_t *coro, coco_error_cb cb);

/**
 * @brief Set the priority of a coroutine
 * @param coro Coroutine pointer
 * @param priority Priority level (see @ref CoroutinePriorities)
 *
 * Higher priority coroutines are scheduled before lower priority ones.
 */
void coco_set_priority(coco_coro_t *coro, coco_priority_t priority);

/**
 * @brief Get the priority of a coroutine
 * @param coro Coroutine pointer
 * @return Priority level (COCO_PRIORITY_NORMAL if coro is NULL)
 */
coco_priority_t coco_get_priority(coco_coro_t *coro);

/**
 * @brief Get the stack usage of a coroutine
 * @param coro Coroutine pointer
 * @return Used bytes, or 0 on error
 *
 * Note: This samples at yield/exit points and may underestimate
 * the peak usage during deep recursion.
 */
size_t coco_get_stack_usage(coco_coro_t *coro);
/** @} */

/* === Channel API === */
/** @defgroup Channel Channel API
 *  @brief Inter-coroutine communication via channels
 *  @{
 */

/**
 * @brief Create a new channel
 * @param capacity Buffer size (0 = unbuffered/synchronous)
 * @return Channel pointer, or NULL on failure
 */
coco_channel_t *coco_channel_create(size_t capacity);

/**
 * @brief Send a value through the channel (blocking)
 * @param ch Channel pointer
 * @param value Value to send
 * @return COCO_OK on success, COCO_ERROR_CHANNEL_CLOSED if closed
 *
 * For buffered channels, returns immediately if buffer has space.
 * For unbuffered channels, blocks until a receiver is ready.
 */
int coco_channel_send(coco_channel_t *ch, void *value);

/**
 * @brief Receive a value from the channel (blocking)
 * @param ch Channel pointer
 * @param value Pointer to receive the value
 * @return COCO_OK on success, COCO_ERROR_CHANNEL_CLOSED if closed
 *
 * Blocks until a value is available or the channel is closed.
 */
int coco_channel_recv(coco_channel_t *ch, void **value);

/**
 * @brief Close a channel
 * @param ch Channel pointer
 *
 * After closing, senders will receive COCO_ERROR_CHANNEL_CLOSED.
 * Receivers can still drain remaining buffered values.
 */
void coco_channel_close(coco_channel_t *ch);

/**
 * @brief Destroy a channel
 * @param ch Channel pointer
 *
 * The channel must be closed first. Frees all resources.
 */
void coco_channel_destroy(coco_channel_t *ch);
/** @} */

/* === I/O API === */
/** @defgroup IO I/O API
 *  @brief Non-blocking I/O operations for coroutines
 *  @{
 */

/**
 * @brief Read from a file descriptor (blocking for coroutines)
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Maximum bytes to read
 * @return Bytes read, or negative error code
 *
 * Yields to the scheduler while waiting for data.
 */
int coco_read(int fd, void *buf, size_t count);

/**
 * @brief Write to a file descriptor (blocking for coroutines)
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Maximum bytes to write
 * @return Bytes written, or negative error code
 *
 * Yields to the scheduler while waiting for write readiness.
 */
int coco_write(int fd, const void *buf, size_t count);

/**
 * @brief Accept a connection (blocking for coroutines)
 * @param fd Listening socket
 * @param addr Client address buffer (struct sockaddr*)
 * @param addrlen Address buffer length (input/output)
 * @return New socket fd, or negative error code
 *
 * Yields to the scheduler while waiting for a connection.
 */
int coco_accept(int fd, void *addr, size_t *addrlen);

/**
 * @brief Connect to a remote host (blocking for coroutines)
 * @param fd Socket
 * @param addr Remote address (struct sockaddr*)
 * @param addrlen Address length
 * @return COCO_OK on success, negative error code on failure
 *
 * Yields to the scheduler while connecting.
 */
int coco_connect(int fd, const void *addr, size_t addrlen);

/**
 * @brief Sleep for a duration
 * @param ms Milliseconds to sleep
 * @return COCO_OK
 *
 * Yields to the scheduler. Other coroutines can run during the sleep.
 */
int coco_sleep(uint64_t ms);
/** @} */

/* === Timer API === */
/** @defgroup Timer Timer API
 *  @brief Timer management for delayed execution
 *  @{
 */

/**
 * @brief Create a one-shot timer
 * @param delay_ms Delay in milliseconds
 * @param callback Callback function to invoke
 * @param arg Argument passed to callback
 * @return Timer pointer, or NULL on failure
 *
 * The timer is automatically freed after the callback executes.
 * Use coco_timer_cancel() to cancel before it fires.
 */
coco_timer_t *coco_timer(uint64_t delay_ms, void (*callback)(void*), void *arg);

/**
 * @brief Cancel a timer (O(1) operation)
 * @param timer Timer pointer
 *
 * Removes the timer from the timing wheel and frees it.
 * Safe to call even if the timer has already fired.
 */
void coco_timer_cancel(coco_timer_t *timer);

/**
 * @brief Create a timer with explicit scheduler
 * @param sched Scheduler pointer
 * @param delay_ms Delay in milliseconds
 * @param callback Callback function to invoke
 * @param arg Argument passed to callback
 * @return Timer pointer, or NULL on failure
 *
 * Explicit scheduler version for multi-scheduler scenarios.
 * Each thread can have its own scheduler via TLS.
 */
coco_timer_t *coco_timer_ex(coco_sched_t *sched, uint64_t delay_ms, void (*callback)(void*), void *arg);
/** @} */

/* === Cancellation API === */
/** @defgroup Cancellation Cancellation API
 *  @brief Coroutine cancellation support
 *  @{
 */

/**
 * @brief Cancel a coroutine
 * @param coro Coroutine to cancel
 * @return COCO_OK on success, COCO_ERROR on failure
 *
 * The coroutine will receive COCO_ERROR_CANCELLED at its next yield point.
 */
int coco_cancel(coco_coro_t *coro);

/**
 * @brief Check if the current coroutine is cancelled
 * @return 1 if cancelled, 0 otherwise
 *
 * Should be called periodically in long-running coroutines
 * to enable cooperative cancellation.
 */
int coco_cancelled(void);
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* COCO_H */
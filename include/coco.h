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

/* Symbol visibility macros */
#ifdef COCO_BUILD_SHARED
    #ifdef _WIN32
        #ifdef COCO_BUILDING
            #define COCO_API __declspec(dllexport)
        #else
            #define COCO_API __declspec(dllimport)
        #endif
    #else
        #ifdef COCO_BUILDING
            #define COCO_API __attribute__((visibility("default")))
        #else
            #define COCO_API
        #endif
    #endif
#else
    #define COCO_API
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct coco_context;

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
#define COCO_ERROR_WOULD_BLOCK -7    /**< Operation would block (non-blocking API) */
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
    COCO_STATE_OVERFLOW_RESUME, /**< Coroutine recovered from overflow, waiting to resume */
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
#define COCO_DEFAULT_STACK_SIZE   2048        /**< 2KB - default, matches Go 1.22+ */
#define COCO_STACK_CONSERVATIVE    (64 * 1024) /**< 64KB - 保守固定栈（不增长） */
#define COCO_STACK_FIXED          (64 * 1024) /**< 64KB - fixed stack (no growth) */


#define COCO_STACK_SMALL          (16 * 1024) /**< 16KB - I/O bound tasks */
#define COCO_STACK_MEDIUM         (32 * 1024) /**< 32KB - general purpose */
#define COCO_STACK_LARGE          (128 * 1024)/**< 128KB - recursive/large stack frames */
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
int COCO_API coco_sched_set_io_backend(coco_sched_t *sched, coco_io_backend_t backend);

/**
 * @brief Get the current I/O backend
 * @param sched Scheduler pointer
 * @return Current backend type
 */
coco_io_backend_t COCO_API coco_sched_get_io_backend(coco_sched_t *sched);

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
int COCO_API coco_sched_set_io_options(coco_sched_t *sched, const coco_io_options_t *options);

/**
 * @brief Get current I/O configuration
 * @param sched Scheduler pointer
 * @param options Output configuration
 * @return COCO_OK on success, COCO_ERROR on failure
 */
int COCO_API coco_sched_get_io_options(coco_sched_t *sched, coco_io_options_t *options);

/**
 * @brief Get io_uring statistics (Linux only)
 * @param sched Scheduler pointer
 * @param submit_count Output: number of submit calls
 * @param syscall_count Output: number of actual syscalls
 *
 * On non-Linux platforms, outputs are set to 0.
 */
void COCO_API coco_iouring_get_stats(coco_sched_t *sched, uint64_t *submit_count, uint64_t *syscall_count);

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
coco_batch_io_t *COCO_API coco_batch_begin(coco_sched_t *sched);

/**
 * @brief Add a read operation to batch
 * @param batch Batch context
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Maximum bytes to read
 * @return COCO_OK on success, negative error code on failure
 */
int COCO_API coco_batch_add_read(coco_batch_io_t *batch, int fd, void *buf, size_t count);

/**
 * @brief Add a write operation to batch
 * @param batch Batch context
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Bytes to write
 * @return COCO_OK on success, negative error code on failure
 */
int COCO_API coco_batch_add_write(coco_batch_io_t *batch, int fd, const void *buf, size_t count);

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
int COCO_API coco_batch_submit(coco_batch_io_t *batch, coco_batch_result_t *results, size_t max_results);

/**
 * @brief Cancel a batch I/O
 * @param batch Batch context
 * @return COCO_OK on success, negative error code on failure
 */
int COCO_API coco_batch_cancel(coco_batch_io_t *batch);

/**
 * @brief End a batch context without submitting
 * @param batch Batch context
 *
 * Discards all accumulated operations and frees the context.
 */
void COCO_API coco_batch_end(coco_batch_io_t *batch);

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
coco_sched_t *COCO_API coco_sched_create(void);

/**
 * @brief Destroy a scheduler and free all resources
 * @param sched Scheduler pointer
 *
 * This will terminate all running coroutines and free associated memory.
 * The stack pool is destroyed, returning all memory to the system.
 */
void COCO_API coco_sched_destroy(coco_sched_t *sched);

/**
 * @brief Run the scheduler until all coroutines finish
 * @param sched Scheduler pointer
 * @return COCO_OK on success, negative error code on failure
 *
 * This is a blocking call that runs the event loop until there are
 * no more ready or waiting coroutines.
 */
int COCO_API coco_sched_run(coco_sched_t *sched);

/**
 * @brief Perform a single scheduling iteration
 * @param sched Scheduler pointer
 * @return COCO_OK on success, negative error code on failure
 *
 * Processes one ready coroutine and handles any expired timers.
 * Useful for integrating with external event loops.
 */
int COCO_API coco_sched_run_once(coco_sched_t *sched);

/**
 * @brief Get the current scheduler for this thread
 * @return Scheduler pointer, or NULL if no scheduler is active
 *
 * Each thread can have its own scheduler. This function returns
 * the scheduler associated with the calling thread.
 */
coco_sched_t *COCO_API coco_sched_get_current(void);

/**
 * @brief Check if a stack map is loaded for the scheduler
 * @param sched Scheduler pointer
 * @return Number of functions in the stack map, or 0 if not loaded
 *
 * Used for testing and verification of stack map loading.
 */
uint32_t coco_sched_get_stack_map_count(coco_sched_t *sched);

/**
 * @brief Enable or disable time-slice fairness scheduling
 * @param sched Scheduler pointer
 * @param enabled true to enable fairness, false to disable
 * @param slice_ms Time slice in milliseconds (0 = use default 10ms)
 * @return COCO_OK on success, COCO_ERROR on failure
 *
 * When enabled, each coroutine is limited to running for at most
 * the specified time slice before being preempted and re-queued.
 * This prevents CPU-bound coroutines from starving others.
 *
 * Default: disabled, 10ms time slice
 */
int COCO_API coco_sched_set_fairness(coco_sched_t *sched, bool enabled, uint32_t slice_ms);
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
coco_coro_t *COCO_API coco_create(coco_sched_t *sched, void (*entry)(void*), void *arg, size_t stack_size);

/**
 * @brief Exit the current coroutine with a result
 * @param coro Coroutine pointer (typically from coco_self())
 * @param result Return value to be retrieved by coco_join()
 *
 * Must be called from within the coroutine. After calling, the coroutine
 * enters COCO_STATE_DEAD state.
 */
void COCO_API coco_exit(coco_coro_t *coro, void *result);

/**
 * @brief Yield execution to the scheduler
 *
 * Must be called from within a coroutine. The coroutine remains in
 * COCO_STATE_READY state and will be rescheduled later.
 */
int COCO_API coco_yield(void);

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
void COCO_API coco_destroy(coco_coro_t *coro);

/**
 * @brief Validate that a stack map is loaded for dynamic stack growth
 * @param sched Scheduler pointer
 * @return COCO_OK if stack map is loaded, COCO_ERROR otherwise
 *
 * For coroutines with dynamic stack enabled (stack_size < 64KB),
 * a stack map must be loaded for pointer adjustment during growth.
 * Call this before creating dynamic stack coroutines to ensure
 * the stack map is available.
 */
int COCO_API coco_validate_stack_map(coco_sched_t *sched);
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
coco_coro_t *COCO_API coco_self(void);

/**
 * @brief Get the state of a coroutine
 * @param coro Coroutine pointer
 * @return Current state (COCO_STATE_DEAD if coro is NULL)
 */
coco_state_t COCO_API coco_get_state(coco_coro_t *coro);

/**
 * @brief Get the unique ID of a coroutine
 * @param coro Coroutine pointer
 * @return Unique 64-bit ID, or 0 if coro is NULL
 */
uint64_t COCO_API coco_get_id(coco_coro_t *coro);

/**
 * @brief Set the error callback for a coroutine
 * @param coro Coroutine pointer
 * @param cb Error callback function
 *
 * The callback is invoked when the coroutine encounters an error
 * such as stack overflow.
 */
void COCO_API coco_set_error_cb(coco_coro_t *coro, coco_error_cb cb);

/**
 * @brief Set the priority of a coroutine
 * @param coro Coroutine pointer
 * @param priority Priority level (see @ref CoroutinePriorities)
 *
 * Higher priority coroutines are scheduled before lower priority ones.
 */
void COCO_API coco_set_priority(coco_coro_t *coro, coco_priority_t priority);

/**
 * @brief Get the priority of a coroutine
 * @param coro Coroutine pointer
 * @return Priority level (COCO_PRIORITY_NORMAL if coro is NULL)
 */
coco_priority_t COCO_API coco_get_priority(coco_coro_t *coro);

/**
 * @brief Get the stack usage of a coroutine
 * @param coro Coroutine pointer
 * @return Used bytes, or 0 on error
 *
 * Note: This samples at yield/exit points and may underestimate
 * the peak usage during deep recursion.
 */
size_t COCO_API coco_get_stack_usage(coco_coro_t *coro);
/** @} */

/* === Coroutine-Local Storage API === */
/** @defgroup CLS Coroutine-Local Storage API
 *  @brief Per-coroutine key-value storage
 *  @{
 */

/**
 * @brief Set a coroutine-local value
 * @param key Key string (must be persistent/constant)
 * @param value Value pointer
 * @param destructor Optional destructor called on cleanup (NULL for none)
 * @return COCO_OK on success, negative error code on failure
 */
int COCO_API coco_cls_set(const char *key, void *value, void (*destructor)(void *));

/**
 * @brief Get a coroutine-local value
 * @param key Key string
 * @return Value pointer, or NULL if not found
 */
void *COCO_API coco_cls_get(const char *key);

/**
 * @brief Delete a coroutine-local value
 * @param key Key string
 * @return COCO_OK on success, negative error code on failure
 */
int COCO_API coco_cls_delete(const char *key);

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
coco_channel_t *COCO_API coco_channel_create(size_t capacity);

/**
 * @brief Send a value through the channel (blocking)
 * @param ch Channel pointer
 * @param value Value to send
 * @return COCO_OK on success, COCO_ERROR_CHANNEL_CLOSED if closed
 *
 * For buffered channels, returns immediately if buffer has space.
 * For unbuffered channels, blocks until a receiver is ready.
 */
int COCO_API coco_channel_send(coco_channel_t *ch, void *value);

/**
 * @brief Receive a value from the channel (blocking)
 * @param ch Channel pointer
 * @param value Pointer to receive the value
 * @return COCO_OK on success, COCO_ERROR_CHANNEL_CLOSED if closed
 *
 * Blocks until a value is available or the channel is closed.
 */
int COCO_API coco_channel_recv(coco_channel_t *ch, void **value);

/**
 * @brief Close a channel
 * @param ch Channel pointer
 *
 * After closing, senders will receive COCO_ERROR_CHANNEL_CLOSED.
 * Receivers can still drain remaining buffered values.
 */
void COCO_API coco_channel_close(coco_channel_t *ch);

/**
 * @brief Destroy a channel
 * @param ch Channel pointer
 *
 * The channel must be closed first. Frees all resources.
 */
void COCO_API coco_channel_destroy(coco_channel_t *ch);
/** @} */

/* === Channel Select API === */
/** @defgroup ChannelSelect Channel Select API
 *  @brief Go-style select over multiple channel operations
 *  @{
 */

/** Select case direction */
enum coco_select_dir {
    COCO_SELECT_SEND,    /**< Send case */
    COCO_SELECT_RECV     /**< Receive case */
};

/** Select case descriptor */
typedef struct coco_select_case {
    coco_channel_t *chan;        /**< Channel */
    enum coco_select_dir dir;    /**< Direction: send or recv */
    void *val;                   /**< Send value or recv output pointer */
    int result;                  /**< Result: COCO_OK or error */
} coco_select_case_t;

/** Select return values */
#define COCO_SELECT_TIMEOUT  (-2)  /**< Timeout expired */
#define COCO_SELECT_DEFAULT  (-3)  /**< Default case taken */

/**
 * @brief Select over multiple channel operations (Go-style)
 * @param cases Array of select cases
 * @param ncases Number of cases
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @param has_default Whether to include a default case
 * @return Index of ready case, COCO_SELECT_TIMEOUT, or COCO_SELECT_DEFAULT
 *
 * Blocks until one case is ready, timeout expires, or default is taken.
 * When multiple cases are ready, one is chosen (first-ready scan).
 */
int coco_channel_select(coco_select_case_t *cases, int ncases,
                        uint64_t timeout_ms, int has_default);

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
int COCO_API coco_read(int fd, void *buf, size_t count);

/**
 * @brief Write to a file descriptor (blocking for coroutines)
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Maximum bytes to write
 * @return Bytes written, or negative error code
 *
 * Yields to the scheduler while waiting for write readiness.
 */
int COCO_API coco_write(int fd, const void *buf, size_t count);

/**
 * @brief Accept a connection (blocking for coroutines)
 * @param fd Listening socket
 * @param addr Client address buffer (struct sockaddr*)
 * @param addrlen Address buffer length (input/output)
 * @return New socket fd, or negative error code
 *
 * Yields to the scheduler while waiting for a connection.
 */
int COCO_API coco_accept(int fd, void *addr, size_t *addrlen);

/**
 * @brief Connect to a remote host (blocking for coroutines)
 * @param fd Socket
 * @param addr Remote address (struct sockaddr*)
 * @param addrlen Address length
 * @return COCO_OK on success, negative error code on failure
 *
 * Yields to the scheduler while connecting.
 */
int COCO_API coco_connect(int fd, const void *addr, size_t addrlen);

/**
 * @brief Sleep for a duration
 * @param ms Milliseconds to sleep
 * @return COCO_OK
 *
 * Yields to the scheduler. Other coroutines can run during the sleep.
 */
int COCO_API coco_sleep(uint64_t ms);
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
coco_timer_t *COCO_API coco_timer(uint64_t delay_ms, void (*callback)(void*), void *arg);

/**
 * @brief Cancel a timer (O(1) operation)
 * @param timer Timer pointer
 *
 * Removes the timer from the timing wheel and frees it.
 * Safe to call even if the timer has already fired.
 */
void COCO_API coco_timer_cancel(coco_timer_t *timer);

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
coco_timer_t *COCO_API coco_timer_ex(coco_sched_t *sched, uint64_t delay_ms, void (*callback)(void*), void *arg);
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
int COCO_API coco_cancel(coco_coro_t *coro);

/**
 * @brief Check if the current coroutine is cancelled
 * @return 1 if cancelled, 0 otherwise
 *
 * Should be called periodically in long-running coroutines
 * to enable cooperative cancellation.
 */
int COCO_API coco_cancelled(void);
/** @} */

/* === Preemption API === */
/** @defgroup Preemption Preemption API
 *  @brief Asynchronous preemption for fair scheduling
 *  @{
 */

/**
 * @brief Enable preemption for the current coroutine
 *
 * After calling this, the coroutine may be preempted after running
 * for more than 10ms without yielding.
 */
void COCO_API coco_preempt_enable(void);

/**
 * @brief Disable preemption for the current coroutine
 *
 * Use this for critical sections that must not be interrupted,
 * such as stack growth operations.
 */
void COCO_API coco_preempt_disable(void);

/**
 * @brief Check if preemption is pending
 * @return 1 if preemption is pending, 0 otherwise
 */
int COCO_API coco_preempt_is_pending(void);

/**
 * @brief Cooperative preemption checkpoint
 *
 * Call this periodically in long-running loops to allow
 * the scheduler to preempt the coroutine.
 */
void COCO_API coco_preempt_checkpoint(void);
/** @} */

/* === Multi-threaded Scheduler API === */
/** @defgroup MultiThreadedScheduler Multi-threaded Scheduler API
 *  @brief Functions for multi-threaded coroutine scheduling
 *  @{
 */

/**
 * @brief Start the global scheduler with worker threads
 * @param num_workers Number of worker threads (0 = auto-detect CPU count)
 * @return COCO_OK on success, negative error code on failure
 *
 * Creates worker threads and binds them to processors.
 * After calling this, coco_go() will dispatch coroutines to workers.
 */
int COCO_API coco_global_sched_start(uint32_t num_workers);

/**
 * @brief Wait for all coroutines to complete
 * @return COCO_OK on success, negative error code on failure
 *
 * Blocks until all active coroutines finish execution.
 */
int COCO_API coco_global_sched_wait(void);

/**
 * @brief Stop the global scheduler and join worker threads
 * @return COCO_OK on success, negative error code on failure
 *
 * Gracefully shuts down all worker threads.
 */
int COCO_API coco_global_sched_stop(void);

/**
 * @brief Coroutine launch options for coco_go
 */
typedef struct coco_go_opts {
    size_t stack_size;          /**< Stack size (0 = default) */
    struct coco_context *context; /**< Associated context (optional) */
    int priority;               /**< Priority (-1 = default) */
    int p_id;                   /**<指定 P (-1 = auto select) */
} coco_go_opts_t;

/**
 * @brief Launch a coroutine (auto-select best P)
 * @param entry Entry function
 * @param arg Entry argument
 * @return Coroutine handle, or NULL on failure
 *
 * In multi-threaded scheduler, auto-selects the least loaded P.
 * In single-threaded scheduler, uses current scheduler.
 */
coco_coro_t *COCO_API coco_go(void (*entry)(void*), void *arg);

/**
 * @brief Launch a coroutine on specified P
 * @param p_id P's ID
 * @param entry Entry function
 * @param arg Entry argument
 * @return Coroutine handle, or NULL on failure
 *
 * Explicitly specify P for data locality scenarios.
 */
coco_coro_t *COCO_API coco_go_on(int p_id, void (*entry)(void*), void *arg);

/**
 * @brief Launch a coroutine with options
 * @param entry Entry function
 * @param arg Entry argument
 * @param opts Options (can be NULL)
 * @return Coroutine handle, or NULL on failure
 */
coco_coro_t *coco_go_with_opts(void (*entry)(void*), void *arg,
                                const coco_go_opts_t *opts);
/** @} */

/* === Version === */
/** @defgroup Version Version API
 *  @brief Runtime version query functions
 *  @{
 */

/**
 * @brief Get the full version string
 * @return Version string in "X.Y.Z" format
 */
const char *COCO_API coco_version(void);

/**
 * @brief Get the major version number
 * @return Major version (e.g., 2 for "2.1.0")
 */
int COCO_API coco_version_major(void);

/**
 * @brief Get the minor version number
 * @return Minor version (e.g., 1 for "2.1.0")
 */
int COCO_API coco_version_minor(void);

/**
 * @brief Get the patch version number
 * @return Patch version (e.g., 0 for "2.1.0")
 */
int COCO_API coco_version_patch(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* COCO_H */
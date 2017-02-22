## Advanced I/O Function
### readv and writev Functions

```
#include <sys/uio.h>

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);

ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
```

The `readv()` system call reads `iovcnt` buffers from the file associated with the file descriptor `fd` into the buffers described by `iov` ("scatter input").

The `writev()` system call writes `iovcnt` buffers of data described by `iov` to the file associated with the file descriptor `fd` ("gather output").

The  pointer  `iov`  points  to  an array of `iovec` structures, defined in `<sys/uio.h>` as:
```
struct iovec {
    void  *iov_base;    /* Starting address */
    size_t iov_len;     /* Number of bytes to transfer */
};
```
The `readv()` system call works just like `read()`  except  that  multiple buffers are filled.

The  `writev()` system call works just like `write()` except that multiple buffers are written out.

Buffers are processed in array order.  This  means that `readv()` completely fills `iov[0]` before proceeding to `iov[1]`, and so on. Similarly, `writev()` writes out the entire contents of `iov[0]` before proceeding to `iov[1]`, and so on.

## libev
### WATCHER TYPES
#### ev_timer - relative and optionally repeating timeouts
##### Be smart about timeouts
1. Use a timer and stop, reinitialise and start it on activity. This is the most obvious, but not the most simple way: In the beginning, start the watcher:
```
ev_timer_init (timer, callback, 60., 0.);
ev_timer_start (loop, timer);
```
Then, each time there is some activity, "ev_timer_stop" it, initialise it and start it again:
```
ev_timer_stop (loop, timer);
ev_timer_set (timer, 60., 0.);
ev_timer_start (loop, timer);
```
This is relatively simple to implement, but means that each time there is some activity, libev will first have to remove the timer from its internal data structure and then add it again.

Libev tries to be fast, but it's still not a constant-time operation.

2. Use a timer and re-start it with "ev_timer_again" inactivity.
This is the easiest way, and involves using "ev_timer_again" instead of "ev_timer_start".

To implement this, configure an "ev_timer" with a "repeat" value of 60 and then call "ev_timer_again" at start and each time you successfully read or write some data. 

At start:
```
ev_init (timer, callback);
timer->repeat = 60.;
ev_timer_again (loop, timer);
```
Each time there is some activity:
```
ev_timer_again (loop, timer);
```
It is even possible to change the time-out on the fly, regardless of whether the watcher is active or not:
```
timer->repeat = 30.;
ev_timer_again (loop, timer);
```
This is slightly more efficient then stopping/starting the timer each time you want to modify its timeout value, as libev does not have to completely remove and re-insert the timer from/into its internal data structure.

### ANATOMY OF A WATCHER

In the following description, uppercase "TYPE" in names stands for the watcher type, e.g. "ev_TYPE_start" can mean "ev_timer_start" for timer watchers and "ev_io_start" for I/O watchers.

A watcher is an opaque structure that you allocate and register to record your interest in some event. To make a concrete example, imagine you want to wait for STDIN to become readable, you would create an "ev_io" watcher for that:
```
static void my_cb (struct ev_loop *loop, ev_io *w, int revents)
{
  ev_io_stop (w);
  ev_break (loop, EVBREAK_ALL);
}

struct ev_loop *loop = ev_default_loop (0);

ev_io stdin_watcher;

ev_init (&stdin_watcher, my_cb);
ev_io_set (&stdin_watcher, STDIN_FILENO, EV_READ);
ev_io_start (loop, &stdin_watcher);

ev_run (loop, 0);
```
Each watcher structure must be initialised by a call to "ev_init (watcher *, callback)", which expects a callback to be provided. This callback is invoked each time the event occurs (or, in the case of I/O watchers, each time the event loop detects that the file descriptor given is readable and/or writable).

To make the watcher actually watch out for events, you have to start it with a watcher-specific start function ("ev_TYPE_start (loop, watcher *)"), and you can stop watching for events at any time by calling the corresponding stop function ("ev_TYPE_stop (loop, watcher *)".

As long as your watcher is active (has been started but not stopped) you must not touch the values stored in it.

Each and every callback receives the event loop pointer as first, the registered watcher structure as second, and a bitset of received events as third argument.

## LD_DEBUG
Output verbose debugging information about the dynamic linker. If set to all prints all debugging information it has.


## make
```
-n, --just-print, --dry-run, --recon
     Print the commands that would be executed, but do not execute them.
```
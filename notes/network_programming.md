## Advanced I/O Functions
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

## I/O Multiplexing: The select and poll Functions

### select Function
This function allows the process to instruct the kernel to wait for any one of multiple events to occur and to wake up the process only when one or more of these events occurs or when a specified amount of time has passed.

As an example, we can call `select` and tell the kernel to return only when:
- Any of the descriptors in the set {1, 4, 5} are ready for reading
- Any of the descriptors in the set {2, 7} are ready for writing
- Any of the descriptors in the set {1, 4} have an exception condition pending
- 10.2 seconds have elapsed

That is, we tell the kernel what descriptors we are interested in (for reading, writing, or an exception condition) and how long to wait.

```
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

int select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout);
```
We start our description of this function with its final argument, which tells the kernel how long to wait for one of the specified descriptors to become ready. A `timeval` structure specifies the number of seconds and microseconds.

There are three possibilities:

1. Wait forever - Return only when one of the specified descriptors is ready for I/O. For this, we specify the `timeout` argument as a null pointer.
2. Wait up to a fixed amount of time - Return when one of the specified descriptors for I/O, but do not wait beyond the number of seconds and microseconds specified in the `timeval` structure pointed by the `timeout` argument.
3. Do not wait at all - Return immediately after checking the descriptors. This is called *polling*. To specify this, the `timeout` argument must point to a `timeval` structure and the timer value must be 0.

The three middle arguments, readset, writeset, and exceptset, specify that we want the kernel to test for reading, writing, and exception conditions.

`select` uses *descriptor sets*, typically an array of integers, with each bit in each integer corresponding to a descriptor.

```
void FD_CLR(int fd, fd_set *fdset); /* is the bit for fd on in fdset? */
int  FD_ISSET(int fd, fd_set *fdset); /* turn off the bit for fd in fdset */
void FD_SET(int fd, fd_set *fdset); /* turn on the bit for fd in fdset */
void FD_ZERO(fd_set *fdset); /* clear all bits in fdset */
```
For example, to define a variable of type `fd_set` and then turn on the bits for descriptors 1, 4, and 5, we write
```
fd_set rset;

FD_ZERO(&rset); /* initialize the set: all bits off */
FD_SET(1, &set); /* turn on bit for fd 1 */
FD_SET(4, &set); /* turn on bit for fd 4 */
FD_SET(5, &set); /* turn on bit for fd 5 */
```
Any of the middle three arguments to `select`, readset, writeset, or exceptset, can be specified as a null pointer if we are not interested in that condition.

The maxfdp1 argument specifies the number of descriptors to be tested.

The maxfdp1 argument forces us to calculate the largest descriptor that we are interested in and then tell the kernel this value. For example, given the previous code that turns on the indicators for descriptors 1, 4, and 5, the maxfdp1 value is 6. The reason it is 6 and not 5 is that we are specifying the number of descriptor, not the largest value, and descriptors start at 0.

1. A socket is ready for reading if any of the following four conditions is true:
   1. The number of bytes of data in the socket receive buffer is greater than or equal to the current size of the low-water mark of the socket receive buffer. A read operation on the socket will not block and will return a value greater than 0 (i.e., the data that is ready to be read). We can set this low-water mark using the SO_RCVLOWAT socket option. It defaults to 1 for TCP and UDP sockets.
   1. The socket is a listening socket and the number of completed connections is nonzero.
1. A socket is ready for writing if any of the following conditions is true:
   1. The number of bytes available space in the socket send buffer is greater than or equal to the current size of the low-water mark for the socket send buffer and either: (i) the socket is connected, or (ii) the socket does not require a connection (e.g., UDP).
   
   1. A socket using a non-blocking `connect` has completed the connection, or the `connect` has failed.

### poll Function

```
#include <poll.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
```
`poll()` performs a similar task to select(2): it waits for one of a set of file descriptors to become ready to perform I/O.

The set of file descriptors to be monitored is specified in the fds argument, which is an array of structures of the following form:
```
struct pollfd {
	int   fd;         /* file descriptor */
	short events;     /* requested events */
	short revents;    /* returned events */
};
```
The caller should specify the number of items in the `fds` array in `nfds`.

The field `events` is an input parameter, a bit mask specifying the events the application is interested in for the file descriptor `fd`.

The field `revents` is an output parameter, filled by the kernel with the events that actually occurred.

If none of the events requested (and no error) has occurred for any of the file descriptors, then `poll()` blocks until one of the events occurs.

The `timeout` argument specifies the number of milliseconds that `poll()` should block waiting for a file descriptor to become ready. Specifying a negative value in `timeout` means an infinite timeout. Specifying a timeout of zero causes `poll()` to return immediately, even if no file descriptors are ready.

On success, a positive number is returned; this is the number of structures which have nonzero revents fields (in other words, those descriptors with events or errors reported).

### epoll Function
```
#define MAX_EVENTS 10
struct epoll_event ev, events[MAX_EVENTS];
int listen_sock, conn_sock, nfds, epollfd;

/* Code to set up listening socket, 'listen_sock', (socket(), bind(), listen()) omitted */

epollfd = epoll_create1(0);
if (epollfd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
}

ev.events = EPOLLIN;
ev.data.fd = listen_sock;
if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
    perror("epoll_ctl: listen_sock");
    exit(EXIT_FAILURE);
}

for (;;) {
    nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
        perror("epoll_wait");
        exit(EXIT_FAILURE);
    }

    for (n = 0; n < nfds; ++n) {
        if (events[n].data.fd == listen_sock) {
            conn_sock = accept(listen_sock, (struct sockaddr *) &addr, &addrlen);
            if (conn_sock == -1) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            setnonblocking(conn_sock);
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = conn_sock;
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
                perror("epoll_ctl: conn_sock");
                exit(EXIT_FAILURE);
            }
        } else {
            do_use_fd(events[n].data.fd);
        }
    }
}
```
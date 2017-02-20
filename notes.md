`readv` and `writev` functions

These two functions are similar to `read` and `write`, but `readv` and `writev` let us read into or write from one or more buffers with a single function call. These operations are called *scatter read* (since the input data is scattered into multiple application buffers) and *gather write* (since multiple buffers are gathered for a single output operation).
```
#include <sys/uio.h>
ssize_t readv(int filedes, const struct iovec *iov, int iovcnt);
ssize_t writev(int filedes, const struct iovec *iov, int iovcnt);

Both return: number of bytes read or written, -1 on error
```
The second argument to both functions is a pointer to an array of `iovec` structures, which is defined by including the `<sys/uio.h>` header.
```
struct iovec {
	void *iov_base; /* starting address of buffer */
	size_t iov_len; /* size of buffer */
}
```

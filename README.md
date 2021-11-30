# sendfile

These two programs use port 48763 to transfer files.

send usage:

```console
~$ ./send [filepath]
```

receive usage:

```console
~$ ./receive [ip address]
```

## build

Make sure `cmake` is installed.

Executables will be saved in `[repo]/bin`.

```bash
mkdir build && cd build
cmake ..
cmake --build . --parallel 2
```

Matroska Server Mk2
===================

This project is the result of years of thinking, trying and finally succeeding.

This software makes it possible to stream (almost) anything remuxed as matroska live in real-time over http to multiple clients.

This is probably also one of the first real world usages of FFmpeg's http server component (which I wrote as part of GSoC 2015).

The second sentence basically said everything already, so let's dive into some usage examples:

First of all, clone the repo and try to build the project:

```
user@example~$ make
```


If successful you will have built the server binary. Let's get some files to stream!

```
user@example~$ ./server somevideo.mkv
```


This will serve the file somevideo.mkv on all interfaces on port 8080 (so far only configurable in server2.c) in real-time.

You can also use stdin from a live feed for example:

```
user@example~$ wget http://example.org/livestream -O - | ./server
```


Or just read it directly as an http served file:

```
user@example~$ ./server http://example.org/livestream
```


Or you can have an easy screencasting setup:


On a server:
```
user@remote~$ nc -l -p 12345 | ./server
```

On a client:

```
user@example~$ ffmpeg -f x11grab -framerate 15 -s 1920x1080 -i :0.0 -c libx264 tcp://remote:12345
```

You will see the server writing some information, as it is still quite verbose (it was even more verbose in earlier versions for obvious reasons ;) ).


Architecture
------------

With the latest iteration more sophisticated data structures have been used.
The following structs make up the architecture:

Segment

Contains a segment of data. A segment is always exactly one GOP, that implies segments always start at a keyframe.
Segments are refcounted.

BufferContext

Circular buffer that manages segments and takes care of refcounting them.


PublisherContext

Holds a BufferContext for new segments and a BufferContext for old segments that should still be sent to new clients. This makes streams on clients start faster and stutter less.

Also holds a list of Clients, which in turn have a BufferContext of segments that still have to be sent to the client.



Dependencies
------------

- very recent ffmpeg libraries (some bugs in ffmpeg were found while writing this project)
In fact so recent that you actually have to patch the current ffmpeg git-master and build from source. (As of 14th April 2017)

git-format patches are in the "patches/" directory.


Todo
----

- Argument parsing
- Configuration
- Documentation
- Management interface

Known issues
------------

 - still leaks very small amounts of memory (valgrind reports a few thousand bytes)

Thanks
------

- Clément Bœsch \<ubitux\> for kind of "triggering" this project with a bugreport
- Nicolas George for mentoring me during GSoC. I would never be familiar enough with the codebase without him and the project.

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


This will serve the file somevideo.mkv on localhost on port 8080 (so far only configurable in main.c) in real-time.

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

(Remember to change the listening address in main.c from 127.0.0.1 otherwise nobody but local users will be able to watch)

You will see the server writing some information, as it is still quite verbose (it was even more verbose in earlier versions for obvious reasons ;) ).

This far nothing is implemented to handle the end of a file or a stream, so the server will loop and accept clients but never write anything to them.


Dependencies
------------

- FFmpeg at least commit FFmpeg/FFmpeg@d0be0cbebc207f3a39da2a668f45be413c9152c4 (contains fixes for ffmpeg's http server)


Todo
----

- Argument parsing
- Configuration
- Documentation
- Handle EOF
- Management interface

Known issues
------------

 - still leaks some memory, I haven't found out where exactly and how to fix it

Thanks
------

- Clément Bœsch \<ubitux\> for kind of "triggering" this project with a bugreport
- Nicolas George for mentoring me during GSoC. I would never be familiar enough with the codebase without him and the project.

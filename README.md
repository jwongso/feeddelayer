# FeedDelayer
Another yet c++ coding test project

Using the websockets API - available at https://api.deriv.com, build a small application 
which calls https://api.deriv.com/api-explorer/#ticks to obtain a real-time tick stream of a 
given instrument, and then delays that tick stream by x minute.

Implemented based on Boost Websocket asnyc client example.

Consider design choices that might help with efficient behaviour.

Pre-requirements: CMake, Boost and OpenSSL dev libraries

Usage:

``` feeddelayer --timeout <delay in secs> --collect_stream true```

feeddelayer Options:

```   -h [ --help ]                    This help screen```

```   -t [ --timeout ] arg (=60)       Timeout/Delay in secs (default = 60, 0 = no delay)``` 

```   -c [ --collect_stream ] arg (=1) Collect stream within delay (true = yes/default, false = no)```


Q&A:
- Why boost? Why not? It provides cool features such as asio, beast-websocket and any other stuff that might fit the trading or no-latency technology requirements
- Is it thread-safe? I hope so since I should work less than 4 hours to implement a robust and production quality code...
- What is boost::lockfree::spsc_queue? Just google it. Basically, the main reasons are thread safe and lock-free. So you don't have to worry about managing mutexes and in the same time, you can modify the queue elements from different threads. In particular, Spsc means single thread producer and single thread consumer the queue, which is fitting nicely in this use case.
- Any other important notes? The websocket part should work asynchronously. The other thread will be suspended based on the given timeout/delay (default 60 secs) and print out either all collected stream or the last one based on --collect_stream option.

## LibTTAK: Timestamp-tracking memory lifetime, Thread and Async Toolkit

LibTTAK provides safer C development by following memory lifetime.
All dynamically allocated variables using libttak have its own lifetime.
If the variable's lifefime is expired, you can call memory cleaner function to safely clean garbages without GC.

- Collect your garbages when you **want to clean**
- No stop-and-go GC.
- Every memory blocks have lifetime.

## How cool is it?

You can see `apps` example.
This can run complex math programs without bad memory management so easily.

## C is good, if you stay safe.

LibTTAK aims for safety over performance.
Contrary to common belief, it will not provide superior performance; its performance is similar to C++.
However, this still follow C-style programming, and you don't need to seek massive C++ documents.
No iterators, every syntax is clear.

If you use proper technique to prevent dangerous patterns, C is good, and sometimes it is even better than C++.
LibTTAK will help you to develop `adequate` C programs.

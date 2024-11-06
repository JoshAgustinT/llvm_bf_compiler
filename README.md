# Bf llvm based compiler
by Joshua Tlatelpa-Agustin 
11-1-24
######  Github link of project:
https://github.com/JoshAgustinT/llvm_bf_compiler/tree/main

---
State of project:
    Currently passing all fuzzer tests and all tests from benches folder. Most tests run a bit faster than my latest native compiler. They are also of course much faster than my interpreter and first naive asm compiler. This is amazing considering how much optimization we did to the native one. I was especially impressed that it made mandel almost 2x faster. It was quite a steep learning curve at the start but once I got it running, everything just worked correctly and passed all the fuzzer tests on the first try (which meant no low level debugging c: ). 

Llvm with -02 opt benchmarks:

    bench.b
    0.00user 0.00system 0:00.00elapsed 100%CPU (0avgtext+0avgdata 2300maxresident)k

    bottles.b
    0.00user 0.00system 0:00.00elapsed 0%CPU (0avgtext+0avgdata 2312maxresident)k

    deadcodetest.b
    0.00user 0.00system 0:00.00elapsed 69%CPU (0avgtext+0avgdata 1220maxresident)k

    hanoi.b
    1.34user 0.00system 0:01.35elapsed 99%CPU (0avgtext+0avgdata 2536maxresident)k

    hello.b
    0.00user 0.00system 0:00.00elapsed 75%CPU (0avgtext+0avgdata 2284maxresident)k

    long.b
    0.32user 0.00system 0:00.32elapsed 100%CPU (0avgtext+0avgdata 2436maxresident)k

    loopremove.b
    0.00user 0.00system 0:00.00elapsed 89%CPU (0avgtext+0avgdata 2284maxresident)k

    mandel.b
    0.47user 0.00system 0:00.47elapsed 100%CPU (0avgtext+0avgdata 2340maxresident)k

    serptri.b
    0.00user 0.00system 0:00.00elapsed 94%CPU (0avgtext+0avgdata 2440maxresident)k

    twinkle.b
    0.00user 0.00system 0:00.00elapsed 84%CPU (0avgtext+0avgdata 2316maxresident)k

---

Native compiler benchmarks:

    bench.b
    0.00user 0.00system 0:00.00elapsed 90%CPU (0avgtext+0avgdata 1432maxresident)k

    bottles.b
    0.00user 0.00system 0:00.00elapsed 89%CPU (0avgtext+0avgdata 1424maxresident)k

    deadcodetest.b
    0.00user 0.00system 0:00.00elapsed 86%CPU (0avgtext+0avgdata 1232maxresident)k

    hanoi.b
    0.01user 0.00system 0:00.01elapsed 100%CPU (0avgtext+0avgdata 1532maxresident)k

    hello.b
    0.00user 0.00system 0:00.00elapsed 91%CPU (0avgtext+0avgdata 1324maxresident)k

    long.b
    0.80user 0.00system 0:00.80elapsed 100%CPU (0avgtext+0avgdata 1432maxresident)k

    loopremove.b
    0.00user 0.00system 0:00.00elapsed 89%CPU (0avgtext+0avgdata 1440maxresident)k

    mandel.b
    0.75user 0.00system 0:00.75elapsed 100%CPU (0avgtext+0avgdata 1452maxresident)k

    serptri.b
    0.00user 0.00system 0:00.00elapsed 89%CPU (0avgtext+0avgdata 1340maxresident)k

    twinkle.b
    0.00user 0.00system 0:00.00elapsed 87%CPU (0avgtext+0avgdata 1352maxresident)k
# xeq

xeq is an external library for [Pure Data](http://pure-data.info), intended for manipulation of sequences of time-stamped Pd messages. It includes reading, processing and writing qlist and midi files, and the rudiments of score following.

It is created by Krzysztof Czaja  
Academy of Music, Warsaw

See Krzysztof's paper "Time and Structure in Xeq" in the doc folder for details. In this document the basic architecture is described, but the version 0.1 differs from this. Originally all functionality was incorporated into the single 'xeq' object, but version 0.1 started by extracting functions to other objects. At the code level, most of the high-level functionality is still part of xeq.c. The low-level functionaity is isolated in the shared framework.

The code compiles, and works for a large part, but some functions are 'experimental' and somewhat scetchy. The challenge is to identify those and make them work. One already fixed is the midi file interface. Another is xeq_polyparse, which can crash with certain argument values.




*Currently being adapted to build with the pd-lib-builder makefile*


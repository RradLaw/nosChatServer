nos2014assignment
=================
Introduction
-------------
This program passes all tests as required, as seen in the file "output.txt".

Compiling notes
---------------
Makefile has all needed toools to compile with "make".

Running notes
-------------
Program generally makes the 3 second test.

Sometimes the PRIVMSG before registration appears after the test program is searching for it. Running program other times shows this a random error.

Line 28 of sample.c and test.c might need to be uncommented. It was commented out for my home PC as filio.h was included in ioctl.h.

Programming notes
-----------------
To test server, run sample with port number (e.g. ./sample 12345).

To run client on it, run telnet on localhost with the port number (e.g. telnet localhost 12345).

To view cpu usage: top

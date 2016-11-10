### Meiko CS/2 Computing Surface Linux port

This repo contains assorted artifacts from a port of Linux
to the [Meiko](http://www.meiko.com/) CS/2 Computing Surface
circa 1999-2000.  The main bits here are some manuals,
low level arch support for the MK401 and MK403 compute boards,
and drivers and utilities for the bargraph and
[CAN](https://en.wikipedia.org/wiki/CAN_bus) bus.

The elan1 comms processor is conspicuously unsupported by
this Linux port, apart from some trivial code for mapping its
nanosecond clock.  Linux on supercomputers really took off at
LLNL at this point and our efforts focused on new hardware - the
[Quadrics](https://en.wikipedia.org/wiki/Quadrics) elan3/4,
which was the primary interconnect for our first production
Linux clusters.

This is my personal testing and benchmarking setup for my library `msf_gif`, which you can find here: https://github.com/notnullnotvoid/msf_gif

I've included the test setup in a separate repository because the samples data I use for it is fairly large (a few hundred megabytes).

To build the code, you need to clone this repository and the `msf_gif` repository both into the same folder so they are next to each other, and then run `build.bat` or `build.sh`.

Running the test program will generate a detailed profiling trace file `trace.json`, which you can view in Google Chrome by opening it in `chrome://tracing/`.

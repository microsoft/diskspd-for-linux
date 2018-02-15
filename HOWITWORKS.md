#diskspd overview

diskspd.cc

- The main function simply creates a Profile and passes it arguments
- It then runs the Profile and calls the result output function

profile.h

- Profile checks for specific arguments to determine how to generate Jobs (only args supported)
- If the XML/JSON input flags are detected, the relevant parser is used instead of optionparser.h
    - See docs - not all options implemented yet
- The parser interface produces a list of Jobs, each of which have Targets (files) for I/O
- System information (#cpus etc) is gathered at the Profile level
- The profile runs each Job in sequence
- The results from each Job are gathered and printed to the output (stdout, or a file depending on
  options)
    - See docs - not all options implemented yet

options.h

- Option parsing interface provided by Options class. Generates a map of options from the program
  arguments, and provides basic utilities for validating and converting them into numeric types
- There is a (somewhat) generic definition of all the command line options
    - In future this could possibly be extended to be used by JSON/XML parsers
- Uses argp.h to parse the arguments

job.h

- JobOptions represents options common to all Targets and threads
- All test results are collated in JobResults
- The Job class represents a period of time in which tests are run
    - Jobs are separate from the Profile because in future we may want to support multiple Jobs from
      a single run of diskspd
- The main function is run\_job(), which:
    - Creates and sets up each Target
    - Sets up thread control blocks (ThreadParams)
    - Initializes the IAsyncIOManager
    - Starts threads and affinitizes them if need be
    - Blocks on thread initialization, then warmup, and the main duration.
- Threads can interrupt the Job at any time to cancel it if there's an error
- Shared boolean variables, mutexes, and cvs are used to synchronize between the Job and threads
- CPU usage information is also collected in the Job

target.h

- Target represents a file to do I/O on. It contains all the options relevant for a specific file.
  - When XML/JSON option parsing is supported, we may want to specify per-Target options. Hence,
    all target-specific options from the program arguments are copied into each target
- TargetData is a single thread's view of a Target, containing buffers for reading/writing to that
  Target, and per-thread TargetResults
- TargetBuffer is a wrapper for buffer allocation - it allows automatic alignment of a buffer for use
  with the O\_DIRECT flag, which requires physical block alignment

thread.h

- ThreadParams represents the control block for each thread.
- Threads initialize themselves and start doing I/O immediately - they block the main thread (the Job)
  until they have submitted the first batch of I/O requests. (This ensures that I/O happens for the
  entire warmup duration).
- Threads use the generic IAsyncIOManager to do I/O - the only work they do is calculating offsets,
  recording results and error checking
- IOPs, latency etc are recorded in the ThreadResults structure if the warmup is over.
- Threads exit as soon as the Job sets a shared variable (run\_threads) to false. Any I/O that
  completed after the duration expired is not recorded.

result\_formatter.h

- Generic interface to a formatter (to support XML/JSON output in future)
- Simply iterates through each Job, printing the results using some helper functions

async\_io.h

- Generic I/O interface for threads to use.
- The concept of a group\_id is used so the io engine can distinguish between threads; each thread
  must register a unique group\_id (we just just the thread\_id)
    - It is not safe to use a single group\_id across multiple threads (but it isn't needed)
- Threads use the IAsyncIOManager as a factory for IAsyncIops which represent a single I/O op
- IASyncIops are enqueue()d to the IAsyncIOManager and then submit()ted
- wait() blocks until a request is completed. The relevant IAsyncIop is returned. It can be
  modified and re-enqueue()d

kernel\_aio.h

- This uses native linux aio, using io\_\* syscalls.
- Using this offers much better performance as all the asynchronous stuff is done in the kernel by
  the disk scheduler
- enqueue()d ops are all submitted to the kernel in a single io\_submit() syscall, so we could add
  support for batched io requests easily. Currently only the initial request is batched.

posix\_aio.h

- The POSIX implementation spawns a lot of extra threads due to the glibc implementation. (It
  emulates asynchronous io using sync io in pthreads).
- Each thread, after enqueuing  and submitting their requests, aio\_suspend()s on an array of 
  aiocb structs. When this function returns, each struct must be polled to check which one
  completed.

perf\_clock.h

- Static interface for getting accurate system timestamps.
- Somewhat unnecessary in C++11; should probably be migrated to chrono::high\_resolution\_clock()

sys\_info.h

- A struct with fields and methods for getting and parsing information about the system's cpus by
  reading /sys and /proc

rng\_engine.h

- A wrapper for seeding and using C++'s random library, using the mersenne twister engine

IoBucketizer.h  
Histogram.h

- These files are directly copies from the Windows implementation of diskspd
- The IoBucketizer is used for calculating the standard deviation of number of iops across the
  duration.
- The Histogram is used to get the mean and standard deviation of iop latency

debug.h

- Global variables and macros implementing d\_printf and v\_printf


# Contributing

This project welcomes contributions and suggestions. Most contributions require you to
agree to a Contributor License Agreement (CLA) declaring that you have the right to,
and actually do, grant us the rights to use your contribution. For details, visit
https://cla.microsoft.com.

When you submit a pull request, a CLA-bot will automatically determine whether you need
to provide a CLA and decorate the PR appropriately (e.g., label, comment). Simply follow the
instructions provided by the bot. You will only need to do this once across all repositories using
our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/)
or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or
comments.

## Getting started

1. Read the README.md, compile and run diskspd, read the wiki on GitHub, and understand how to use
   the various options available.
2. Read HOWITWORKS.md and the code itself to get a sense of how the project is structured.
3. Identify something you want to fix or improve, or a feature that should be added.
    - README.md contains a list of planned features.
    - This file (CONTRIBUTING.md) and HOWITWORKs.md have information on code that can be improved,
      and there are plenty of TODOs in the code itself.
    - The Windows version of diskspd has many features that this one does not yet have. Some of
      those may be good candidates. See the list at the bottom of this file.
4. Fork the project and make changes in a topic branch with a relevant name.
5. Create a pull request!

## Planned improvements

- Use more C++ features - e.g. replace perf\_clock.h with C++11's chrono::high\_resolution\_timer
- Convert raw byte sizes to rounded KB, MB etc in results
- File creation speed - parallelize with libaio
- Move file creation/setup to Target or Profile
- Test on older kernels/gcc versions etc
    - Should compile with GCC 4.8.1 (earliest feature-complete C++11 implementation)
    - Potentially support kernels as early as 2.6
    - Investigate BSD support

## Known issues

- File setup quite slow for the first run
    - currently mitigated by simply using existing files if they're of the right size or larger
    - parallelizing file setup would improve this
- Some combinations of inputs may cause undefined behaviour
    - for the most part everything is checked for validity and conflicting options error out or are
      ignored, but it's possible not every combination is covered
- Possible bugs with Iops std-dev calculation - it seems to match diskspd for Windows behaviour
  however. This could be an issue with how the IoBucketizer is used, or something else.

## Azure specific features

These features will be added specifically for benchmarking Azure Linux systems, but may be useful in
other contexts too.

- print out which io scheduler is in use in the kernel (/sys/block/XXX/queue/scheduler)
- print out whether fua caching is in use (/sys/module/libata/parameters/fua)
    - print other relevant host caching settings

## Windows diskspd features

These are some features the Windows version has that are low-priority or may not be necessary in the
Linux version.

- Start overlapped I/Os with the same offset (-p). I.e., all async IOs issues by a thread (-o) are
  started at the same offset
- Progress indicator (-P)
- More -Z options; e.g. for using a file as a source for the I/O buffers



#!/bin/sh

#
# This file is a developer tool for testing options
# Simply run this script and keep an eye on the output to check for errors
# In future we could automatically fuzz or auto generate different option configurations
#

make

# test all options
bin/diskspd -c1M -d1 -W1 -v df1     # mostly defaults
bin/diskspd -d1 -W1 df1             # no create
bin/diskspd -c1M -d1 -W1 -o1 df1     # single overlap
bin/diskspd -c1M -L -d1 -W1 -o1 df1     # latency stats
bin/diskspd -c1M -L -D50 -d1 -W1 -o1 df1     # std dev iops 50ms
bin/diskspd -c1M -L -D -d1 -W1 -o1 df1     # std dev iops 1000ms
bin/diskspd -c1M -L -D -w100 -d1 -W1 -o1 df1      # write only
bin/diskspd -c1M -L -D -w50 -d1 -W1 -o1 df1       # write 50%
bin/diskspd -c1M -L -D -w50 -Sd -d1 -W1 -o1 df1       # O_DIRECT
bin/diskspd -c1M -L -D -w50 -Ss -d1 -W1 -o1 df1       # O_SYNC
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -o1 df1       # O_SYNC | O_DIRECT
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -o1 -z32 df1 df2    # specific seed
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -o1 -z df1 df2      # random seed
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -o4 -z df1     # multiple overlap
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -o1 -z -t4 df1 # multiple thread
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -o4 -z -t4 df1 # multiple thread, multiple overlap
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -z -t4 df1 df2 # multiple files
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -z -F8 df1 df2 # total threads

bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zz df1 df2 # zero buffers
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zs df1 df2 # separate buffers
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zrs df1 df2 # random separate buffers

bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zs -B12K df1 df2 # base offset
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zs -B12K -f76K df1 df2 # max size

bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zs -s1K df1 df2 # small stride
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zs -r1K df1 df2 # random align
bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zs -si df1 df2 # sequential interlock

bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zs -T1K df1 df2 # small thread stride

bin/diskspd -c1M -L -D -w50 -Sh -d1 -W1 -t4 -z -Zs -xp df1 df2 # posix suspend


# resource-intensive tests - should saturate a high performance SSD on Azure
# keep in mind it takes 20-30 seconds to set the files up
# bin/diskspd -c512M -b1M -L -D -w50 -Sh -z -Zs -d30 -W5 -o32 -t1 df1 df2 df3 df4 df5 df6 df7 df8
# bin/diskspd -c512M -b4K -L -D -w50 -Sh -z -Zs -d30 -W5 -o32 -t1 df1 df2 df3 df4 df5 df6 df7 df8


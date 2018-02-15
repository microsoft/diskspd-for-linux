# Performance testing

diskspd needs to be able to drive large storage devices to full capacity to do its job.

Use these standard Linux tools to profile diskspd, check actual disk iops (because diskspd reports 
each block written/read as an iop) etc..

* perf top
    * with caching on (-Sb, default), lots of time spent in
        * copy_user_enhanced_fast_string (copying to/from buffer-cache)
        * rwsem_spin_on_owner
    * always spend lots of time in
        * \__lock_text_start
        * pthread_mutex_lock
        * native_queued_spin_lock_slowpath
* sudo iotop -o
    * shows all diskspd threads doing their thing
* iostat -x -d 1 100
    * shows net io, actual read/write ops to device (as opposed to what we record in the tool)
    * e.g. if we write 1MiB blocks from the tool, actual read/write ops are about 330KiB
* perf lock <command>
    * NOTE can't use this unless CONFIG\_LOCKDEP and CONFIG\_LOCK\_STAT are enabled in kernel build


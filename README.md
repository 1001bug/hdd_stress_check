# Disk stress test with checks

New disks usually die immediately or run for a long time. Therefore, it is necessary to stress it for a few days. If it survived - well done.  
Writing to files on formatted partition. In turn unlill limit or disk free space reached.  
Then read all data back and check it.

Reading after write has caveats - pagecache keep all written data. If amount of data less than RAM O_DIRECT should be passed to open(). Not all OS has it implementation.  
O_DIRECT is slow like O_SYNC. To speed it up blocksize multiplier can increase write/read buffer size (32, 64, 128 and so on).  
On success returns 0, so can be in cycle until error occurs.  


# Compile
This is NetBeans project but can be compilled like `gcc main.c -o hdd_stress_check`

# Env
Expecting /dev/urandom.
tested on armv7l RPi4B (Debian 10.8 Linux 5.10.11) and INTEL x86_64 (Debian 9.13 Linux 4.9.0)


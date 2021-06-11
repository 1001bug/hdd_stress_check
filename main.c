/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.c
 * Author: alex
 *
 * Created on 7 июня 2021 г., 20:59
 * 
 * can be compilled like `make main`
 * or
 * `gcc main.c -o hdd_stress_check`
 * `gcc --std=c99 main.c -o hdd_stress_check`
 * 
 * tested on armv7l RPi4B (Debian 10.8 Linux 5.10.11) and INTEL x86_64 (Debian 9.13 Linux 4.9.0)
 * compile under cygwin
 * 
 * TODO: log file. But different partition? log file path in ENV? (like ansimble)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include <malloc.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>


extern const char *__progname; //иммя программы без прибамбасов

struct settings {
    int verbose;
    int direct_write;
    int direct_read;
    int sync;
    int blk_mult;
    int f_num;
    unsigned long long bytes;
    char *path;

    int mode_w;
    int mode_r;

};

struct test_file {
    int fd;
    unsigned long bytes_w;
    unsigned long bytes_r;
    char fname[256];
    int do_not_read_any_more;
    int failed;
};

/*
 * 
 */

char * friendly_bytes(unsigned long long bytes) {

    static char formated[24] = {0}; //max ulong 18446744073709551615 = 20 chars
    const char* sf[5] = {"B", "KiB", "MiB", "GiB", "TiB"};

    int k_limit = sizeof (sf) / sizeof (char*) - 1;
    int k = 0;
    long double ff = bytes;
    while (ff >= 1024 && k < k_limit) {
        ff /= 1024;
        k += 1;
    }

    //zero precision for B, 1 for KiB, 2 for MiB, 3 for GiB, 4 for TiB
    int ret = snprintf(formated, sizeof (formated) - 1, "%.*Lf%s", k, ff, sf[k]);

    if (ret < 0 || ret >= sizeof (formated)) {
        formated[0] = '?';
        formated[1] = '\0';
    }



    //ignore crop?
    //check for negative and return empty?


    return formated;
}

int fill_with_random(unsigned char *buf, size_t size) {

    //Fantastic! but it works under cygwin
    FILE *fp;
    fp = fopen("/dev/urandom", "r");
    int ret = fread(buf, 1, size, fp);
    fclose(fp);

    if (ret == size)
        return 0;
    else
        return
        1;



}

int close_files(struct test_file *arr, int num_of_files) {

    for (int i = 0; i < num_of_files; i++) {



        assert(arr[i].fd != 0);

        errno = 0;
        if (close(arr[i].fd)) {
            perror("clsoe()");
            //exit(EXIT_FAILURE);
            return 1;
        }

        arr[i].fd = 0;

    }
    return 0;
}

int unlink_files(struct test_file *arr, int num_of_files) {



    for (int i = 0; i < num_of_files; i++) {



        assert(arr[i].fd == 0);
        assert(strlen(arr[i].fname) > 0);

        errno = 0;
        int ret = unlink(arr[i].fname);
        if (ret) {
            perror("unlink()");
            //exit(EXIT_FAILURE);
            return 1;
        }

    }
    return 0;
    //fprintf(stderr,"%i files removed\n",num_of_files);
}

int open_files(struct settings set, struct test_file *arr, int mode, mode_t mode2) {

    assert(set.path != NULL);
    const int fname_len = sizeof (arr[0].fname) - 1;
    assert(fname_len > 0);

    for (int i = 0; i < set.f_num; i++) {

        if (strlen(arr[i].fname) == 0) {
            
            int ret = snprintf(arr[i].fname, fname_len, "%s%s_%08i.bin"
                    , set.path
                    , __progname
                    , i
                    );

            //do not ignore crop
            //negative is fatal
            if (ret<0 || ret >= fname_len) {
                fprintf(stderr, "compose file name failed. Path too long. (%s)", arr[i].fname);
                //exit(EXIT_FAILURE);
                return 1;
            }
        }

        assert(arr[i].fd == 0);

        //int mode = O_CREAT|O_SYNC|O_DIRECT|O_RDWR;
        //int mode = O_CREAT|O_RDWR;
        errno = 0;
        arr[i].fd = open(arr[i].fname, mode, mode2);

        if (errno || arr[i].fd == 0) {
            int e=errno;
            fprintf(stderr,"open(%i) faled code=%i '%s'\n",i,e,strerror(e));
            if(e == EMFILE || e == ENFILE)
                fprintf(stderr,"Check ulimits for 'nofile'\n");
            //exit(EXIT_FAILURE);
            return 1;
        }

    }
    if (set.verbose)
        fprintf(stderr, "%i files opened\n", set.f_num);
    return 0;
}

char * time_stamp(long _sec) {

    //0d 00:00:01 >= 12
    static char formated[16] = {0};
    int sec = (int) _sec;


    int min = sec / 60;

    sec = sec % 60;

    int hr = min / 60;

    min = min % 60;

    int days = hr / 24;

    hr = hr % 24;


    //ignore crop?
    //check for negative and return empty?
    if(days)
        snprintf(formated, sizeof (formated) - 1, "%id %02i:%02i:%02i", days, hr, min, sec);
    else
        snprintf(formated, sizeof (formated) - 1, "%02i:%02i:%02i", hr, min, sec);
    
    return formated;

    //fprintf(stderr,"%id %02i:%02i:%02i (%li)\n",title,days,hr,min,sec,_sec);
}

void print_stat(unsigned long long bytes, unsigned long long bytes_prev, __time_t sec_start, __time_t sec_end, __time_t sec_prev, char * action, struct settings set) {
    unsigned long long bytes_cycle_diff = (bytes - bytes_prev);// / (1024 * 1024);
    unsigned long long bytes_cycle_diff_s = 0;

    //unsigned long bytes_MB = bytes / (1024L * 1024L);
    //unsigned long bytes_GB = bytes_MB / 1024;
    unsigned long long bytes_total_s = 0;

    __time_t sec_total = sec_end - sec_start;

    if (sec_total) {
        bytes_total_s = bytes / sec_total;
    }


    __time_t sec_cycle = sec_end - sec_prev;




    if (sec_cycle) {
        bytes_cycle_diff_s = bytes_cycle_diff / sec_cycle;
    }


    //calc done percent
    //for unlimited write -1%
    //for read after unlimited write - TOTAL_w to set.bytes
    //for limited - both real
    char * time_is = "Elpsd";
    int prc = -1;
    if (set.bytes) {
        long double pp = (long double) bytes / (long double) set.bytes;
        pp *= 100;
        prc = (int) pp;


        if (bytes_total_s) {
            unsigned long long bytes_left_to = set.bytes - bytes;
            //long double tt = (long double)set.bytes / (long double)bytes;
            long double tt = (long double) bytes_left_to / (long double) bytes_total_s;

            sec_total = (__time_t) tt; // * sec_total - sec_total;
        }

        time_is = "Estmt";
    }
    /* int p_sec = sec_total;
    
   int p_min = p_sec / 60;

    p_sec = p_sec % 60;

    int p_hr = p_min / 60;

    p_min = p_min % 60;

    int p_days = p_hr / 24;

    p_hr = p_hr % 24;
     */

    //percentage of TOTAL_W to TOTAL_w ? how?
    //chage hand made calc of Mib to friendly_bytes with strdupA() - stack mem will freed on print_stat exit
    fprintf(stderr, "%s (%s) %s %9s (%9s/s), Total %9s (%9s/s) [%2i%%] %s%s%s\n"
            , time_stamp(sec_total)//p_days,p_hr,p_min,p_sec
            , time_is
            , action
            , strdupa(friendly_bytes(bytes_cycle_diff))
            , strdupa(friendly_bytes(bytes_cycle_diff_s))
            //, action
            , strdupa(friendly_bytes(bytes))
            , strdupa(friendly_bytes(bytes_total_s))
            , prc
            , set.direct_write ? " D_W" : ""
            , set.sync ? " S_W" : ""
            , set.direct_read ? " D_R" : ""


            );

}

void help(char *msg) {
    if (msg)
        printf("%s\n", msg);

    printf("%s --f[iles-num] <N> [path/]\n"
            "Write random data across <N> files until free space or limit, then read all data back with compare\n"
            "\tOther options:\n"
            "\t--sync\tO_SYNC when write\n"
            "\t--direct-r[ead]\t\tO_DIRECT when read (avoid pagecache)\n"
            "\t--direct-w[rite]\tO_DIRECT when write (avoid pagecache)\n"
            "\t--b[lksize-mult] <N>\tmultiply st_blksize of first target file to write/read at once\n"
            "\t--mb[-limit] <N>\tset limit in Mib across all files (N*1024*1024)\n"
            "\t--gb[-limit] <N>\tset limit in Gib across all files (N*1024*1024*1024)\n"
            "\t--v[erbose]\tprint more messages\n"
            "\tpath must ends with `/`\n"
            , __progname
            );
}

struct settings get_settings(int argc, char** argv) {
    struct settings set = {0};

    set.blk_mult = 1;
    set.path = "";
    set.mode_w = O_CREAT | O_EXCL | O_WRONLY;
    set.mode_r = O_RDONLY;






    struct option long_options[] = {
        /* These options set a flag. */
        {"sync", no_argument, &set.sync, 1},
        {"direct-read", no_argument, &set.direct_read, 1},
        {"direct-write", no_argument, &set.direct_write, 1},
        {"verbose", no_argument, &set.verbose, 1},
        /* These options don’t set a flag.
           We distinguish them by their indices. */
        {"blksize-mult", required_argument, 0, 0},
        {"files-num", required_argument, 0, 0},
        {"mb-limit", required_argument, 0, 0},
        {"gb-limit", required_argument, 0, 0},

        {0, 0, 0, 0}
    };
    for (;;) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {
            case 0:
            {
                /* If this option set a flag, do nothing */
                if (long_options[option_index].flag != 0)
                    break;

                //printf("option %s", long_options[option_index].name);

                if (strcmp(long_options[option_index].name, "blksize-mult") == 0 && optarg) {
                    set.blk_mult = atoi(optarg);
                    assert(set.blk_mult > 0);

                } else if (strcmp(long_options[option_index].name, "files-num") == 0 && optarg) {
                    set.f_num = atoi(optarg);

                } else if (strcmp(long_options[option_index].name, "mb-limit") == 0 && optarg) {
                    set.bytes = atoll(optarg) * 1024 * 1024;
                    assert(set.bytes > 0);

                } else if (strcmp(long_options[option_index].name, "gb-limit") == 0 && optarg) {
                    set.bytes = atoll(optarg) * 1024L * 1024;
                    set.bytes*= 1024L;
                    //overflow?
                    assert(set.bytes > 0);

                } else {
                    fprintf(stderr, "Unexpected option... %s\n", long_options[option_index].name);
                    help(NULL);
                    exit(EXIT_FAILURE);
                }


                break;
            }
            default:
            {
                //getopt_long print error message by itself
                help(NULL);
                exit(EXIT_FAILURE);
            }
        }


    }//for


    //all other have def value
    if (set.f_num == 0) {
        help(NULL);
        exit(EXIT_FAILURE);
    }

    if (set.direct_write)
        set.mode_w |= O_DIRECT;

    if (set.sync)
        set.mode_w |= O_SYNC;

    if (set.direct_read)
        set.mode_r |= O_DIRECT;



    //last arg is path or current folder
    if (optind < argc) {

        set.path = argv[optind];

        size_t path_len = strlen(set.path);
        if (path_len == 0 || set.path[path_len - 1] != '/') {
            help("Path must end with '/'!");
            exit(EXIT_FAILURE);
        }

    }


    fprintf(stderr, "%s: number of files %i, limit %s (%s), blksize multiply %i, O_DIRECT on write %s, O_SYNC on write %s, O_DIRECT on read %s, path: %s\n"
            , __progname
            , set.f_num
            , friendly_bytes(set.bytes)
            , set.bytes ? "LIMITED" : "!!! UNLIMITED WRITE !!!"
            , set.blk_mult
            , set.direct_write ? "YES" : "NO"
            , set.sync ? "YES" : "NO"
            , set.direct_read ? "YES" : "NO"
            , strlen(set.path) ? set.path : "(current folder)"


            );



    return set;
}

int main(int argc, char** argv) {

    //exit on wrong params inside get_settings()
    struct settings set = get_settings(argc, argv);









    struct test_file *test_files_arr = calloc(set.f_num, sizeof (struct test_file));
    assert(test_files_arr != NULL);



    if (open_files(set, test_files_arr, set.mode_w, /*permissions*/S_IRUSR | S_IWUSR) != 0) {
        fprintf(stderr, "open_files(CREATE) failed\n");
        return (EXIT_FAILURE);
    }



    __blksize_t blksize = 0;

    //get st_blksize of first opened file 
    struct stat sb;
    errno = 0;
    if (fstat(test_files_arr[0].fd, &sb) == 0) {

        blksize = sb.st_blksize * set.blk_mult;
        if (set.verbose)
            fprintf(stdout, "fstat: st_blksize %li X multiply %i = %li bytes at once\n", sb.st_blksize, set.blk_mult, blksize);
        assert(blksize > 0);

    } else {
        fprintf(stderr, "fstat to get st_blksize FAILed for '%s', err: %s\n", test_files_arr[0].fname, strerror(errno));
        return (EXIT_FAILURE);

    }




    //for O_DIRECT we need aligned buffer
    //roundup it to pagesize
    size_t pagesize = getpagesize();

    //original buffer with rundom data to write
    unsigned char *buf_write = aligned_alloc(pagesize, (((blksize - 1) / pagesize) + 1) * pagesize);
    //secondary buffer. use when read and compare with original data that should be read if everything is ok
    unsigned char *buf_reread = aligned_alloc(pagesize, (((blksize - 1) / pagesize) + 1) * pagesize);


    if (fill_with_random(buf_write, blksize)) {
        fprintf(stderr, "failed to populate with urandom\n");
        return (EXIT_FAILURE);
    }


    if (set.verbose) {
        for (int i = 0; i < blksize; i++)
            printf("%hu:", buf_write[i]);
        printf("\n");
    }


    struct timespec time_program_start = {0};
    struct timespec time_cycle_start = {0};
    struct timespec time_end = {0};
    __time_t prev_sec = 0;

    clock_gettime(CLOCK_MONOTONIC, &time_program_start);
    clock_gettime(CLOCK_MONOTONIC, &time_cycle_start);

    //for statistics
    prev_sec = time_cycle_start.tv_sec;

    if (set.verbose)
        fprintf(stdout, "Start write!\n");

    unsigned long long TOTAL_w = 0;
    unsigned long long TOTAL_w_prev = 0;
    

    //openmp?
    //multithread simultaneously write all files? (uselessly? serialized on write()?)
    //for now simultaneously by order. fair
    //stop on limit or crop or error 'no free space'
    //exit on error (exept 28-'no free space')
    //print stat at end of cycle if new second starts
    for (;;) {
        for (int i = 0; i < set.f_num; i++) {

            int e = 0;
            errno = 0;
            long ret = write(test_files_arr[i].fd, buf_write, blksize);
            e = errno;
            if (ret < 0) {

                //disk is full - partial write    
                if (e == 28) {
                    fprintf(stdout, "Write() failed with '%s' (%i). Stop write at %s\n", strerror(e), e, friendly_bytes(TOTAL_w));
                    goto stop_write;
                } 
                
                //fatal error
                else {
                    fprintf(stderr, "Write() failed with '%s' (%i). Didnot write anything. Exit! ret %li\n", strerror(e), e, ret);
                    return (EXIT_FAILURE);
                }
            }
            
            test_files_arr[i].bytes_w += ret;
            TOTAL_w += ret;
                
            //disk is full - partial write    
            if (ret != blksize) {
                fprintf(stdout, "Write() failed with '%s' (%i). Write %li instedad of %li. Stop write at %s\n", strerror(e), e, ret, blksize, friendly_bytes(TOTAL_w + ret));
                goto stop_write;
            }
            
            //limit
            if (set.bytes > 0 && TOTAL_w >= set.bytes) {
                fprintf(stdout, "Stop write at %s (limit)\n", friendly_bytes(TOTAL_w) );
                goto stop_write;
            }

        }

        //print stat at end of cycle in new second
        clock_gettime(CLOCK_MONOTONIC, &time_end);
        if (prev_sec != time_end.tv_sec) {

            print_stat(TOTAL_w, TOTAL_w_prev, time_cycle_start.tv_sec, time_end.tv_sec, prev_sec, "Write", set);
            prev_sec = time_end.tv_sec;
            TOTAL_w_prev = TOTAL_w;

        }




    }
stop_write:

    clock_gettime(CLOCK_MONOTONIC, &time_end);

    fprintf(stdout, "Write time: %s, volume write %s\n", time_stamp(time_end.tv_sec - time_cycle_start.tv_sec), friendly_bytes(TOTAL_w));



    if (close_files(test_files_arr, set.f_num) == 0) {
        if (set.verbose)
            fprintf(stdout, "%i files closed\n", set.f_num);
    } else {
        fprintf(stderr, "close_files() failed\n");
        return (EXIT_FAILURE);
    }





    if (open_files(set, test_files_arr, set.mode_r, 0) != 0) {
        fprintf(stderr, "open_files(READ) failed\n");
        return (EXIT_FAILURE);
    }


    //if write is UNLIMITED, cannot calc % done
    //but for reading target is known
    //bytes target for print_stat() while reading
    set.bytes = TOTAL_w;


    //for stat
    unsigned long long TOTAL_r = 0;
    unsigned long long TOTAL_r_prev = 0;

    if (set.verbose)
        fprintf(stdout, "Start read!\n");

    clock_gettime(CLOCK_MONOTONIC, &time_cycle_start);
    prev_sec = time_cycle_start.tv_sec;

    //how to deal with read? each file till end or all files simultaneously?
    //each file - head goes from begin of disk to end as many times as number of files
    //simultaneously - head goes from begin of disk to end once
    //for now - read simultaneously
    for (;;) {

        int sucsess = 0;

        for (int i = 0; i < set.f_num; i++) {
            if (test_files_arr[i].do_not_read_any_more)
                continue;

            errno = 0;
            long ret = read(test_files_arr[i].fd, buf_reread, blksize);
            
            //fatal - skip now and then
            if(ret < 0) {
                fprintf(stderr, "Read() failed with '%s' (%i). Didnot write anything. Exit! ret %li\n", strerror(errno), errno, ret);
                test_files_arr[i].do_not_read_any_more |= 0x8;
                continue;
            }
            
            //end of file - skip now and then
            if (ret == 0) {
                test_files_arr[i].do_not_read_any_more |= 0x1;
                continue;
            }


            //partial read (maybe because partial write), dont read this file next time
            if (ret != blksize) {
                if (set.verbose)
                    fprintf(stdout, "Read() failed with %s. Read %li instedad of %li\n", strerror(errno), ret, blksize);

                test_files_arr[i].do_not_read_any_more |= 0x2;
            }


            //compare original data and read back from file (if not O_DIRECT and written amount of data less then RAM - practicaly no reading from DISK happands (file chache, page cache).
            if (memcmp(buf_write, buf_reread, ret) != 0) {
                if (set.verbose)
                    fprintf(stderr, "memcmp() found difference on %s betwean written data and read back\n", test_files_arr[i].fname);

                test_files_arr[i].do_not_read_any_more |= 0x4;
                continue;
            }

            test_files_arr[i].bytes_r += ret;
            TOTAL_r += ret;

            sucsess += 1;



        }

        //no files during cycle? FINISH
        if (sucsess == 0)
            break;

        //print stat at end of cycle in new second
        clock_gettime(CLOCK_MONOTONIC, &time_end);
        if (prev_sec != time_end.tv_sec) {

            print_stat(TOTAL_r, TOTAL_r_prev, time_cycle_start.tv_sec, time_end.tv_sec, prev_sec, "Read", set);
            prev_sec = time_end.tv_sec;
            TOTAL_r_prev = TOTAL_r;

        }

    }

    //finalyse stat
    clock_gettime(CLOCK_MONOTONIC, &time_end);
    //"Write time:
    fprintf(stdout, "Read time:  %s, volume read %s\n", time_stamp(time_end.tv_sec - time_cycle_start.tv_sec), friendly_bytes(TOTAL_r));
    //fprintf(stdout,"TOTAL TIME: %s, volume r+w  %s\n",time_stamp(time_end.tv_sec-time_program_start.tv_sec),friendly_bytes(TOTAL_r+TOTAL_w));




    //fprintf(stderr,"TIME: %li.%li\n",time_end.tv_sec-time_start.tv_sec,time_end.tv_nsec-time_start.tv_nsec);

    //check w==r
    int files_failed = 0;
    int files_success = 0;

    for (int i = 0; i < set.f_num; i++) {

        if (test_files_arr[i].bytes_r == test_files_arr[i].bytes_w)
            files_success++;
        else {
            fprintf(stderr, "File %s: W %li, R %li, status %i\n", test_files_arr[i].fname, test_files_arr[i].bytes_w, test_files_arr[i].bytes_r, test_files_arr[i].do_not_read_any_more);
            files_failed += 1;
        }

    }


    if (close_files(test_files_arr, set.f_num) == 0) {
        if (set.verbose)
            fprintf(stdout, "%i files closed\n", set.f_num);
    } else {
        fprintf(stderr, "close_files() failed\n");
        return (EXIT_FAILURE);
    }





    //if failed leave files in place
    if (files_failed) {
        fprintf(stderr, "END: TOTAL %i, SUCCESS %i, FAILED %i\n", set.f_num, files_success, files_failed);
        return (EXIT_FAILURE);
    } else {
        if (files_success != set.f_num)
            fprintf(stderr, "aliens!!!!! (1)\n");
        if (TOTAL_r != TOTAL_w)
            fprintf(stderr, "aliens!!!!! (2)\n");

        fprintf(stdout, "HAPPY END: %s, files: %i\n"
                , time_stamp(time_end.tv_sec - time_program_start.tv_sec)
                , set.f_num
                //, friendly_bytes(TOTAL_r + TOTAL_w)
                );

        //cleanup after success
        if (unlink_files(test_files_arr, set.f_num) != 0) {
            fprintf(stderr, "unlink_files() failed\n");
            return (EXIT_FAILURE);
        }


        return (EXIT_SUCCESS);
    }
}


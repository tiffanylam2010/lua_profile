#ifndef __STORAGE_H__
#define __STORAGE_H__

#include <sys/shm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_CHAIN_LEN 10
#define STR_MAX_NUM 10000
#define SIZEOF_INT 4
#define SIZEOF_LONG 8

struct DB {
    void* shmaddr;
    int max_size;
    int used_size; // 当前的用到的size;
};

struct Storage{
    struct DB* evt_db;
    struct DB* str_db;
    char* strlist[STR_MAX_NUM];
    int left;
};

extern struct Storage * create(int evt_key, int evt_size, int map_key, int map_size);
extern void record(struct Storage *st, unsigned long nanosec, const int event, const char* filename, const int line, const char *funcname);
extern void remove_records(int evt_key, int map_key);
extern void load(int evt_key, int map_key);


#endif // __STORAGE_H__

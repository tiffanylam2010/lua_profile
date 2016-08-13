#include <sys/time.h>
#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>

#include "storage.h"

#define NANOSEC 1000000000

// 共享内存的名字
#define EVT_SHMKEY 10123
#define STR_SHMKEY 10124

// 共享内存的大小
#define EVT_SHMSIZE 1024*1024*1024
#define STR_SHMSIZE  1024*1024

static struct Storage* G=NULL;

unsigned long get_nano_sec(){
    struct timespec ti;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ti);
    return ti.tv_sec * NANOSEC + ti.tv_nsec;
}

static void monitor(lua_State *L, lua_Debug *ar) {
    struct Storage * st = G;
    lua_getinfo(L, "nS", ar);
    switch (ar->event) {
        case LUA_HOOKCALL:
        case LUA_HOOKTAILCALL:
        case LUA_HOOKRET:
            record(st, get_nano_sec(), ar->event, ar->short_src, ar->linedefined, ar->name);
            break;
    }
}

static int init(lua_State *L){
    if (G == NULL) {
        remove_records(EVT_SHMKEY, STR_SHMKEY);
        G = create(EVT_SHMKEY, EVT_SHMSIZE, STR_SHMKEY, STR_SHMSIZE);
    }
    return 0;
}


static int profile(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_sethook(L, monitor, LUA_MASKCALL | LUA_MASKRET , 0);
    int args = lua_gettop(L) - 1;
    lua_call(L, args, 0);
    lua_sethook(L, NULL, 0 , 0);
    return 0;
}

static int dump_stats(lua_State *L) {
    load(EVT_SHMKEY, STR_SHMKEY);

}
int luaopen_cpu_profile(lua_State *L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
        { "init", init},
        { "profile", profile},
        { "dump_stats", dump_stats},
        { NULL, NULL },
    };
    luaL_newlib(L,l);
    return 1;
}



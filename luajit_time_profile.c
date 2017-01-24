// only for luajit
// totaltime计算不准确，因为递归尾调用没有办法判断
//
#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
// Windows
#include <Windows.h>
#else
// LINUX
#include <time.h>
#endif

#define NANOSEC 1000000000

#define STACK_SIZE    10000
#define RESULT_SIZE   10000
#define NAMELIST_SIZE 10000

#define NO_INDEX -1
#define ERRNO_OK -1001
#define ERRNO_STACK_FULL  -1001
#define ERRNO_STACK_EMPTY -1002
#define ERRNO_CALLID_INVALID -1003
#define ERRNO_NAMELIST_FULL  -1004
#define ERRNO_RESULT_FULL -1005
#define ERRNO_MALLOC -1006

#define C_FILENAME "[C]"

#define LEAVE_TYPE_HOOK 0
#define LEAVE_TYPE_C_FUNCTION 1
#define LEAVE_TYPE_TAIL_CALL 2
#define LEAVE_TYPE_FLUSH 3



struct CallInfo {
    int call_id;
    int filename_id; // 文件名对应的id
    int function_id; // 函数名对应的id
    int line; // 

    unsigned long begin_time; 
    unsigned long self_time;
    unsigned long children_time ;

    int father_call_id;
};

struct FunctionStat{
    int filename_id;
    int function_id;
    int line;
    unsigned long self_time;
    unsigned long total_time;
    unsigned long call_count;
};

struct ProfileState{
    struct CallInfo stack_data[STACK_SIZE];
    int stack_top;

    char* namelist[NAMELIST_SIZE]; // 
    struct FunctionStat* result[RESULT_SIZE];

    int C_filename_id;

};

static struct ProfileState * G = NULL;

unsigned long get_nano_sec(){
#ifdef _WIN32
  static LARGE_INTEGER frequency;
  if (frequency.QuadPart == 0)
    ::QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER now;
  ::QueryPerformanceCounter(&now);
  return now.QuadPart / double(frequency.QuadPart) * NANOSEC;
#else
  struct timespec now;
    /*clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now); // 本线程到当前代码系统CPU花费的时间 */
    /*clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);  // 本进程到当前代码系统CPU花费的时间*/
    clock_gettime(CLOCK_MONOTONIC, &now); // 从系统启动这一刻起开始计时,不受系统时间被用户改变的影响 
  return now.tv_sec*NANOSEC + now.tv_nsec ;
#endif
}


int init_profile(struct ProfileState *st){
    int i=0;

    st->stack_top = -1;

    for(i=0;i<RESULT_SIZE;i++){
        st->result[i] = NULL;
    }

    for(i=0;i<NAMELIST_SIZE;i++){
        st->namelist[i] = NULL;
    }

    st->C_filename_id = name2id(st->namelist, C_FILENAME);
    /*printf("init profile done\n\n");*/

}

// BKDR Hash Function
unsigned int BKDRHash(const char *str, unsigned int size)
{
    unsigned int seed = 131313; // 31 131 1313 13131 131313 etc..
    unsigned int hash = 0;

    while (*str)
    {
        hash = hash * seed + (*str++);
    }

    return (hash & 0x7FFFFFFF) % size;
}


int name2id(char* namelist[], const char* name){
    unsigned int index;
    int size;
    int i;

    if( !name) {
        return NO_INDEX;
    }

    size = strlen(name);
    if (size<=0) {
        return NO_INDEX;
    }

    index = BKDRHash(name, NAMELIST_SIZE);

    for(i=0; i<NAMELIST_SIZE/3; i++){
        if (namelist[index]){
            if (strcmp(name, namelist[index]) == 0 ){
                // 找到了
                return index;
            }
            else{
                // 继续找下一个
                index = (index+1) % NAMELIST_SIZE;
            }
            
        }else{
            // 有空位置，说明 它不存在了
            // 直接拷贝进去
            size = size + 1; // 多拷贝一个\0
            namelist[index] = (char*)malloc(size);

            if(!namelist[index]){
                return ERRNO_MALLOC;
            }

            memcpy(namelist[index], name, size);

            namelist[index][size-1] = '\0';
            /*printf("name2id(%s) new. id:%d mem:<%s> size:%d\n", name, index, namelist[index], size);*/

            return index;
        }
    }
    printf("ERROR namelist full\n");
    return ERRNO_NAMELIST_FULL;
}

char* id2name(char* namelist[], int index){
    if (index>=0 && index<NAMELIST_SIZE){
        return namelist[index];
    }
    return NULL;
}

int get_call_id(struct FunctionStat* result[], int filename_id, int function_id, int line){
    int i, key, index, seed;
    struct FunctionStat *pStat;
    seed = 1313;
    if (line>0){
        key = filename_id * seed + line*seed ;
    }else{
        key = filename_id * seed + function_id * seed ;
    }
    index = key % RESULT_SIZE;

    for(i=0; i<RESULT_SIZE/3; i++){
        pStat = result[index];
        if (pStat){
            if( (line>0 && filename_id==pStat->filename_id && line==pStat->line) || 
                    (line<0 && filename_id==pStat->filename_id && function_id==pStat->function_id)){
                if (function_id>=0 && pStat->function_id<0){
                    pStat->function_id = function_id;
                }
                return index;
            }else{
                index = (index+1) % RESULT_SIZE;
            }

        }else{
            // 空位置，说明不存在，需要新增
            result[index] = (struct FunctionStat*)malloc(sizeof(struct FunctionStat));
            if (!result[index]){
                printf("ERROR malloc FunctionStat failed\n");
                return ERRNO_MALLOC;
            }

            result[index]->filename_id = filename_id;
            result[index]->function_id = function_id;
            result[index]->line = line;
            result[index]->call_count = 0;
            result[index]->self_time = 0;
            result[index]->total_time = 0;
            return index;
        }
    }
    printf("ERROR result full\n");
    return ERRNO_RESULT_FULL;
}



int add_result(struct FunctionStat* result[], int call_id, int call_count, unsigned long self_time, unsigned long total_time){

    struct FunctionStat* pStat;
    if (call_id>=0 && call_id < RESULT_SIZE && result[call_id]){
        pStat = result[call_id];
        pStat->call_count += call_count;
        pStat->self_time += self_time;
        pStat->total_time += total_time;
        return ERRNO_OK;
    }else{
        printf("ERROR invalid callid %d\n", call_id);
        return ERRNO_CALLID_INVALID;
    }
}

int on_call_event(struct ProfileState *st, unsigned long nanosec, const char* filename, const char* function, int line){
    int filename_id;
    int function_id;
    int father_call_id = -1;
    int call_id;
    struct CallInfo *pInfo;
    /*printf("enter filename:%s function:%s line:%d nanosec:%ld\n", filename, function, line, nanosec);*/

    if (st->stack_top + 1 >= STACK_SIZE ) {
        printf("ERROR STACK FULL\n");
        return ERRNO_STACK_FULL;
    }

    father_call_id = -1;
    filename_id = name2id(st->namelist, filename);
    function_id = name2id(st->namelist, function);

    /*if (st->stack_top >= 0){*/
    while (st->stack_top >= 0){
        pInfo = &(st->stack_data[st->stack_top]);

        if (pInfo->line<0 && pInfo->filename_id == st->C_filename_id){ // 栈顶是  C function
            leave_function(st, nanosec, LEAVE_TYPE_C_FUNCTION);
        }
        else {
            if( line != pInfo->line &&  function_id == pInfo->function_id && function_id>0) { // is tail call
                leave_function(st, nanosec, LEAVE_TYPE_TAIL_CALL);
                father_call_id = pInfo->call_id;
                function_id = -1;
            }
            else{
                // 进入下一个函数，累计栈顶的函数的self_time
                if (nanosec > pInfo->begin_time){
                    pInfo->self_time += nanosec - pInfo->begin_time;
                }
            }
            break;
        }
    }


    // push to stack
    call_id = get_call_id(st->result, filename_id, function_id, line);
    st->stack_top ++;
    pInfo = &(st->stack_data[st->stack_top]);
    pInfo->call_id = call_id;
    pInfo->filename_id = filename_id;
    pInfo->function_id = function_id;
    pInfo->line = line;
    pInfo->father_call_id = father_call_id;
    pInfo->self_time = 0;
    pInfo->children_time = 0;
    pInfo->begin_time = get_nano_sec();
    /*pInfo->begin_time = nanosec;*/

    /*printf("\t[STACK] [PUSH] filename:%s function:%s line:%d\n", filename, function, line);*/
    return ERRNO_OK;
}

int is_same_function(int filename_id_1, int function_id_1, int line_1, int filename_id_2, int function_id_2, int line_2){
    return line_1 == line_2 && filename_id_1 == filename_id_2 && (line_1>0 || filename_id_1 == filename_id_2);
}

int on_return_event(struct ProfileState *st, unsigned long nanosec, const char* filename, const char* function, int line){
    int filename_id = name2id(st->namelist, filename);
    int function_id = name2id(st->namelist, function);
    struct CallInfo *pInfo;
    while (st->stack_top>=0){
        pInfo = &(st->stack_data[st->stack_top]);
        leave_function(st, nanosec, LEAVE_TYPE_HOOK);
        if(is_same_function(pInfo->filename_id, pInfo->function_id, pInfo->line, filename_id, function_id, line)){
            break;
        }
    }
    return ERRNO_OK;
}

int on_flush_stack(struct ProfileState *st) {
    struct CallInfo *pInfo;
    unsigned long nanosec = get_nano_sec();
    while (st->stack_top>=0){
        pInfo = &(st->stack_data[st->stack_top]);
        leave_function(st, nanosec, LEAVE_TYPE_FLUSH);
    }
 
}

int leave_function(struct ProfileState *st, unsigned long nanosec, int leavetype){
    unsigned long total_time = 0;
    struct CallInfo *pInfo ;
    // pop
    if (st->stack_top<0){
        printf("ERROR STACK EMPTY\n");
        return ERRNO_STACK_EMPTY;
    }
    pInfo = &(st->stack_data[st->stack_top]);

    if (nanosec > pInfo->begin_time ){ // 因为nanosec事件发生的时间，begin_time可能被更新为最新时间
        pInfo->self_time += nanosec - pInfo->begin_time;
    }

    total_time = pInfo->self_time + pInfo->children_time;
    add_result(st->result, pInfo->call_id, 1, pInfo->self_time, total_time);

    if (pInfo->father_call_id>=0 ){
        // 是尾调用，把自己的totaltime累计到父函数的totaltime中
        add_result(st->result, pInfo->father_call_id, 0, 0, total_time);
    }

    /*printf("\t[STACK] [POP] filename:%s function:%s line:%d\n", */
            /*id2name(st->namelist, pInfo->filename_id), */
            /*id2name(st->namelist, pInfo->function_id), */
            /*pInfo->line);*/
    st->stack_top --;
    if (st->stack_top >= 0){
        pInfo = &(st->stack_data[st->stack_top]);
        pInfo->children_time += total_time;
        pInfo->begin_time = get_nano_sec(); // 用当前时间而非参数nanosec，以剔除中间的计算时间
        /*pInfo->begin_time = nanosec;*/
    }
    return 0;
}

int print_stats(struct ProfileState *st){
    // 不用排序，打印结果即可
    struct FunctionStat *pStat; 
    char *filename;
    char *funcname;
    char *nil = "nil";
    int i;

    printf("[PROFILE]\tcount\tself_time\ttotal_time\tfunction\n");
    for(i=0; i< RESULT_SIZE; i++){
        if(st->result[i]){
            pStat = st->result[i];
            filename = id2name(st->namelist, pStat->filename_id);
            if(!filename) filename = nil;
            funcname = id2name(st->namelist, pStat->function_id);
            if(!funcname) funcname = nil;

            //count,self_time,total_time,filename:line:funcname
            /*printf("%d\t %f\t %f\t %s:%d:%s\n",*/
            printf("[PROFILE]\t%ld\t%.6f\t%.6f\t%s:%d:%s\n",
                pStat->call_count,
                (double)pStat->self_time/NANOSEC,
                (double)pStat->total_time/NANOSEC,
                filename,
                pStat->line,
                funcname);
   
        }
    }
}


void callhook(lua_State *L, lua_Debug *ar) {
    lua_getinfo(L, "nS", ar);
    switch (ar->event) {
        case LUA_HOOKCALL:
            /*printf("enter_function:filename:%s line:%d function:%s\n", ar->short_src, ar->linedefined, ar->name);*/
            on_call_event(G, get_nano_sec(), ar->short_src, ar->name, ar->linedefined);
            break;
        case LUA_HOOKRET:
            /*printf("leave_function:filename:%s line:%d function:%s\n", ar->short_src, ar->linedefined, ar->name);*/
            on_return_event(G, get_nano_sec(), ar->short_src, ar->name, ar->linedefined); 
            break;
    }
}

static int linit(lua_State *L){
    if (G == NULL) {
        G = (struct ProfileState*)malloc(sizeof(struct ProfileState));
        if(!G) {
            error("malloc failed\n");
            return 0;
        }
        init_profile(G);
    }
    return 0;
}

static int lprofile(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_sethook(L, callhook, LUA_MASKCALL | LUA_MASKRET , 0);
    int args = lua_gettop(L) - 1;
    lua_call(L, args, 0);
    lua_sethook(L, NULL, 0 , 0);
    /*printf("G->stack_top>>:%d\n", G->stack_top);*/
    on_flush_stack(G);
    // TODO: 返回值处理
    return 0;
}

static int lstats(lua_State *L) {
    print_stats(G);
    return 0;
}

static const struct luaL_Reg mylib[] = {
        { "init", linit},
        { "profile", lprofile},
        { "stats", lstats},
        { NULL, NULL },
};


int luaopen_time_profile(lua_State *L) {
    luaL_register(L, "time_profile", mylib);
    return 1;
}



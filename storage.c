#include "storage.h"
/*
 *
 * 一块共享内存放:time(unsigned long),event(int),filename(int),line(int),funcname(int)
 * 另一块共享内存放: id(int)size_of_name(int)name(char*size)
 *
*/

void* _shm_open(int key, int size){
    int mid = 0;
    void *addr = NULL;
    int flag = 0666|IPC_EXCL;
    if (size>0){
        // iscreate
        flag |= IPC_CREAT;
    }
    mid = shmget(key, size, flag);
    if(mid<0){
        perror("shmget fail");
        return NULL;
    }

    addr = shmat(mid, NULL, 0);
    if(addr == (void*)-1){
        perror("shmat fail");
        return NULL;
    }

    return addr;
}

struct DB* _db_init(int key, int max_size){
    struct DB *db = NULL;
    void *shmaddr = _shm_open(key, max_size);
    if (shmaddr){
        db = malloc(sizeof(struct DB));
        db->shmaddr = shmaddr;
        db->max_size = max_size;
        db->used_size = SIZEOF_INT;
        if(max_size>0){ // init for write
            *((int*)db->shmaddr) = 0;
        } // else{// init for read
   }
    return db;
}


void _db_inc_len(struct DB* db){
    (*((int*)db->shmaddr)) ++;
}

int _db_get_len(struct DB* db){
    return *((int*)db->shmaddr);
}

void _db_put(struct DB* db, void* value, int size){
    void * addr = db->shmaddr + db->used_size;
    memcpy(addr, value, size);
    db->used_size += size;
    //assert(db->used_size <= db->max_size);
}

void* _db_get(struct DB* db, int size){
    void * addr = db->shmaddr + db->used_size;
    db->used_size += size;
    return addr;
}

struct Storage * _storage_init(int evt_dbname, int evt_size, int str_dbname, int str_size){
    int i=0;
    struct Storage* st = malloc(sizeof(struct Storage));
    st->evt_db = _db_init(evt_dbname, evt_size);
    st->str_db = _db_init(str_dbname, str_size);

    for(i=0;i<STR_MAX_NUM;i++){
        st->strlist[i] = NULL;
    }
    return st;

}

struct Storage * create(int evt_dbname, int evt_size, int str_dbname, int str_size){
    return _storage_init(evt_dbname, evt_size, str_dbname, str_size);
}

unsigned int BKDRHash(const char *str)
{
    unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
    unsigned int hash = 0;

    while (*str)
    {
        hash = hash * seed + (*str++);
    }

    return (hash & 0x7FFFFFFF);
}


int _string2id(struct Storage *st, const char* name){
    int id = -1;
    int i=0;
    int size;
    if(name) {
        id = BKDRHash(name)%STR_MAX_NUM;
        for(i=0;i<MAX_CHAIN_LEN;i++){
            if(st->strlist[id]){
                // 遇到存在的，对比
                if (strcmp(st->strlist[id], name)==0){
                    return id;
                }
            }else{
                // 遇到空的还没找到,说明不存在
                size = strlen(name)+1;
                st->strlist[id] = (char*)malloc(size);
                memcpy(st->strlist[id], name, size);

                // 放入共享内存
                _db_put(st->str_db, (void*)(&id), SIZEOF_INT);
                _db_put(st->str_db, (void*)(&size), SIZEOF_INT);
                _db_put(st->str_db, (void*)(st->strlist[id]), size);
                _db_inc_len(st->str_db);

                return id;
            }
        }
        // ERROR;
    }

    return id;
    
}
    
void record(struct Storage *st, unsigned long nanosec, const int event, const char* filename, const int line, const char *funcname){
    int id_file = _string2id(st, filename);
    int id_func = _string2id(st, funcname);

    _db_put(st->evt_db, (void*)&(nanosec), SIZEOF_LONG);
    _db_put(st->evt_db, (void*)&(event), SIZEOF_INT);
    _db_put(st->evt_db, (void*)&(id_file), SIZEOF_INT);
    _db_put(st->evt_db, (void*)&(line), SIZEOF_INT);
    _db_put(st->evt_db, (void*)&(id_func), SIZEOF_INT);
    // 记录数加1
    _db_inc_len(st->evt_db);
}

void load(int evt_dbname, int str_dbname){
    int len, i, id, size;
    int event,line, file_id, func_id;
    char *filename;
    char *funcname;
    char default_name[5] = "NULL";
    unsigned long nanosec;

    struct Storage* st = _storage_init(evt_dbname, 0, str_dbname, 0);

    len = _db_get_len(st->str_db);
    for(i=0;i<len;i++){
        id = *((int*)_db_get(st->str_db, SIZEOF_INT));
        size = *((int*)_db_get(st->str_db, SIZEOF_INT));
        st->strlist[id] = (char*)_db_get(st->str_db, size);
    }

    len = _db_get_len(st->evt_db);
    for(i=0;i<len;i++){
        nanosec = *((unsigned long*)_db_get(st->evt_db, SIZEOF_LONG));
        event = *((int*)_db_get(st->evt_db, SIZEOF_INT));
        file_id = *((int*)_db_get(st->evt_db, SIZEOF_INT));
        line = *((int*)_db_get(st->evt_db, SIZEOF_INT));
        func_id = *((int*)_db_get(st->evt_db, SIZEOF_INT));
        filename = st->strlist[file_id];
        if (func_id>=0)
            funcname = st->strlist[func_id];
        else
            funcname = default_name;
        printf("nanosec:%ld event:%d file:%s line:%d func:%s\n",
                nanosec, event, filename, line, funcname );
    }

}

void _remove_shm(int key){
    int shmid = shmget(key, 0, 0);
    if(shmid>=0){
        shmctl(shmid, IPC_RMID, NULL);
    }
}

void remove_records(int evt_key, int map_key) {
    _remove_shm(evt_key);
    _remove_shm(map_key);
}

// open, read
struct Storage* open(int evt_dbname, int str_dbname){
    int i, id, size, len;
    struct Storage *st = _storage_init(evt_dbname, 0, str_dbname, 0);
    if (st){
        len = _db_get_len(st->str_db);
        for(i=0;i<len;i++){
            id = *((int*)_db_get(st->str_db, SIZEOF_INT));
            size = *((int*)_db_get(st->str_db, SIZEOF_INT));
            st->strlist[id] = (char*)_db_get(st->str_db, size);
        }
        st->left = _db_get_len(st->evt_db);
    }
    return st;
}

int read_record(struct Storage* st, unsigned long* nanosec, int* event, char* filename, int* line, char* funcname){
    int file_id,func_id;
    if(st->left>0){
        *nanosec = *((unsigned long*)_db_get(st->evt_db, SIZEOF_LONG));
        *event = *((int*)_db_get(st->evt_db, SIZEOF_INT));
        file_id = *((int*)_db_get(st->evt_db, SIZEOF_INT));
        *line = *((int*)_db_get(st->evt_db, SIZEOF_INT));
        func_id = *((int*)_db_get(st->evt_db, SIZEOF_INT));
        if(file_id>=0){
            memcpy(filename, st->strlist[file_id], sizeof(st->strlist[file_id])+1);
        }
        if(func_id>=0){
            memcpy(funcname, st->strlist[func_id], sizeof(st->strlist[func_id])+1);
        }
        
        st->left -= 1;
        return 1;
    }
    return 0;
}


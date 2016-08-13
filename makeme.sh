gcc -g -fPIC -o cpu_profile.so --shared -I/home/ltt/soft/lua-5.3.2/src -L/home/ltt/soft/lua-5.3.2/src -llua lua_cpu_profile.c storage.c
gcc -g -fPIC -o storage.so --shared  storage.c


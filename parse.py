# coding:utf8
import ctypes
import sys

EVT_SHMKEY = 10123
STR_SHMKEY = 10124

EVENT_RET = 1 
EVENT_CALL = 0 
EVENT_TAILCALL = 4 
NANOSEC = 1000000000

EVT2NAME = {
        EVENT_RET: "EVENT_RET",
        EVENT_CALL: "EVENT_CALL",
        EVENT_TAILCALL: "EVENT_TAILCALL",
}

FILENAME = "./storage.so"
Lib = ctypes.cdll.LoadLibrary(FILENAME)

Lib.open.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
        ]
Lib.open.restype = ctypes.c_void_p

Lib.read_record.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_ulong), #nanosec
            ctypes.POINTER(ctypes.c_int),#event
            ctypes.c_char_p, #filename
            ctypes.POINTER(ctypes.c_int),#line
            ctypes.c_char_p, #funcname
        ]
Lib.read_record.restype = ctypes.c_int

def load():
    st = Lib.open(EVT_SHMKEY,STR_SHMKEY)

    while True:
        filename = ctypes.create_string_buffer("NULL", 256)
        funcname = ctypes.create_string_buffer("NULL", 256)
        nanosec = ctypes.c_ulong(0)
        event = ctypes.c_int(0)
        line = ctypes.c_int(0)
        ret = Lib.read_record(st, nanosec, event, filename, line, funcname)
        if(ret==0):
            break
        else:
            yield (nanosec.value, event.value, filename.value, line.value, funcname.value)
                
def parse(output_file=None):
    parser = Parser()
    for nanosec, event, filename, line, funcname in load():
        # print("nanosec:%s event:%s file:%s line:%s func:%s"%(nanosec, event, filename, line, funcname))
        parser.add_record(nanosec, event, filename, line, funcname)
    parser.dump_output(output_file)


class StackInfo(object):
    def __init__(self, nanosec, event, filename, line, funcname):
        self.nanosec = nanosec
        self.event = event
        self.filename = filename
        self.line = line
        self.funcname = funcname
        self.id = self.__id()
        self.last_nanosec = nanosec;

    def __id(self):
        if self.line > 0:
            return "%s:%s"%(self.filename, self.line)
        else:
            return "%s:%s:%s"%(self.filename, self.line, self.funcname)

    def __repr__(self):
        s = "time:%s event:%s filename:%s line:%s funcname:%s id:%s"\
                %(self.nanosec, EVT2NAME[self.event], self.filename, self.line, self.funcname, self.id)
        return s


class ResultInfo(object):
    def __init__(self, filename, line, funcname):
        self.filename = filename
        self.line = line
        self.funcname = funcname

        self.runtime_total = 0
        self.runtime_internal = 0
        self.call_count = 0
        self.caller_map = {}

class Parser(object):
    def __init__(self):
        self.stack = []
        self.result = {}
        self.profile_time = 0

    def sort_result(self):
        retlist = self.result.values()

        def _sort(a,b):
            if a.runtime_internal > b.runtime_internal:
                return -1
            elif a.runtime_internal == b.runtime_internal:
                return 0
            else:
                return 1
        retlist.sort(_sort)
        return retlist

    def dump_output(self, filename):
        retlist = self.sort_result()

        sumtime = 0
        for id, info in self.result.iteritems():
            sumtime += info.runtime_internal

        line = ["count", "self_time", "%self_time","total_time", "function"]
        linelist = [line,]
        for info in retlist:
            info.s_call_count = "%s"%info.call_count 
            info.s_self_time = "%.3f"%(info.runtime_internal*1.0/NANOSEC)
            info.s_self_time_percent = "%.2f%%"%(info.runtime_internal*100.0/sumtime)
            info.s_total_time = "%.3f"%(info.runtime_total*1.0/NANOSEC)
            info.s_name = "%s:%s:%s"%(info.filename,info.line, info.funcname)

            line = [
                    info.s_call_count,
                    info.s_self_time,
                    info.s_self_time_percent,
                    info.s_total_time,
                    info.s_name,
                    ]
            linelist.append(line)

        # save to file
        if filename:
            fd = open(filename, "w")
        else:
            fd = sys.stdout
        fd.write("total profile_time: %.3f sec\n\n"%(self.profile_time*1.0/NANOSEC))
        for line in linelist:
            fd.write("%10s %10s %10s %10s %s\n"%tuple(line))
        fd.flush()
        if filename:
            fd.close()

    def dump_to_graph(self, graphname):
        import pygraphviz
        g = pygraphviz.AGraph(directed=True)
        for id, info in self.result.iteritems():
            callee = "%s(%s)"%(info.s_name, info.s_self_time_percent)
            for k, v in info.caller_map.iteritems():
                value = self.result[k]
                caller = "%s(%s)"%(value.s_name, value.s_self_time_percent)
                label = "%s"%v["cnt"]
                g.add_edge(caller, callee, label=label)

        g.layout('dot')
        g.draw(graphname)



    def add_record(self, nanosec, event, filename, line, funcname):
        info = StackInfo(nanosec, event, filename, line, funcname)
        if event == EVENT_CALL:
            self.__on_call(info)
        elif event == EVENT_TAILCALL:
            self.__on_call(info)
        else: 
            self.__on_ret(info.id, info.nanosec)

    def __get_ret(self, info):
        ret = self.result.get(info.id)
        if not ret:
            ret = ResultInfo(info.filename, info.line, info.funcname)
            self.result[info.id] = ret

        if info.funcname != ret.funcname and info.funcname != "NULL":
            ret.funcname = info.funcname

        return ret


    def __on_call(self, info):
        ret = self.__get_ret(info)

        caller = None
        if self.stack:
            caller = self.stack[-1]
            # 开始调用之前，先更新调用者的内部时间
            delta = info.nanosec - caller.last_nanosec
            self.result[caller.id].runtime_internal += delta

        self.stack.append(info)

    def __on_ret(self, id, now):
        last_info = self.stack.pop(-1)
        assert(last_info.id == id)

        ret = self.result[id]
        ret.call_count += 1

        delta_total = now - last_info.nanosec # 此函数运行的总时间
        ret.runtime_total += delta_total

        delta_internal = now - last_info.last_nanosec # 
        ret.runtime_internal += delta_internal

        if self.stack:
            caller = self.stack[-1]
            caller.last_nanosec = now

            if caller.id not in ret.caller_map:
                ret.caller_map[caller.id] = {"cnt":0, "time": 0}
            ret.caller_map[caller.id]["cnt"] += 1
            ret.caller_map[caller.id]["time"] += delta_total

            if last_info.event == EVENT_TAILCALL:
                self.__on_ret(caller.id, now)
        else:
            self.profile_time += delta_total

if __name__ == '__main__':
    filename = None
    if len(sys.argv)>2:
        filename = sys.argv[1]
    parse(filename)


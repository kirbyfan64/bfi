#include <asmjit/asmjit.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <utility>
#include <cstring>
#include <string>
#include <vector>

#include <assert.h>

using namespace asmjit::host;
using namespace asmjit;
using std::string;

struct Op {
    enum Type { Inc, Dec, Lptr, Rptr, Lbr, Rbr, Put, Get, Reduce, Nil };
    Op(Type type): type{type} {}
    Type type;
    int fold{0}, offs{0};
};

// Useful for debugging.
std::ostream& operator<<(std::ostream& os, const Op& op) {
    static string strings[] = {
        "Inc", "Dec", "Lptr", "Rptr", "Lbr", "Rbr", "Put", "Get", "Reduce", "Nil"
    };
    os << "Op{" << strings[static_cast<int>(op.type)]
                << "+" << op.offs
                << "*" << op.fold
                << "}";
    return os;
}

using Program = std::vector<Op>;
using BfFunc = void(*)(char*);

inline bool reads(Op::Type x) {
    return x < Op::Lptr || (x > Op::Rbr && x < Op::Nil);
}
inline bool shifts(Op::Type x) { return x == Op::Lptr || x == Op::Rptr; }
inline bool jumps(Op::Type x) { return x == Op::Lbr || x == Op::Rbr; }
inline Op::Type inverse(Op::Type x) {
    assert(x < Op::Reduce);
    if (x == Op::Inc) return Op::Dec;
    return static_cast<Op::Type>(x + (x%2==0 ? 1 : -1));
}

Program parse(std::istream& in) {
    string valid_chars{"+-<>[].,"};
    Program res;
    size_t p;
    while (in) {
        char c = in.get();
        if (in && (p = valid_chars.find(c)) != -1)
            res.emplace_back(static_cast<Op::Type>(p));
    }
    return res;
}

void fold(Program& p) {
    Program res{p};
    Op::Type limit = Op::Rptr;
    Op last{Op::Nil};
    int streak;
    for (int i=0; i<p.size();) {
        streak = 0;
        while (p[i].type <= limit) {
            if (last.type != p[i].type) streak = 0;
            ++res[i-streak].fold;
            if (streak) res[i] = Op::Nil;
            ++streak;
            last = p[i];
            if (i+1 >= p.size()) break;
            ++i;
        }
        last = p[i];
        ++i;
    }
    p = res;
}

void clean(Program& p) {
    Program res;
    for (Op op : p) if (op.type != Op::Nil) res.push_back(op);
    p = res;
}

void simplify(Program& p) {
    Program res;
    if (p.size() <= 2) return;
    for (int i=0; i<p.size();)
        if (i<p.size()-2) {
            // [-]
            if (p[i].type == Op::Lbr && p[i+1].type == Op::Dec &&
                p[i+2].type == Op::Rbr) {
                res.emplace_back(Op::Reduce);
                i += 3;
            }
            // <+>, <->, >+<, >-<, <.>, etc.
            else if (shifts(p[i].type) && reads(p[i+1].type) &&
                     p[i+2].type == inverse(p[i].type) &&
                     p[i+2].fold == p[i].fold) {
                res.push_back(p[i+1]);
                res.back().offs = p[i].type == Op::Lptr ? -p[i].fold : p[i].fold;
                i += 3;
            }
            else res.push_back(p[i++]);
        }
        // WIP
        // [+<->], [<+>-], [-<+>], [<->+], etc.
        //else if (i<p.size()-4 && p[i].type == Op::Lbr && p[i+5] == Op::rbr) {
            //Program slice{p.begin()+i, p.begin()+i+4};
        //    if (shifts(slice[0]))
        //        for (Op op : slice) ;
        //}
        else res.push_back(p[i++]);
    p = res;
}

void optimize(Program& p) {
    fold(p);
    clean(p);
    simplify(p);
}

void dump(const Program& p) {
    for (Op op : p)
        std::cout << op << std::endl;
}

BfFunc gen_func(JitRuntime& r, Program prog, bool debug=false) {
    FileLogger logger{stdout};
    X86Compiler c{&r};
    if (debug) c.setLogger(&logger);
    c.addFunc(kFuncConvHost, FuncBuilder1<void, char*>{});
    X86GpVar tape{c, kVarTypeIntPtr, "tape"};
    c.setArg(0, tape);
    X86GpVar p{c, kVarTypeIntPtr, "p"},
             v_putchar{c, kVarTypeIntPtr, "v_putchar"},
             tmp{c, kVarTypeInt8, "tmp"},
             zero{c, kVarTypeInt8, "zero"};
    c.mov(v_putchar, imm_ptr(reinterpret_cast<void*>(putchar)));
    c.mov(p, tape);
    c.mov(zero, 0);
    std::vector<std::pair<Label, Label>> labels;
    for (Op op : prog) switch (op.type) {
        case Op::Inc:
            c.add(dword_ptr(p, op.offs), op.fold ? op.fold : 1);
            break;
        case Op::Dec:
            c.sub(dword_ptr(p, op.offs), op.fold ? op.fold : 1);
            break;
        case Op::Lptr:
            c.sub(p, op.fold ? op.fold : 1);
            break;
        case Op::Rptr:
            c.add(p, op.fold ? op.fold : 1);
            break;
        case Op::Lbr:
            labels.push_back({Label{c}, Label{c}});
            c.bind(labels.back().second);
            assert(!op.offs);
            c.cmp(byte_ptr(p), 0);
            c.je(labels.back().first);
            break;
        case Op::Rbr:
            c.bind(labels.back().first);
            assert(!op.offs);
            c.cmp(byte_ptr(p), 0);
            c.jne(labels.back().second);
            labels.pop_back();
            break;
        case Op::Put:
            c.mov(tmp, byte_ptr(p, op.offs));
            c.call(v_putchar, kFuncConvHost, FuncBuilder1<void, char>{})
                  ->setArg(0, tmp);
            break;
        case Op::Reduce:
            c.mov(byte_ptr(p, op.offs), zero);
            break;
        case Op::Get:
        case Op::Nil:
            abort();
    }
    c.ret();
    c.endFunc();
    return asmjit_cast<BfFunc>(c.make());
}

int main(int argc, char** argv) {
    bool debug{false};
    if (argc != 2 && argc != 3) {
        std::cout << "usage: " << argv[0] << " <program> [<debug>=0]\n";
        return 1;
    }
    if (argc == 3 && !strcmp(argv[2], "1")) debug = true;
    std::ifstream f(argv[1]);
    if (!f) {
        std::cout << "error while openening file " << argv[1] << std::endl;
        return 1;
    }
    Program prog = parse(f);
    optimize(prog);
    JitRuntime r;
    BfFunc func = gen_func(r, prog, debug);
    // Hope this is big enough.
    char tape[20000];
    memset(tape, 0, sizeof(tape));
    func(tape);
    r.release(static_cast<void*>(f));
    return 0;
}

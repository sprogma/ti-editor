#include <stdio.h>
#define STB_DS_IMPLEMENTATION
#include "external/stb_ds.h"
#include <time.h>
#include <threads.h>
#include <ctype.h>

#define BESTASSERT_IMPLEMENTATION
#include <bestassert.h>
#include <mp.h>

#include <api.h>

void start_term(int);

int main(int argc, char **argv) {
    // init subsystems
    CoreConfig *cfg = config_start();
    config_add_str(cfg, "module.path", "./test/native/native.dll");
    config_add_int(cfg, "net.idle_timeout_ms", 60000);
    int port = -1;
    if (argc == 2) {
        port = atoi(argv[1]);
        config_add_int(cfg, "network.run", 1);
        config_add_int(cfg, "net.port", port);
    } else {
        config_add_int(cfg, "net.port", 1234);
    }
    init_systems(cfg);

    mp_sleep(T_SEC(1));

    NetworkNode peers[128];
    u64 peers_count = network_list(peers, sizeof(peers)/sizeof(*peers));

    printf("CONNECTED TO %lld:\n", peers_count);
    for (u64 i = 0; i < peers_count; ++i) {
        printf("peer[%lld] = id:%lld\n", i, peers[i]);
    }
    printf("\n");

    OperationId opid = create_operation(0xAB0BA, NULL, 0);
    assert(opid != InvalidOperation);

    printf("opid = %lld\n", opid);

    Operation op;
    i32 res2 = get_operation(opid, &op);
    assert(res2 == GET_OPERATION_SUCCESS);
    const IOperation *iop = get_operation_type(op.type);
    assert(iop);
    
    RangeTask data[8];
    i64 res3 = iop->decompose(op.self, &(Range){{1, 1}, {2, -1}}, data, 8);
    assert(res3 > 0);

    for (int i = 0; i < res3; ++i) {
        if (data[i].op == InvalidOperation) {
            u64 siz = data[i].seg.size;
            byte *txt = data[i].seg.data;
            printf("res[%d] = <%.*s>\n", i, (int)siz, (char *)txt);
        } else {
            printf("res[%d] = [REF: op=%lld range=%lld %lld : %lld %lld]\n", 
                    i, data[i].ref.op, data[i].ref.range.l.anchor, data[i].ref.range.l.offset, data[i].ref.range.r.anchor, data[i].ref.range.r.offset);
        }
    }

    printf("raw api completed.\n");

    RequestInfo rq[3] = {
        {
            .request_type = RequestInfoType_Data,
            .data = {
                .op = opid,
                .range = {{1, 1}, {2, -1}},
            },
        },
        {
            .request_type = RequestInfoType_Anchor,
            .anchor = {
                .op = opid,
                .base = 1,
                .request = 2,
            },
        },
        {
            .request_type = RequestInfoType_Data,
            .data = {
                .op = opid,
                .range = {{1, 0}, {2, 0}},
            },
        },
    };

    TaskId tsk[3];

    network_run(network_get_self(), tsk, rq, 3);
    assert(tsk[0] != InvalidTaskId);
    assert(tsk[1] != InvalidTaskId);
    assert(tsk[2] != InvalidTaskId);

    printf("Tasks runned\n");

    struct timespec duration = { .tv_sec = 0, .tv_nsec = 10000000 }; // 10 ms

    DataTaskResult *d0, *d2;
    i64 a1;
    int is_ready;
    printf("wait for d0\n");
    do { d0 = task_get_data(tsk[0], &is_ready); thrd_sleep(&duration, NULL); } while (!is_ready);
    printf("wait for a1\n");
    do { a1 = task_get_anchor(tsk[1], &is_ready); thrd_sleep(&duration, NULL); } while (!is_ready);
    printf("wait for d2\n");
    do { d2 = task_get_data(tsk[2], &is_ready); thrd_sleep(&duration, NULL); } while (!is_ready);
    printf("all data collected\n");

    for (int i = 0; i < d0->data_count; ++i) {
        printf("Task0[%d] = <%.*s> from op=%lld\n", i, (int)d0->data[i].seg.size, (char *)d0->data[i].seg.data, d0->data[i].op);
    }
    printf("Task1 anchor offset is %lld\n", a1);
    for (int i = 0; i < d2->data_count; ++i) {
        printf("Task2[%d] = <%.*s> from op=%lld\n", i, (int)d2->data[i].seg.size, (char *)d2->data[i].seg.data, d2->data[i].op);
    }

    task_unref_segment(d0);
    task_unref_segment(d2);

    printf("Op api completed\n");

    if (port == -1) {
        printf("To get into editor, pass port via args\n");
        return 1;
    }
    
    printf("Starting application...\n");
    Sleep(1000);
    
    start_term(port);

    network_quit();
    
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////
typedef char *(*CmdFn)(char **args, u64 argc);
typedef struct { 
    char *key;
    CmdFn fn;
    char *params;
    char *body;
} Command;
typedef struct { char *key; char *value; } VarEntry;

static VarEntry *vars = NULL;
static Command *g_cmds = NULL;
static u64 g_cmd_count = 0;
static bool g_running = true;

static const char *eval_block(char *code);

static const char *get_var(const char *name) {
    ptrdiff_t idx = shgeti(vars, name);
    return idx >= 0 ? vars[idx].value : "";
}

static void set_var(const char *name, const char *val) {
    ptrdiff_t idx = shgeti(vars, name);
    if (idx >= 0) {
        free(vars[idx].value);
        vars[idx].value = strdup(val);
    } else {
        shput(vars, strdup(name), strdup(val));
    }
}

static bool to_i64(const char *s, int64_t *out) {
    if (!s) return false;
    errno = 0;
    char *end;
    long long val = strtoll(s, &end, 0);
    if (end == s) return false;
    if (errno == ERANGE) return false;
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') return false;
    *out = (int64_t)val;
    return true;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    return s;
}
/////////////////////////////////////////////////////////////////////////////////////////////

static char *cmd_read(char **args, u64 argc) {
    static char buf[1024];
    i64 opid, a1, a2, d1, d2;
    if (argc == 4) {
        if (!to_i64(args[1], &opid)) goto bad;
        if (!to_i64(args[2], &a1)) goto bad;
        if (!to_i64(args[3], &a2)) goto bad;
        d1 = 0;
        d2 = 0;
    } else if (argc == 5) {
        if (!to_i64(args[1], &opid)) goto bad;
        if (!to_i64(args[2], &a1)) goto bad;
        if (!to_i64(args[3], &d1)) goto bad;
        if (!to_i64(args[4], &d2)) goto bad;
        a2 = a1;
    } else if (argc == 6) {
        if (!to_i64(args[1], &opid)) goto bad;
        if (!to_i64(args[2], &a1)) goto bad;
        if (!to_i64(args[3], &d1)) goto bad;
        if (!to_i64(args[4], &a2)) goto bad;
        if (!to_i64(args[5], &d2)) goto bad;
    } else {
        goto bad;
    }

    RequestInfo rq[1] = {
        {
            .request_type = RequestInfoType_Data,
            .data = {
                .op = opid,
                .range = {{a1, d1}, {a2, d2}},
            },
        },
    };
    TaskId tsk;
    network_run(network_get_self(), &tsk, rq, 1);
    assert(tsk != InvalidTaskId);
    DataTaskResult *d0;
    int is_ready;
    struct timespec duration = { .tv_sec = 0, .tv_nsec = 10000000 }; // 10 ms
    do { d0 = task_get_data(tsk, &is_ready); thrd_sleep(&duration, NULL); } while (!is_ready);
    char *dst = buf;
    for (int i = 0; i < d0->data_count; ++i) {
        memcpy(dst, d0->data[i].seg.data, d0->data[i].seg.size);
        dst += d0->data[i].seg.size;
    }
    *dst = 0;
    return buf;
bad:
    printf("ERROR: probably given non integer values, or not 3, 4 or 5...\n");
    buf[0] = 0;
    return buf;
}

static char *cmd_anchor(char **args, u64 argc) {
    static char buf[1024];
    i64 opid, a1, a2;
    if (argc == 4) {
        if (!to_i64(args[1], &opid)) goto bad;
        if (!to_i64(args[2], &a1)) goto bad;
        if (!to_i64(args[3], &a2)) goto bad;
    } else {
        goto bad;
    }

    RequestInfo rq[1] = {
        {
            .request_type = RequestInfoType_Anchor,
            .anchor = {
                .op = opid,
                .base = a1,
                .request = a2,
            },
        },
    };
    TaskId tsk;
    network_run(network_get_self(), &tsk, rq, 1);
    assert(tsk != InvalidTaskId);
    i64 a0;
    int is_ready;
    struct timespec duration = { .tv_sec = 0, .tv_nsec = 10000000 }; // 10 ms
    do { a0 = task_get_anchor(tsk, &is_ready); thrd_sleep(&duration, NULL); } while (!is_ready);
    sprintf(buf, "%lld", a0);
    return buf;
bad:
    printf("ERROR: probably given non integer values, or not 3...\n");
    buf[0] = 0;
    return buf;
}

static char *start_op(char **args, u64 argc) {
    static char buf[64];
    i64 tid;
    char *arg = NULL;
    if (argc == 2) {
        if (!to_i64(args[1], &tid)) goto bad;
    } else if (argc == 3) {
        if (!to_i64(args[1], &tid)) goto bad;
        arg = args[2];
    } else {
        goto bad;
    }

    OperationId opid = create_operation(tid, arg, (arg == NULL ? 0 : strlen(arg)));
    if (opid == InvalidOperation) goto bad;

    sprintf(buf, "%lld", opid);
    return buf;
bad:
    printf("ERROR: probably given non integer type, or not 1 or 2...\n");
    buf[0] = 0;
    return buf;
}

static char *list_net(char **args, u64 argc) {
    if (argc != 1) goto bad;

    static char buf[1024];
    char *dst = buf;
    NetworkNode nodes[64];
    u64 cnt = network_list(nodes, 64);
    for (u64 i = 0; i < cnt; ++i) {
        dst += sprintf(dst, "%lld%s", nodes[i], (i + 1 == cnt ? "\n" : ""));
    }
    return buf;
bad:
    printf("ERROR: probably given not 0...\n");
    buf[0] = 0;
    return buf;
}

static char *list_self(char **args, u64 argc) {
    if (argc != 1) goto bad;
    static char buf[64];
    u64 node = network_get_self();
    sprintf(buf, "%lld", node);
    return buf;
bad:
    printf("ERROR: probably given not 0...\n");
    buf[0] = 0;
    return buf;
}

static char *net_stat(char **args, u64 argc) {
    if (argc != 2) goto bad;
    i64 node;
    if (!to_i64(args[1], &node)) goto bad;
    static char buf[1024];
    char *dst = buf;
    NetworkStat stat = {};
    network_stat(node, &stat);
    #define PRINT_STAT(name, val, scale, unit, end) \
        (dst += ((val) < 0 ? sprintf(dst, "%s %16s %s" end, name, "unknown", unit) \
                           : sprintf(dst, "%s %16.2lf %s" end, name, (double)(val) / (scale), unit)))
    PRINT_STAT("idle_power", stat.idle_power, 1.0, "cores", "\n");
    PRINT_STAT("ping      ", stat.ping,       1e6, "ms",    "\n");
    PRINT_STAT("bandwidth ", stat.bandwidth,  1e6, "MB/s",  "\n");
    PRINT_STAT("free_ram  ", stat.free_ram,   1e9, "GB",    "\n");
    PRINT_STAT("free_disk ", stat.free_disk,  1e9, "GB",    "");
    #undef PRINT_STAT
    return buf;
bad:
    printf("ERROR: probably given not integer or not 1...\n");
    buf[0] = 0;
    return buf;
}

static char *cmd_connect(char **args, u64 argc) {
    if (argc != 2) goto bad;
    i64 target_port;
    if (!to_i64(args[1], &target_port)) goto bad;
    if ((i64)(u16)target_port != target_port) goto bad;
    peer_addr_t addr = {
        .type = PEER_ADDR_DIRECT_IP,
        .direct = {
            .ip = "127.0.0.1",
            .port = target_port
        }
    };
    printf("Connecting to 127.0.0.1:%u...\n", target_port);
    if (!network_open_connection(&addr)) {
        printf("[Chat] Failed to start connection\n");
    }
    return "";
bad:
    printf("ERROR: probably given not integer or not 1... [or port is not u16]\n");
    return "";
}

static char *cmd_echo(char **args, u64 argc) {
    static char buf[1024*8];
    buf[0] = '\0';
    for (u64 i = 1; i < argc; i++) {
        if (i > 1) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, args[i], sizeof(buf) - strlen(buf) - 1);
    }
    printf("%s", buf);
    return "";
}

static char *cmd_cat(char **args, u64 argc) {
    static char buf[1024*8];
    buf[0] = '\0';
    for (u64 i = 1; i < argc; i++) {
        if (i > 1) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, args[i], sizeof(buf) - strlen(buf) - 1);
    }
    return buf;
}

static char *cmd_at(char **args, u64 argc) {
    static char buf[1024];
    if (argc < 2) goto bad;
    i64 id;
    if (!to_i64(args[1], &id)) goto bad;
    id = id + 2;
    if (id >= (i64)argc || id < 2) {
        printf("at: index overbound\n");
        buf[0] = '\0';
        return buf;
    }
    buf[0] = '\0';
    strncat(buf, args[id], sizeof(buf));
    return buf;
bad:
    printf("at: wrong usage [index is int not given]\n");
    buf[0] = '\0';
    return buf;
}

static char *cmd_sub(char **args, u64 argc) {
    static char buf[1024];
    buf[0] = '\0';
    if (argc != 4) { printf("sub: usage sub str l r\n"); return buf; }
    i64 l, r;
    if (!to_i64(args[2], &l) || !to_i64(args[3], &r)) { printf("sub: l, r must be integers\n"); return buf; }
    const char *s = args[1];
    i64 len = (i64)strlen(s);
    if (l < 0) l = 0;
    if (r > len) r = len;
    if (r < l) r = l;
    snprintf(buf, sizeof(buf), "%.*s", (int)(r - l), s + l);
    return buf;
}

static char *cmd_rep(char **args, u64 argc) {
    static char buf[2048];
    buf[0] = '\0';
    if (argc != 4) { printf("rep: usage rep str from to\n"); return buf; }
    const char *s = args[1], *from = args[2], *to = args[3];
    size_t flen = strlen(from), tlen = strlen(to);
    if (flen == 0) { snprintf(buf, sizeof(buf), "%s", s); return buf; }
    char *dst = buf, *end = buf + sizeof(buf) - 1;
    const char *p = s;
    while (*p && dst < end) {
        if (strncmp(p, from, flen) == 0) {
            size_t n = (size_t)(end - dst);
            if (tlen < n) n = tlen;
            memcpy(dst, to, n);
            dst += n;
            p += flen;
        } else {
            *dst++ = *p++;
        }
    }
    *dst = '\0';
    return buf;
}

static char *cmd_len(char **args, u64 argc) {
    static char buf[32];
    if (argc != 2) { printf("len: usage len str\n"); return ""; }
    snprintf(buf, sizeof(buf), "%zu", strlen(args[1]));
    return buf;
}

static char *cmd_upper(char **args, u64 argc) {
    static char buf[1024];
    if (argc != 2) { printf("upper: usage upper str\n"); return ""; }
    snprintf(buf, sizeof(buf), "%s", args[1]);
    for (char *p = buf; *p; p++) *p = toupper((unsigned char)*p);
    return buf;
}

static char *cmd_lower(char **args, u64 argc) {
    static char buf[1024];
    if (argc != 2) { printf("lower: usage lower str\n"); return ""; }
    snprintf(buf, sizeof(buf), "%s", args[1]);
    for (char *p = buf; *p; p++) *p = tolower((unsigned char)*p);
    return buf;
}

static char *cmd_find(char **args, u64 argc) {
    static char buf[32];
    if (argc != 3) { printf("find: usage find str substring\n"); return ""; }
    const char *p = strstr(args[1], args[2]);
    snprintf(buf, sizeof(buf), "%lld", p ? (long long)(p - args[1]) : -1LL);
    return buf;
}


static char *cmd_help(char **args, u64 argc) {
    (void)args; (void)argc;
    printf(
        "Commands:\n"
        "echo -> print all arguments, return nothing \n"
        "    echo [string...] (varialdic) \n"
        "cat -> concats all input using spaces \n"
        "    cat [string...] (varialdic) \n"
        "sub -> substring \n"
        "    sub [string] [left] [right] \n"
        "rep -> replace \n"
        "    rep [string] [from] [to] \n"
        "len -> length \n"
        "    len [string] \n"
        "upper -> make string upper \n"
        "    upper [string] \n"
        "lower -> make string lower \n"
        "    lower [string] \n"
        "find -> find substring, return index or -1 \n"
        "    find [string] [substring] \n"
        "at -> use for arrays, zero indexed \n"
        "    echo [index] [strings...] (varialdic) \n"
        "get -> request text slice from operation \n"
        "    get [op_id] [anchor1] [anchor2] \n"
        "    get [op_id] [anchor1] [anchor2] \n"
        "    get [op_id] [anchor1] [offset1] [offset2] \n"
        "    get [op_id] [anchor1] [offset1] [anchor2] [offset2] \n"
        "pos -> request anchor offset in operation \n"
        "    pos [op_id] [anchor1] [anchor2] \n"
        "op -> start operation, supports only no or one string argument to it \n"
        "    op [type_id] \n"
        "    op [type_id] [string_argument] \n"
        "ls -> list connected networks \n"
        "    ls \n"
        "stat -> get statistic for given node \n"
        "    stat [node] \n"
    );
    printf("Examples: $x = 4 + 5 * (2 + 3); while ($x -lt 40) { $x++; echo $x }; echo (at 2 @(echo x y z))\n");
    return "";
}

static void init_builtins(void) {
    shputs(g_cmds, ((Command){.key="cat",   .fn=cmd_cat}));
    shputs(g_cmds, ((Command){.key="echo",  .fn=cmd_echo}));
    shputs(g_cmds, ((Command){.key="at",    .fn=cmd_at}));
    shputs(g_cmds, ((Command){.key="get",   .fn=cmd_read}));
    shputs(g_cmds, ((Command){.key="pos",   .fn=cmd_anchor}));
    shputs(g_cmds, ((Command){.key="op",    .fn=start_op}));
    shputs(g_cmds, ((Command){.key="con",   .fn=cmd_connect}));
    shputs(g_cmds, ((Command){.key="ls",    .fn=list_net}));
    shputs(g_cmds, ((Command){.key="self",  .fn=list_self}));
    shputs(g_cmds, ((Command){.key="stat",  .fn=net_stat}));
    shputs(g_cmds, ((Command){.key="help",  .fn=cmd_help}));
    shputs(g_cmds, ((Command){.key="sub",   .fn=cmd_sub}));
    shputs(g_cmds, ((Command){.key="rep",   .fn=cmd_rep}));
    shputs(g_cmds, ((Command){.key="len",   .fn=cmd_len}));
    shputs(g_cmds, ((Command){.key="upper", .fn=cmd_upper}));
    shputs(g_cmds, ((Command){.key="lower", .fn=cmd_lower}));
    shputs(g_cmds, ((Command){.key="find",  .fn=cmd_find}));
    g_cmd_count = shlen(g_cmds);
}

/////////////////////////////////////////////////////////////////////////////////////////////

static char *find_match(char *s, char open, char close) {
    int depth = 1;
    for (; *s; s++) {
        if (*s == open) depth++;
        else if (*s == close && --depth == 0) return s;
    }
    return NULL;
}

static char *find_op(char *s, const char *op, size_t op_len) {
    int depth = 0;
    char *found = NULL;
    for (char *p = s; *p; p++) {
        if (*p == '(') depth++;
        else if (*p == ')') depth--;
        else if (depth == 0 && strncmp(p, op, op_len) == 0) {
            if (op_len == 1 && (p == s || !isspace((unsigned char)p[-1]) || !isspace((unsigned char)p[1])))
                continue;
            found = p;
        }
    }
    return found;
}

static void eval_expr(char *expr, char *out, size_t out_sz) {
    expr = trim(expr);
    if (!*expr) { out[0] = '\0'; return; }

    while (*expr == '(' && find_match(expr + 1, '(', ')') == expr + strlen(expr) - 1) {
        expr[strlen(expr) - 1] = '\0';
        expr = trim(expr + 1);
    }

    const char *cmps[] = {"-eq", "-ne", "-lt", "-gt", "-le", "-ge"};
    for (int i = 0; i < 6; i++) {
        char *op = find_op(expr, cmps[i], 3);
        if (op) {
            *op = '\0';
            char l[1024], r[1024];
            eval_expr(expr, l, sizeof(l));
            eval_expr(op + 3, r, sizeof(r));
            i64 a, b;
            bool num = to_i64(l, &a) && to_i64(r, &b);
            int cmp = num ? (a > b) - (a < b) : strcmp(l, r);
            bool res = (i == 0) ? (cmp == 0) : (i == 1) ? (cmp != 0) :
                       (i == 2) ? (cmp <  0) : (i == 3) ? (cmp >  0) :
                       (i == 4) ? (cmp <= 0) : (cmp >= 0);
            snprintf(out, out_sz, "%d", res ? 1 : 0);
            return;
        }
    }

    char *op_add = find_op(expr, "+", 1);
    char *op_sub = find_op(expr, "-", 1);
    char *op_pm = (op_add > op_sub) ? op_add : op_sub;
    if (op_pm) {
        char op_ch = *op_pm;
        *op_pm = '\0';
        char l[1024], r[1024];
        eval_expr(expr, l, sizeof(l));
        eval_expr(op_pm + 1, r, sizeof(r));
        i64 a, b;
        if (to_i64(l, &a) && to_i64(r, &b)) {
            snprintf(out, out_sz, "%lld", (long long)(op_ch == '+' ? a + b : a - b));
        } else if (op_ch == '+') {
            snprintf(out, out_sz, "%s%s", l, r);
        } else {
            snprintf(out, out_sz, "0");
        }
        return;
    }

    char *op_mul = find_op(expr, "*", 1);
    char *op_div = find_op(expr, "/", 1);
    char *op_md = (op_mul > op_div) ? op_mul : op_div;
    if (op_md) {
        char op_ch = *op_md;
        *op_md = '\0';
        char l[1024], r[1024];
        eval_expr(expr, l, sizeof(l));
        eval_expr(op_md + 1, r, sizeof(r));
        i64 a, b;
        if (to_i64(l, &a) && to_i64(r, &b)) {
            i64 res = (op_ch == '*') ? (a * b) : (b != 0 ? a / b : 0);
            snprintf(out, out_sz, "%lld", (long long)res);
        } else {
            snprintf(out, out_sz, "0");
        }
        return;
    }
    
    if (*expr == '$' && expr[1] == '(') {
        char *pos = find_match(expr + 2, '(', ')');
        if (pos != expr + strlen(expr) - 1) {
            printf("Syntax error: unmatched '(' in $(...\n");
            out[0] = 0;
            return;
        }
        *pos = 0;
        snprintf(out, out_sz, "%s", eval_block(expr + 2));
    } else if (*expr == '$') {
        snprintf(out, out_sz, "%s", get_var(expr + 1));
    } else if (*expr == '(') {
        char *pos = find_match(expr + 1, '(', ')');
        if (pos == NULL) {
            printf("Syntax error: unmatched '(' in (...\n");
            out[0] = 0;
            return;
        }
        snprintf(out, out_sz, "%s", eval_block(expr));
    } else if (strchr(expr, ' ') || strchr(expr, '\t')) {
        char *a = strchr(expr, ' ');
        char *b = strchr(expr, '\t');
        char *end = a ? a : b;
        char buf[256] = {};
        bool ok = false;
        memcpy(buf, expr, end - expr);
        if (shgeti(g_cmds, buf) == -1) {
            printf("Unknown command: %s\n", buf);
            out[0] = 0;
        } else {
            snprintf(out, out_sz, "%s", eval_block(expr));
        }
    } else {
        snprintf(out, out_sz, "%s", expr);
    }
}

static void push_split(char *str, char raw_args[64][1024], char **args, u64 *argc) {
    char *tok = strtok(str, " \t\r\n");
    while (tok && *argc < 64) {
        snprintf(raw_args[*argc], 1024, "%s", tok);
        args[*argc] = raw_args[*argc];
        (*argc)++;
        tok = strtok(NULL, " \t\r\n");
    }
}

static char *call_user_func(Command *c, char **args, u64 argc) {
    static char result[1024]; result[0] = '\0';

    char params_copy[1024];
    snprintf(params_copy, sizeof(params_copy), "%s", c->params ? c->params : "");

    char *saved_names[64]; char *saved_vals[64]; int nsaved = 0;

    char *ptok = strtok(params_copy, ",");
    u64 pi = 1;
    while (ptok && pi < argc) {
        ptok = trim(ptok);
        if (*ptok == '$') ptok++;
        ptrdiff_t vi = shgeti(vars, ptok);
        saved_names[nsaved] = strdup(ptok);
        saved_vals[nsaved]  = (vi >= 0) ? strdup(vars[vi].value) : NULL;
        nsaved++;
        set_var(ptok, args[pi]);
        pi++;
        ptok = strtok(NULL, ",");
    }

    char body_copy[4096];
    snprintf(body_copy, sizeof(body_copy), "%s", c->body ? c->body : "");
    const char *r = eval_block(body_copy);
    snprintf(result, sizeof(result), "%s", r ? r : "");

    for (int i = nsaved - 1; i >= 0; i--) {
        if (saved_vals[i]) {
            set_var(saved_names[i], saved_vals[i]);
            free(saved_vals[i]);
        } else {
            ptrdiff_t vi = shgeti(vars, saved_names[i]);
            if (vi >= 0) shdel(vars, saved_names[i]);
        }
        free(saved_names[i]);
    }
    return result;
}

static const char *eval_stmt(char *stmt) {
    stmt = trim(stmt);
    if (!*stmt) return "";
    
    char stmt_copy[1024];
    snprintf(stmt_copy, sizeof(stmt_copy), "%s", stmt);

    size_t len = strlen(stmt);
    if (*stmt == '$' && len > 3 && (strcmp(stmt + len - 2, "++") == 0 || strcmp(stmt + len - 2, "--") == 0)) {
        int delta = (stmt[len - 1] == '+') ? 1 : -1;
        stmt[len - 2] = '\0';
        i64 v = 0;
        to_i64(get_var(stmt + 1), &v);
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)(v + delta));
        set_var(stmt + 1, buf);
        return get_var(stmt + 1);
    }

    int depth = 0;
    for (char *p = stmt; *p; p++) {
        if (*p == '(') depth++;
        else if (*p == ')') depth--;
        else if (*p == '=' && depth == 0 && *stmt == '$') {
            *p = '\0';
            char *vname = trim(stmt + 1);
            const char *val = eval_stmt(p + 1);
            set_var(vname, val);
            return get_var(vname);
        }
    }
    
    char raw_args[64][1024];
    char *args[64];
    u64 argc = 0;
    char *p = stmt;

    while (*p && argc < 64) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (p[0] == '@' && p[1] == '(') {
            char *end = find_match(p + 2, '(', ')');
            char *inner_start = p + 2;
            if (end) {
                *end = '\0';
                p = end + 1;
            } else {
                p += strlen(p);
            }

            char eval_buf[1024];
            eval_expr(inner_start, eval_buf, sizeof(eval_buf));
            push_split(eval_buf, raw_args, args, &argc);
            continue;
        }

        if (p[0] == '@' && p[1] != '\0' && !isspace((unsigned char)p[1])) {
            char *vname = p + 1;
            while (*p && !isspace((unsigned char)*p)) p++;
            char saved = *p;
            *p = '\0';

            char eval_buf[1024];
            snprintf(eval_buf, sizeof(eval_buf), "%s", get_var(vname));
            push_split(eval_buf, raw_args, args, &argc);

            if (saved) p++;
            continue;
        }

        char *start = p;
        if (*p == '(') {
            char *end = find_match(p + 1, '(', ')');
            p = end ? end + 1 : p + strlen(p);
        } else if (*p == '$' && p[1] == '(') {
            char *end = find_match(p + 2, '(', ')');
            p = end ? end + 1 : p + strlen(p);
        } else {
            while (*p && !isspace((unsigned char)*p)) p++;
        }

        char saved = *p;
        *p = '\0';
        eval_expr(start, raw_args[argc], sizeof(raw_args[argc]));
        args[argc] = raw_args[argc];
        argc++;
        if (saved) p++;
    }

    if (argc == 0) return "";

    if (strcmp(args[0], "cls") == 0) { printf("\x1b[2J\x1b[3J\x1b[H"); fflush(stdout); return ""; }
    if (strcmp(args[0], "exit") == 0) { g_running = false; return ""; }

    ptrdiff_t idx = shgeti(g_cmds, args[0]);
    if (idx >= 0) {
        Command *c = &g_cmds[idx];
        if (c->fn) return c->fn(args, argc);
        return call_user_func(c, args, argc);
    }


    static char expr_res[1024];
    eval_expr(stmt_copy, expr_res, sizeof(expr_res));
    return expr_res;
}


static const char *eval_block(char *code) {
    const char *last = "";
    char *p = trim(code);

    while (*p && g_running) {
        while (isspace((unsigned char)*p) || *p == ';') p++;
        if (!*p) break;

        if (strncmp(p, "while", 5) == 0 && (isspace((unsigned char)p[5]) || p[5] == '(')) {
            char *c_start = strchr(p + 5, '(');
            char *c_end = c_start ? find_match(c_start + 1, '(', ')') : NULL;
            char *b_start = c_end ? strchr(c_end + 1, '{') : NULL;
            char *b_end = b_start ? find_match(b_start + 1, '{', '}') : NULL;
            if (!b_end) { printf("Syntax error in while\n"); break; }

            *c_end = '\0';
            *b_end = '\0';
            int guard = 0;
            while (g_running && guard++ < 10000) {
                char cond_buf[256], body_buf[1024];
                snprintf(cond_buf, sizeof(cond_buf), "%s", c_start + 1);
                const char *res = eval_block(cond_buf);
                if (!*res || strcmp(res, "0") == 0) break;

                snprintf(body_buf, sizeof(body_buf), "%s", b_start + 1);
                last = eval_block(body_buf);
            }
            p = b_end + 1;
            continue;
        }

        if (strncmp(p, "function", 8) == 0 && (isspace((unsigned char)p[8]) || p[8] == '(')) {
            char *name_start = p + 8;
            while (isspace((unsigned char)*name_start)) name_start++;
            char *paren = strchr(name_start, '(');
            char *paren_end = paren ? find_match(paren + 1, '(', ')') : NULL;
            char *b_start = paren_end ? strchr(paren_end + 1, '{') : NULL;
            char *b_end = b_start ? find_match(b_start + 1, '{', '}') : NULL;
            if (!b_end) { printf("Syntax error in function\n"); break; }

            size_t nlen = paren - name_start;
            while (nlen > 0 && isspace((unsigned char)name_start[nlen-1])) nlen--;
            char fname[256];
            snprintf(fname, sizeof(fname), "%.*s", (int)nlen, name_start);

            *paren_end = '\0';
            *b_end     = '\0';

            char params_buf[512] = {0};
            char *pp = paren + 1;
            while (isspace((unsigned char)*pp)) pp++;
            snprintf(params_buf, sizeof(params_buf), "%s", pp);

            ptrdiff_t idx = shgeti(g_cmds, fname);
            if (idx >= 0) {
                free(g_cmds[idx].params);
                free(g_cmds[idx].body);
                g_cmds[idx].fn     = NULL;
                g_cmds[idx].params = strdup(params_buf);
                g_cmds[idx].body   = strdup(b_start + 1);
            } else {
                shputs(g_cmds, ((Command){.key = strdup(fname), .fn=NULL, .params=strdup(params_buf), .body=strdup(b_start+1)}));
            }
            g_cmd_count = shlen(g_cmds);
            p = b_end + 1;
            continue;
        }

        int depth = 0;
        char *semi = p;
        while (*semi) {
            if (*semi == '(' || *semi == '{') depth++;
            else if (*semi == ')' || *semi == '}') depth--;
            else if (*semi == ';' && depth == 0) break;
            semi++;
        }
        char saved = *semi;
        *semi = '\0';
        last = eval_stmt(p);
        p = saved ? semi + 1 : semi;
    }
    return last;
}

void start_term(int port) {
    init_builtins();

    printf("\x1b[2J\x1b[3J\x1b[H");
    printf("[Editor] Node running on UDP port %d.\n", port);
    printf("use 'help' to get some help\n");
    char line[1024];
    while (g_running) {
        printf("ED/> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        
        char *trimmed = trim(line);
        bool is_assign = (*trimmed == '$' && strchr(trimmed, '=') != NULL);

        const char *res = eval_block(trimmed);
        if (!is_assign && res && *res) {
            printf("%s\n", res);
        }

    }
}

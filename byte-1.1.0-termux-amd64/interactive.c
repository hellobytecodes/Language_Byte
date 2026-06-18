/*
 * Byte Language Interactive Shell
 * gcc byte.c -o byte -I./src ./src/liblua.a -lm -ldl
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <sys/ioctl.h>

#include "src/lua.h"
#include "src/lualib.h"
#include "src/lauxlib.h"

#define RED    "\033[1;31m"
#define RESET  "\033[0m"

#define HIST_MAX  500
#define BUF_MAX   4096
#define HIST_FILE ".byte_history"

/* ── تاریخچه ── */
static char *hist[HIST_MAX];
static int   hist_len = 0;

static void hist_add(const char *s) {
    if (!s || !*s) return;
    if (hist_len > 0 && strcmp(hist[hist_len-1], s) == 0) return;
    if (hist_len == HIST_MAX) { free(hist[0]); memmove(hist, hist+1, (HIST_MAX-1)*sizeof(char*)); hist_len--; }
    hist[hist_len++] = strdup(s);
}

static void hist_load(void) {
    const char *h = getenv("HOME"); if (!h) return;
    char p[512]; snprintf(p, sizeof(p), "%s/%s", h, HIST_FILE);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[BUF_MAX];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        if (l && line[l-1]=='\n') line[l-1]='\0';
        hist_add(line);
    }
    fclose(f);
}

static void hist_save(void) {
    const char *h = getenv("HOME"); if (!h) return;
    char p[512]; snprintf(p, sizeof(p), "%s/%s", h, HIST_FILE);
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i=0; i<hist_len; i++) fprintf(f, "%s\n", hist[i]);
    fclose(f);
}

/* ── ترمینال raw ── */
static struct termios orig_term;
static int raw_on = 0;

static void term_restore(void) {
    if (raw_on) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term); raw_on=0; }
}

static int term_raw(void) {
    if (!isatty(STDIN_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &orig_term) < 0) return -1;
    struct termios r = orig_term;
    r.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
    r.c_oflag &= ~(OPOST);
    r.c_cflag |= CS8;
    r.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
    r.c_cc[VMIN]=1; r.c_cc[VTIME]=0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &r) < 0) return -1;
    raw_on=1; return 0;
}

/* ── گرفتن عرض ترمینال ── */
static int term_cols(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80; /* پیش‌فرض */
}

/* ── ویرایشگر خط ── */
typedef struct {
    char buf[BUF_MAX];
    int  len, pos, hidx;
    char saved[BUF_MAX];
    const char *prompt;
    int  plen;
} LE;

static int pvisible(const char *s) {
    int n=0, e=0;
    while (*s) {
        if (*s=='\033'){e=1;s++;continue;}
        if (e){if(isalpha((unsigned char)*s))e=0;s++;continue;}
        n++; s++;
    }
    return n;
}

/*
 * le_draw - بازرسم کامل خط (فقط برای history/backspace/cursor move)
 * تایپ معمولی مستقیم write میشه بدون redraw
 */
static void le_draw(LE *e) {
    int cols = term_cols();
    int total_vis = e->plen + e->len;
    int cur_vis   = e->plen + e->pos;
    int end_row   = (total_vis > 0 ? total_vis - 1 : 0) / cols;
    int cur_row   = cur_vis / cols;

    /* برو به ردیف اول */
    if (cur_row > 0) {
        char up[32]; snprintf(up, sizeof(up), "\033[%dA", cur_row);
        write(STDOUT_FILENO, up, strlen(up));
    }
    write(STDOUT_FILENO, "\r", 1);
    write(STDOUT_FILENO, "\033[J", 3);      /* پاک از اینجا تا آخر */
    write(STDOUT_FILENO, e->prompt, strlen(e->prompt));
    write(STDOUT_FILENO, e->buf, e->len);

    /* cursor رو به pos ببر */
    int target_row = cur_vis / cols;
    int target_col = cur_vis % cols;
    int rows_up = end_row - target_row;
    if (rows_up > 0) {
        char up[32]; snprintf(up, sizeof(up), "\033[%dA", rows_up);
        write(STDOUT_FILENO, up, strlen(up));
    }
    write(STDOUT_FILENO, "\r", 1);
    if (target_col > 0) {
        char right[32]; snprintf(right, sizeof(right), "\033[%dC", target_col);
        write(STDOUT_FILENO, right, strlen(right));
    }
}

/*
 * le_ins - تایپ معمولی: فقط کاراکتر رو مستقیم بنویس
 * اگه cursor وسط متنه (pos < len) باید redraw کنه
 */
static void le_ins(LE *e, char c) {
    if (e->len >= BUF_MAX-1) return;
    if (e->pos == e->len) {
        /* cursor آخر متنه - فقط write کن، بذار ترمینال wrap کنه */
        e->buf[e->pos++] = c;
        e->len++;
        e->buf[e->len] = '\0';
        write(STDOUT_FILENO, &c, 1);
    } else {
        /* cursor وسط متنه - باید redraw کنیم */
        memmove(e->buf+e->pos+1, e->buf+e->pos, e->len-e->pos);
        e->buf[e->pos++] = c;
        e->len++;
        e->buf[e->len] = '\0';
        le_draw(e);
    }
}

static void le_delb(LE *e) {
    if (!e->pos) return;
    memmove(e->buf+e->pos-1, e->buf+e->pos, e->len-e->pos);
    e->pos--; e->len--; e->buf[e->len]='\0'; le_draw(e);
}

static void le_delf(LE *e) {
    if (e->pos>=e->len) return;
    memmove(e->buf+e->pos, e->buf+e->pos+1, e->len-e->pos-1);
    e->len--; e->buf[e->len]='\0'; le_draw(e);
}

static void le_wl(LE *e) {
    while(e->pos>0 && !isalnum((unsigned char)e->buf[e->pos-1])) e->pos--;
    while(e->pos>0 &&  isalnum((unsigned char)e->buf[e->pos-1])) e->pos--;
    le_draw(e);
}

static void le_wr(LE *e) {
    while(e->pos<e->len && !isalnum((unsigned char)e->buf[e->pos])) e->pos++;
    while(e->pos<e->len &&  isalnum((unsigned char)e->buf[e->pos])) e->pos++;
    le_draw(e);
}

static void le_hp(LE *e) {
    if (!hist_len) return;
    if (e->hidx==-1) { strncpy(e->saved,e->buf,BUF_MAX-1); e->hidx=hist_len-1; }
    else if (e->hidx>0) e->hidx--;
    else return;
    strncpy(e->buf, hist[e->hidx], BUF_MAX-1);
    e->len=e->pos=strlen(e->buf); le_draw(e);
}

static void le_hn(LE *e) {
    if (e->hidx==-1) return;
    if (e->hidx<hist_len-1) { e->hidx++; strncpy(e->buf,hist[e->hidx],BUF_MAX-1); }
    else { e->hidx=-1; strncpy(e->buf,e->saved,BUF_MAX-1); }
    e->len=e->pos=strlen(e->buf); le_draw(e);
}

static volatile int got_int = 0;
static void on_sigint(int s) { (void)s; got_int=1; }

static int le_read(const char *prompt, char *out, int max) {
    LE e; memset(&e,0,sizeof(e));
    e.hidx=-1; e.prompt=prompt; e.plen=pvisible(prompt);

    if (!isatty(STDIN_FILENO) || term_raw()<0) {
        write(STDOUT_FILENO, prompt, strlen(prompt));
        if (!fgets(out, max, stdin)) return 1;
        size_t l=strlen(out); if(l&&out[l-1]=='\n') out[l-1]='\0';
        return 0;
    }

    got_int=0; signal(SIGINT, on_sigint);
    write(STDOUT_FILENO, prompt, strlen(prompt));

    for(;;) {
        if (got_int) {
            got_int=0; term_restore();
            /* رفتن به آخر متن فعلی و شروع خط جدید */
            e.pos = e.len;
            write(STDOUT_FILENO, e.buf + e.pos, 0); /* dummy */
            write(STDOUT_FILENO, "\r\n", 2);
            out[0]='\0'; term_raw();
            e.len=e.pos=0; e.buf[0]='\0'; e.hidx=-1;
            write(STDOUT_FILENO, prompt, strlen(prompt));
            continue;
        }

        unsigned char c;
        if (read(STDIN_FILENO,&c,1)<=0) { term_restore(); write(STDOUT_FILENO,"\n",1); return 1; }

        if (c=='\r'||c=='\n') {
            /* برو به آخر متن و بعد newline بزن - مثل پایتون */
            e.pos = e.len;
            le_draw(&e);
            e.buf[e.len]='\0'; strncpy(out,e.buf,max-1); out[max-1]='\0';
            term_restore(); write(STDOUT_FILENO,"\r\n",2); return 0;
        }
        if (c==127||c==8)  { le_delb(&e); continue; }
        if (c==1)          { e.pos=0;     le_draw(&e); continue; }
        if (c==5)          { e.pos=e.len; le_draw(&e); continue; }
        if (c==2)          { if(e.pos>0){e.pos--;le_draw(&e);} continue; }
        if (c==6)          { if(e.pos<e.len){e.pos++;le_draw(&e);} continue; }
        if (c==11)         { e.len=e.pos; e.buf[e.len]='\0'; le_draw(&e); continue; }
        if (c==21)         { memmove(e.buf,e.buf+e.pos,e.len-e.pos); e.len-=e.pos; e.pos=0; e.buf[e.len]='\0'; le_draw(&e); continue; }
        if (c==12)         { write(STDOUT_FILENO,"\033[2J\033[H",7); le_draw(&e); continue; }
        if (c==4&&!e.len)  { term_restore(); write(STDOUT_FILENO,"\n",1); return 1; }
        if (c==4)          { le_delf(&e); continue; }

        if (c=='\033') {
            unsigned char s1=0,s2=0;
            if (read(STDIN_FILENO,&s1,1)<=0) continue;
            if (s1=='b') { le_wl(&e); continue; }
            if (s1=='f') { le_wr(&e); continue; }
            if (s1=='[') {
                if (read(STDIN_FILENO,&s2,1)<=0) continue;
                if (s2=='A') { le_hp(&e); continue; }
                if (s2=='B') { le_hn(&e); continue; }
                if (s2=='C') { if(e.pos<e.len){e.pos++;le_draw(&e);} continue; }
                if (s2=='D') { if(e.pos>0){e.pos--;le_draw(&e);} continue; }
                if (s2=='H') { e.pos=0;     le_draw(&e); continue; }
                if (s2=='F') { e.pos=e.len; le_draw(&e); continue; }
                if (s2=='3') { unsigned char t; read(STDIN_FILENO,&t,1); if(t=='~') le_delf(&e); continue; }
                if (s2=='1') {
                    unsigned char t[3]; read(STDIN_FILENO,t,3);
                    if(t[0]==';'&&t[1]=='5') { if(t[2]=='D') le_wl(&e); else if(t[2]=='C') le_wr(&e); }
                    continue;
                }
            }
            if (s1=='O') {
                if (read(STDIN_FILENO,&s2,1)<=0) continue;
                if (s2=='H') { e.pos=0;     le_draw(&e); continue; }
                if (s2=='F') { e.pos=e.len; le_draw(&e); continue; }
            }
            continue;
        }

        if (c>=32&&c<127) { le_ins(&e,c); continue; }
        if (c>=0x80) {
            le_ins(&e,c);
            int ex=0;
            if((c&0xE0)==0xC0) ex=1;
            else if((c&0xF0)==0xE0) ex=2;
            else if((c&0xF8)==0xF0) ex=3;
            for(int i=0;i<ex;i++){unsigned char b;if(read(STDIN_FILENO,&b,1)>0)le_ins(&e,b);}
        }
    }
}

/* ── Lua ── */
static lua_State *L;

static void lua_setup(void) {
    luaL_openlibs(L);
    luaL_dostring(L,
        "echo=function(...) local t={...} for _,v in ipairs(t) do io.write(tostring(v)) end end\n"
        "println=function(...) local t={...} for _,v in ipairs(t) do io.write(tostring(v)) end io.write('\\n') end\n"
        "function input(m) io.write(m or '') io.flush() return io.read() end\n"
        "package.path='./libs/Lua/?.by;./libs/Lua/?.lua;'..package.path\n"
        "package.cpath='./libs/C/?.so;'..package.cpath\n"
        "function _byte_include(name)\n"
        "  local ok,lib=pcall(require,name)\n"
        "  if ok then _G[name]=lib else print('[!] cannot load: '..name..': '..lib) end\n"
        "end\n"
        "function exit(c) os.exit(c or 0) end\n"
        "function help()\n"
        "  print('Byte Language 1.1  (Lua 5.4 core)')\n"
        "  print('Commands : exit() | help() | import(\"lib\")')\n"
        "  print('Shortcuts: Up/Down=history  Ctrl+A/E=home/end')\n"
        "  print('           Alt+B/F=word  Ctrl+K/U=kill  Ctrl+L=clear')\n"
        "end\n"
        "function dir(obj)\n"
        "  local t = type(obj)\n"
        "  local items = {}\n"
        "  if t == 'table' then\n"
        "    for k,v in pairs(obj) do\n"
        "      table.insert(items, {name=tostring(k), kind=type(v)})\n"
        "    end\n"
        "  elseif t == 'string' then\n"
        "    for k,v in pairs(string) do\n"
        "      table.insert(items, {name=tostring(k), kind=type(v)})\n"
        "    end\n"
        "  else\n"
        "    print('[dir] expected a table or library, got: '..t)\n"
        "    return\n"
        "  end\n"
        "  table.sort(items, function(a,b) return a.name < b.name end)\n"
        "  local funcs, fields = {}, {}\n"
        "  for _,item in ipairs(items) do\n"
        "    if item.kind == 'function' then\n"
        "      table.insert(funcs, item.name)\n"
        "    else\n"
        "      table.insert(fields, item.name..' ('..item.kind..')')\n"
        "    end\n"
        "  end\n"
        "  if #funcs > 0 then\n"
        "    print('Methods:')\n"
        "    for _,n in ipairs(funcs) do print('  '..n..'()') end\n"
        "  end\n"
        "  if #fields > 0 then\n"
        "    print('Fields:')\n"
        "    for _,n in ipairs(fields) do print('  '..n) end\n"
        "  end\n"
        "  print('Total: '..#funcs..' methods, '..#fields..' fields')\n"
        "end\n"
    );
}

static void preproc(const char *in, char *out, int max) {
    lua_getglobal(L,"string"); lua_getfield(L,-1,"gsub");
    lua_pushstring(L,in);
    lua_pushstring(L,"import%s*%(%s*['\"]([%w_]+)['\"]%s*%)");
    lua_pushstring(L,"_byte_include('%1')");
    if(lua_pcall(L,3,1,0)==LUA_OK) strncpy(out,lua_tostring(L,-1),max-1);
    else strncpy(out,in,max-1);
    lua_settop(L,0);
}

static int check_code(const char *code) {
    int st = luaL_loadstring(L, code);
    if (st == LUA_OK) { lua_pop(L,1); return 0; }
    if (st == LUA_ERRSYNTAX) {
        const char *m = lua_tostring(L,-1);
        int inc = m && strstr(m,"<eof>") != NULL;
        lua_pop(L,1);
        return inc ? 1 : -1;
    }
    lua_pop(L,1);
    return -1;
}

static const char *clean_err(const char *msg) {
    if (!msg) return "unknown error";
    const char *p = strstr(msg, "]:");
    if (p) {
        p += 2;
        while (*p && *p != ' ' && *p != ':') p++;
        if (*p == ':') p++;
        if (*p == ' ') p++;
        return p;
    }
    return msg;
}

static void exec(const char *code) {
    char proc[BUF_MAX*8]; preproc(code,proc,sizeof(proc));

    char expr[BUF_MAX*8+8]; snprintf(expr,sizeof(expr),"return %s",proc);
    if(luaL_loadstring(L,expr)==LUA_OK) {
        if(lua_pcall(L,0,LUA_MULTRET,0)==LUA_OK) {
            int n=lua_gettop(L);
            for(int i=1;i<=n;i++){
                if(i>1) printf("\t");
                int t=lua_type(L,i);
                if(t==LUA_TSTRING)        printf("%s",lua_tostring(L,i));
                else if(t==LUA_TNIL)      printf("nil");
                else if(t==LUA_TBOOLEAN)  printf("%s",lua_toboolean(L,i)?"true":"false");
                else {
                    lua_getglobal(L,"tostring"); lua_pushvalue(L,i);
                    lua_pcall(L,1,1,0); printf("%s",lua_tostring(L,-1)); lua_pop(L,1);
                }
            }
            if(n>0) printf("\n");
            lua_settop(L,0); return;
        }
        const char *err = lua_tostring(L,-1);
        fprintf(stderr, RED"Error: "RESET"%s\n", clean_err(err));
        lua_settop(L,0); return;
    } else lua_pop(L,1);

    if(luaL_loadstring(L,proc)!=LUA_OK){
        fprintf(stderr, RED"SyntaxError: "RESET"%s\n", clean_err(lua_tostring(L,-1)));
        lua_settop(L,0); return;
    }
    if(lua_pcall(L,0,0,0)!=LUA_OK)
        fprintf(stderr, RED"Error: "RESET"%s\n", clean_err(lua_tostring(L,-1)));
    lua_settop(L,0);
}

/* ── REPL ── */
static void repl(void) {
    printf("Byte Language 1.1 (Lua 5.4 core)\n");
    printf("Type \"help()\" for more information.\n");

    hist_load();
    char line[BUF_MAX], ml[BUF_MAX*8]; ml[0]='\0';
    int multi=0;

    for(;;) {
        const char *p = multi ? "... " : ">>> ";
        if(le_read(p,line,BUF_MAX)==1){ printf("\n"); break; }

        if(!strlen(line)){
            if(multi){ exec(ml); multi=0; ml[0]='\0'; }
            continue;
        }

        char test[BUF_MAX*8];
        if(multi){
            snprintf(test, sizeof(test), "%s\n%s", ml, line);
        } else {
            strncpy(test, line, sizeof(test)-1);
            test[sizeof(test)-1]='\0';
        }

        int st = check_code(test);

        if(st == 1) {
            strncpy(ml, test, sizeof(ml)-1);
            ml[sizeof(ml)-1]='\0';
            multi=1;
            hist_add(line);
            continue;
        }

        if(st == -1 && multi) {
            strncpy(ml, test, sizeof(ml)-1);
            exec(ml);
            multi=0; ml[0]='\0';
            hist_add(line);
            continue;
        }

        hist_add(line);
        exec(test);
        multi=0; ml[0]='\0';
    }

    hist_save();
}

/* ── اجرای فایل ── */
static int run_file(const char *fn) {
    FILE *f=fopen(fn,"rb"); if(!f){ fprintf(stderr,"Error: cannot open '%s'\n",fn); return 1; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *raw=malloc(sz+1); if(!raw){fclose(f);return 1;}
    fread(raw,1,sz,f); fclose(f); raw[sz]='\0';

    lua_getglobal(L,"string"); lua_getfield(L,-1,"gsub");
    lua_pushstring(L,raw);
    lua_pushstring(L,"import%s*%(%s*['\"]([%w_]+)['\"]%s*%)");
    lua_pushstring(L,"_byte_include('%1')");
    free(raw);
    if(lua_pcall(L,3,1,0)!=LUA_OK){
        fprintf(stderr,"Error: %s\n",lua_tostring(L,-1)); lua_settop(L,0); return 1;
    }
    if(luaL_dostring(L,lua_tostring(L,-1))!=LUA_OK){
        fprintf(stderr,RED"Error:\n"RESET"%s\n",lua_tostring(L,-1));
        lua_settop(L,0); return 1;
    }
    lua_settop(L,0); return 0;
}

int main(int argc, char **argv) {
    L=luaL_newstate(); lua_setup();
    int r=(argc>=2)?run_file(argv[1]):(repl(),0);
    lua_close(L); return r;
}

#ifndef __RPC_MAIN_HEADER__
#define __RPC_MAIN_HEADER__

struct commandline {
        int cflag;
        int hflag;
        int lflag;
        int sflag;
        int mflag;
        char *infile;
        char *outfile;
};

static char *extendfile(char *file, char *ext);
static void open_output(char *infile, char*outfile);
static void open_input(char *infile, char *define);
static void c_output(char *infile, char *define,
                     int extend, char *outfile);
static void h_output(char *infile, char *define,
                     int extend, char *outfile);
static void s_output(int argc, char *argv[],
                     char *infile, char *define,
                     int extend, char *outfile,
                     int nomain);
static void l_output(char *infile, char *define,
                     int extend, char *outfile);
static void do_registers(int argc, char *argv[]);
static int parseargs(int argc, char *argv[],
                     struct commandline *cmd);

#endif //__RPC_MAIN_HEADER__

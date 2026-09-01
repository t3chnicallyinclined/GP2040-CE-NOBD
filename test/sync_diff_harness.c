/*
 * test/sync_diff_harness.c -- the C half of the sync-window differential.
 *
 * Emits the shipping core's output for a deterministic pseudo-random script.
   argv: seed window release_debounce commit_at preserve_width synced attack */
#include <stdio.h>
#include <stdlib.h>
#include "sync_window.h"
static unsigned st;
static unsigned nxt(void){ st = st*1664525u + 1013904223u; return (st>>16)&0xFFFFu; }
int main(int argc,char**argv){
    st = (unsigned)atoi(argv[1]);
    sync_window_t s;
    sync_window_init(&s,(unsigned)atoi(argv[2]),atoi(argv[3])!=0);
    sync_window_set_masks(&s,(buttons_t)strtoul(argv[6],0,0),(buttons_t)strtoul(argv[7],0,0));
    sync_window_set_commit_at(&s,(unsigned)atoi(argv[4]));
    if(atoi(argv[5])) sync_window_set_preserve_width(&s,1);
    unsigned raw=0;
    for(unsigned t=0;t<400;t++){
        unsigned r=nxt();
        if(r&1u) raw ^= (1u<<((r>>1)&7u));   /* toggle a random bit of the low 8 */
        printf("%u\n",(unsigned)sync_window_step(&s,t,(buttons_t)(raw&0xFFu)));
    }
    return 0;
}

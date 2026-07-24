#include <assert.h>
#include <stddef.h>   /* NULL */
#include <string.h>   /* memcpy (on-chip: mcpy) */
#include "buffer.h"

/*
 * Single-writer seqlock. The writer owns `seq`: it is even between writes and odd
 * during one, so the single-writer asserts (begin requires even, data/end require
 * odd) enforce a well-formed begin -> data -> end sequence. The reader is read-only
 * and never blocks the writer.
 */

void buffer_init(buffer_t *b)
{
    assert(b != NULL);
    atomic_store_explicit(&b->seq, 0u, memory_order_relaxed);
    memset(&b->slot, 0, sizeof b->slot);
}

void buffer_write_begin(buffer_t *b)
{
    assert(b != NULL);
    uint32_t s = atomic_load_explicit(&b->seq, memory_order_relaxed);
    assert((s & 1u) == 0u);                                 /* single writer: must be stable (even) */
    atomic_store_explicit(&b->seq, s + 1u, memory_order_release);   /* -> odd (write in progress) */
}

void buffer_write_data(buffer_t *b, const input_frame_t *f)
{
    assert(b != NULL);
    assert(f != NULL);
    assert((atomic_load_explicit(&b->seq, memory_order_relaxed) & 1u) == 1u);  /* must be mid-write */
    memcpy(&b->slot, f, sizeof b->slot);                    /* on-chip: mcpy hardware block-copy */
}

void buffer_write_end(buffer_t *b)
{
    assert(b != NULL);
    uint32_t s = atomic_load_explicit(&b->seq, memory_order_relaxed);
    assert((s & 1u) == 1u);                                 /* must be mid-write */
    atomic_store_explicit(&b->seq, s + 1u, memory_order_release);   /* -> even (published) */
}

void buffer_publish(buffer_t *b, const input_frame_t *f)
{
    buffer_write_begin(b);
    buffer_write_data(b, f);
    buffer_write_end(b);
}

bool buffer_snapshot(const buffer_t *b, input_frame_t *out)
{
    assert(b != NULL);
    assert(out != NULL);
    for (uint32_t attempt = 0; attempt < BUFFER_MAX_RETRIES; attempt++) {
        uint32_t s1 = atomic_load_explicit(&b->seq, memory_order_acquire);
        if (s1 & 1u)
            continue;                                       /* write in progress -> retry */
        input_frame_t tmp;
        memcpy(&tmp, &b->slot, sizeof tmp);
        uint32_t s2 = atomic_load_explicit(&b->seq, memory_order_acquire);
        if (s1 == s2) {                                     /* no write intervened -> clean */
            *out = tmp;
            return true;
        }
    }
    return false;                                           /* retries exhausted -> caller keeps its last */
}

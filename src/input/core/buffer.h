#ifndef NOBD_CORE_BUFFER_H
#define NOBD_CORE_BUFFER_H
#include <stdatomic.h>
#include <stdbool.h>
#include "frame.h"   /* input_frame_t -- the published frame this buffer carries */

/*
 * The cross-core input buffer -- the single published frame the input pipeline (V3F)
 * writes and the ACTIVE reflector reads (V5F for USB/Ethernet, or the PIOC mailbox
 * for the Maple/Dreamcast input_mode). Only ONE output input_mode is active at a time, so
 * this is single-writer / single-reader, NOT a broadcast.
 *
 * Why it exists (the one hard problem): writer and reader run on DIFFERENT cores over
 * shared SRAM, so a naive read can see a torn frame (some words old, some new). This
 * is a seqlock: the writer bumps `seq` odd before writing and even after; the reader
 * reads seq, copies the frame, reads seq again, and retries if a write intervened.
 * Single writer, so no writer lock is needed.
 *
 * Real-time safe: the reader retries a BOUNDED number of times, then keeps its
 * previous snapshot (consistent, at most a tick stale) rather than spin -- a
 * fight-stick reader must produce a report NOW (bounded-loop rule).
 *
 * On-chip (golden rule -- offload from the CPU): `seq` is an RV32A atomic
 * (acquire/release); the frame copy is the V3F `mcpy` hardware block-copy; a
 * cross-core doorbell (HSEM/IPC) wakes the reader so it need not poll. Portable here
 * via <stdatomic.h> + memcpy, which lower to exactly those on the chip.
 *
 * HONEST SCOPE: input/dst.py tier I15 fuzzes the seqlock PROTOCOL (odd-seq reject,
 * bounded retry, never return an uncommitted frame) by interleaving the real C
 * publish / snapshot / decomposed-write calls. True cross-core MEMORY ORDERING
 * (barrier placement, intra-copy tearing) is a board bring-up item -- a host sim has
 * no real second core.
 */
#define BUFFER_MAX_RETRIES 4u    /* reader attempts before falling back to its last snapshot */

typedef struct {
    atomic_uint   seq;           /* even = stable, odd = write in progress */
    input_frame_t slot;          /* the single seqlock-protected data area (see frame.h) */
} buffer_t;

void buffer_init(buffer_t *b);
void buffer_publish(buffer_t *b, const input_frame_t *f);    /* writer (V3F): begin, copy, end */
bool buffer_snapshot(const buffer_t *b, input_frame_t *out); /* reader: true+fill if clean; false = keep your last (out untouched) */

/* Decomposed writer steps (buffer_publish = begin -> data -> end). Exposed so a test
 * can create an in-progress-write state and confirm the reader rejects it. */
void buffer_write_begin(buffer_t *b);
void buffer_write_data(buffer_t *b, const input_frame_t *f);
void buffer_write_end(buffer_t *b);

#endif /* NOBD_CORE_BUFFER_H */

#pragma once

#include <gst/gst.h>
#include <glib.h>

G_BEGIN_DECLS

/* One buffered frame */
typedef struct {
    GstBuffer *buffer;   /* ref-owned */
    gboolean   keyframe; /* TRUE if IDR / keyframe */
} PrebufferFrame;

/* Ring buffer */
typedef struct {
    GQueue *queue;       /* holds PrebufferFrame* */
    guint   max_frames;  /* capacity in frames */
} FrameRing;

/* Lifecycle */
void frame_ring_init(FrameRing *ring, guint max_frames);
void frame_ring_clear(FrameRing *ring);
void frame_ring_deinit(FrameRing *ring);

/* Operations */
void frame_ring_push(FrameRing *ring,
                     GstBuffer *buffer,
                     gboolean keyframe);

/*
 * Return a GList* pointing to the FIRST frame to flush,
 * starting from the LAST keyframe in the ring.
 *
 * - The list nodes belong to the ring (do NOT free).
 * - Returns NULL if no keyframe exists.
 */
GList *frame_ring_get_from_last_keyframe(FrameRing *ring);

G_END_DECLS

#include "frame_ring.h"

/* --------------------------------------------------
 * Internal helper
 * -------------------------------------------------- */
static void prebuffer_frame_free(PrebufferFrame *f)
{
    if (!f)
        return;

    if (f->buffer)
        gst_buffer_unref(f->buffer);

    g_free(f);
}

/* --------------------------------------------------
 * Public API
 * -------------------------------------------------- */
void frame_ring_init(FrameRing *ring, guint max_frames)
{
    g_return_if_fail(ring != NULL);

    ring->queue = g_queue_new();
    ring->max_frames = max_frames;
}

void frame_ring_clear(FrameRing *ring)
{
    g_return_if_fail(ring != NULL);
    g_return_if_fail(ring->queue != NULL);

    while (!g_queue_is_empty(ring->queue)) {
        PrebufferFrame *f =
            (PrebufferFrame *)g_queue_pop_head(ring->queue);
        prebuffer_frame_free(f);
    }
}

void frame_ring_deinit(FrameRing *ring)
{
    g_return_if_fail(ring != NULL);

    if (ring->queue) {
        frame_ring_clear(ring);
        g_queue_free(ring->queue);
        ring->queue = NULL;
    }
}

void frame_ring_push(FrameRing *ring,
                GstBuffer *buffer,
                gboolean keyframe)
{
    g_return_if_fail(ring != NULL);
    g_return_if_fail(ring->queue != NULL);
    g_return_if_fail(buffer != NULL);

    if (ring->max_frames == 0)
        return;

    /* Drop oldest if full */
    if (g_queue_get_length(ring->queue) >= ring->max_frames) {
        PrebufferFrame *old =
            (PrebufferFrame *)g_queue_pop_head(ring->queue);
        prebuffer_frame_free(old);
    }

    PrebufferFrame *f = g_new0(PrebufferFrame, 1);
    f->buffer = gst_buffer_ref(buffer);
    f->keyframe = keyframe;

    g_queue_push_tail(ring->queue, f);
}

GList * frame_ring_get_from_last_keyframe(FrameRing *ring)
{
    g_return_val_if_fail(ring != NULL, NULL);
    g_return_val_if_fail(ring->queue != NULL, NULL);

    if (g_queue_is_empty(ring->queue))
        return NULL;

    /* Walk backwards to find LAST keyframe */
    for (GList *l = g_queue_peek_tail_link(ring->queue);
         l != NULL;
         l = l->prev) {

        PrebufferFrame *f = (PrebufferFrame *)l->data;
        if (f->keyframe)
            return l; /* flush from here forward */
    }

    /* No keyframe → cannot safely decode */
    return NULL;
}

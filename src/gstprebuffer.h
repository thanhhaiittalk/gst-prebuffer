#ifndef __GST_PREBUFFER_H__
#define __GST_PREBUFFER_H__

#include <gst/gst.h>
#include "frame_ring.h"

G_BEGIN_DECLS

/* ------------------------------------------------------------------
 * Type macros
 * ------------------------------------------------------------------ */

#define GST_TYPE_PREBUFFER (gst_prebuffer_get_type())
#define GST_PREBUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PREBUFFER, GstPrebuffer))
#define GST_PREBUFFER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_PREBUFFER, GstPrebufferClass))
#define GST_IS_PREBUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_PREBUFFER))
#define GST_IS_PREBUFFER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_PREBUFFER))

/* ------------------------------------------------------------------
 * Mode enum
 * ------------------------------------------------------------------ */

typedef enum {
    PREBUFFER_MODE_DISABLED = 0,
    PREBUFFER_MODE_PRE_RECORD,
    PREBUFFER_MODE_RECORD
} PrebufferMode;

/* ------------------------------------------------------------------
 * Stream type (pad identification)
 * ------------------------------------------------------------------ */

typedef enum {
    PREBUFFER_STREAM_VIDEO = 0,
    PREBUFFER_STREAM_AUDIO
} PrebufferStreamType;

/* ------------------------------------------------------------------
 * Instance structure
 * ------------------------------------------------------------------ */

typedef struct _GstPrebuffer {
    GstElement parent;

    /* Pads */
    GstPad *video_sink;   /* request pad */
    GstPad *audio_sink;   /* request pad */
    GstPad *src;          /* always pad */

    guint video_pad_count;
    guint audio_pad_count;

    /* Thread safety */
    GMutex lock;

    /* Mode state */
    PrebufferMode mode;
    PrebufferMode pending_mode;

    gboolean flush_pending;

    /* Configuration */
    guint duration_sec;

    /* Sync / timing */
    GstClockTime record_start_pts;
    gboolean record_start_pts_valid;

    /* Ring buffers */
    FrameRing video_ring;
    FrameRing audio_ring;

    gboolean video_ring_inited;
    gboolean audio_ring_inited;


} GstPrebuffer;

/* ------------------------------------------------------------------
 * Class structure
 * ------------------------------------------------------------------ */

typedef struct _GstPrebufferClass {
    GstElementClass parent_class;
} GstPrebufferClass;

/* ------------------------------------------------------------------
 * GType
 * ------------------------------------------------------------------ */

#define PREBUFFER_TYPE_MODE (prebuffer_mode_get_type())

GType gst_prebuffer_get_type(void);
GType prebuffer_mode_get_type(void);

G_END_DECLS

#endif /* __GST_PREBUFFER_H__ */

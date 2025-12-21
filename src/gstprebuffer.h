#ifndef __GST_PREBUFFER_H__
#define __GST_PREBUFFER_H__

#include <gst/base/gstbasetransform.h>
#include "frame_ring.h"

G_BEGIN_DECLS

#define GST_TYPE_PREBUFFER (gst_prebuffer_get_type())
#define GST_PREBUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PREBUFFER, GstPrebuffer))

/* Enum definition */
typedef enum {
    PREBUFFER_MODE_DISABLED = 0,
    PREBUFFER_MODE_PRE_RECORD,
    PREBUFFER_MODE_RECORD
} PrebufferMode;

typedef struct _GstPrebuffer {
    GstBaseTransform parent;

    GMutex        lock;

    PrebufferMode mode;          // current mode
    PrebufferMode pending_mode;  // requested mode (optional but recommended)

    gboolean      flush_pending;   /* need to flush pre-record buffer */
    gboolean      buffering_enabled;

    guint         duration_sec;

    /* ring buffer state */
    FrameRing ring;

} GstPrebuffer;

typedef struct _GstPrebufferClass {
    GstBaseTransformClass parent_class;
} GstPrebufferClass;

/* GType for the enum */
#define PREBUFFER_TYPE_MODE (prebuffer_mode_get_type())
GType gst_prebuffer_get_type(void);

G_END_DECLS

#endif

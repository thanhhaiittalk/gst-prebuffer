#ifndef __GST_PREBUFFER_H__
#define __GST_PREBUFFER_H__

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_PREBUFFER (gst_prebuffer_get_type())
#define GST_PREBUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PREBUFFER, GstPrebuffer))

typedef struct _GstPrebuffer {
    GstBaseTransform parent;

    /* empty instance fields */
} GstPrebuffer;

typedef struct _GstPrebufferClass {
    GstBaseTransformClass parent_class;
} GstPrebufferClass;

GType gst_prebuffer_get_type(void);

G_END_DECLS

#endif

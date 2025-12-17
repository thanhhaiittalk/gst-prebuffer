#include "gstprebuffer.h"

/* Type registration */
G_DEFINE_TYPE(GstPrebuffer, gst_prebuffer, GST_TYPE_BASE_TRANSFORM)

/* Property enum */
enum {
    PROP_0,
    PROP_MODE,
    PROP_DURATION,
};


/* Register enum with GObject type system */
GType
prebuffer_mode_get_type(void)
{
    static GType mode_type = 0;

    if (g_once_init_enter(&mode_type)) {
        static const GEnumValue values[] = {
            { PREBUFFER_MODE_DISABLED,   "PREBUFFER_MODE_DISABLED",   "disabled" },
            { PREBUFFER_MODE_PRE_RECORD, "PREBUFFER_MODE_PRE_RECORD", "pre-record" },
            { PREBUFFER_MODE_RECORD,     "PREBUFFER_MODE_RECORD",     "record" },
            { 0, NULL, NULL }
        };

        GType t = g_enum_register_static("PrebufferMode", values);
        g_once_init_leave(&mode_type, t);
    }

    return mode_type;
}

/* Forward declarations */
static void gst_prebuffer_set_property(GObject *object,
                                       guint prop_id,
                                       const GValue *value,
                                       GParamSpec *pspec);

static void gst_prebuffer_get_property(GObject *object,
                                       guint prop_id,
                                       GValue *value,
                                       GParamSpec *pspec);

static gboolean gst_prebuffer_start(GstBaseTransform *trans);
static gboolean gst_prebuffer_stop (GstBaseTransform *trans);

static GstFlowReturn gst_prebuffer_transform_ip(GstBaseTransform *trans,
                                                GstBuffer *buf);

/* -------------------------- Class Init -------------------------- */
static void
gst_prebuffer_class_init(GstPrebufferClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    /* ---------- GObject ---------- */
    gobject_class->set_property = gst_prebuffer_set_property;
    gobject_class->get_property = gst_prebuffer_get_property;

    /* ---------- Properties ---------- */

    g_object_class_install_property(
        gobject_class,
        PROP_MODE,
        g_param_spec_enum(
            "mode",
            "Mode",
            "Prebuffer operating mode",
            PREBUFFER_TYPE_MODE,
            PREBUFFER_MODE_PRE_RECORD, /* default */
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        )
    );

    g_object_class_install_property(
        gobject_class,
        PROP_DURATION,
        g_param_spec_uint(
            "duration",
            "Duration",
            "Pre-record duration in seconds",
            0,        /* min */
            G_MAXUINT,/* max */
            30,       /* default */
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        )
    );

    /* ---------- GstElement ---------- */

    gst_element_class_set_static_metadata(
        element_class,
        "Prebuffer",
        "Filter/Video",
        "Pre-record buffer element",
        "Your Name <you@example.com>"
    );

    /* ---------- GstBaseTransform ---------- */

    /* We decide per-buffer whether to drop or forward */
    base_transform_class->transform_ip = gst_prebuffer_transform_ip;

    /* We are not a pure passthrough element */
    base_transform_class->passthrough_on_same_caps = FALSE;
}


/* -------------------------- Instance Init -------------------------- */
static void gst_prebuffer_init(GstPrebuffer *self)
{
    /* empty */
}

/* -------------------------- set_property -------------------------- */
static void gst_prebuffer_set_property(GObject *object,
                                       guint prop_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
    GstPrebuffer *self = GST_PREBUFFER(object);

    switch (prop_id) {

    case PROP_MODE: {
        PrebufferMode new_mode = g_value_get_enum(value);

        g_mutex_lock(&self->lock);

        if (self->mode != new_mode) {
            self->pending_mode = new_mode;
            /* Actual transition is handled in streaming thread */
        }

        g_mutex_unlock(&self->lock);
        break;
    }

    case PROP_DURATION: {
        guint new_duration = g_value_get_uint(value);

        g_mutex_lock(&self->lock);
        self->duration_sec = new_duration;
        /* buffer resize can be lazy */
        g_mutex_unlock(&self->lock);
        break;
    }

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* -------------------------- get_property -------------------------- */
static void gst_prebuffer_get_property(GObject *object,
                                       guint prop_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
    /* empty */
}

/* -------------------------- start() -------------------------- */
static gboolean gst_prebuffer_start(GstBaseTransform *trans)
{
    /* empty */
    return TRUE;
}

/* -------------------------- stop() -------------------------- */
static gboolean gst_prebuffer_stop(GstBaseTransform *trans)
{
    /* empty */
    return TRUE;
}

static void gst_prebuffer_apply_mode_transition(GstPrebuffer *self,
                                    PrebufferMode old_mode,
                                    PrebufferMode new_mode)
{
    /* PRE_RECORD → RECORD */
    if (old_mode == PREBUFFER_MODE_PRE_RECORD &&
        new_mode == PREBUFFER_MODE_RECORD) {
        /* mark flush_pending = TRUE */
        /* next buffers will flush ring buffer */
    }

    /* RECORD → PRE_RECORD */
    if (old_mode == PREBUFFER_MODE_RECORD &&
        new_mode == PREBUFFER_MODE_PRE_RECORD) {
        /* send EOS downstream if needed */
        /* clear buffer */
    }

    /* any → DISABLED */
    if (new_mode == PREBUFFER_MODE_DISABLED) {
        /* clear buffer */
        /* disable buffering */
    }
}

/* -------------------------- transform_ip() -------------------------- */
static GstFlowReturn gst_prebuffer_transform_ip(GstBaseTransform *base,
                           GstBuffer        *buf)
{
    GstPrebuffer *self = GST_PREBUFFER(base);

    /* --------------------------------------------------
     * 1. Handle pending mode transitions (streaming thread)
     * -------------------------------------------------- */
    g_mutex_lock(&self->lock);

    if (self->pending_mode != self->mode) {
        gst_prebuffer_apply_mode_transition(self,
                                             self->mode,
                                             self->pending_mode);
        self->mode = self->pending_mode;
    }

    /* Snapshot current mode for this buffer */
    PrebufferMode mode = self->mode;

    g_mutex_unlock(&self->lock);

    /* --------------------------------------------------
     * 2. Per-buffer data path decision
     * -------------------------------------------------- */
    switch (mode) {

    case PREBUFFER_MODE_DISABLED:
        /*
         * Passthrough mode
         * - Do nothing
         * - Forward buffer downstream
         */
        return GST_FLOW_OK;

    case PREBUFFER_MODE_PRE_RECORD:
        /*
         * Pre-record mode
         * - Store this buffer in internal ring buffer
         * - Track timestamps and keyframes
         * - Enforce duration window
         * - Do NOT forward downstream
         */
        /* TODO: store buffer */
        return GST_BASE_TRANSFORM_FLOW_DROPPED;

    case PREBUFFER_MODE_RECORD:
        /*
         * Record mode
         * - Pre-record buffer already flushed during transition
         * - Forward all incoming buffers downstream
         * - No buffering here
         */
        return GST_FLOW_OK;
    }

    /* Should never reach here */
    return GST_FLOW_OK;
}

/* -------------------------- Plugin init -------------------------- */
static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin,
                                "prebuffer",
                                GST_RANK_NONE,
                                GST_TYPE_PREBUFFER);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    prebuffer,
    "Empty prebuffer plugin",
    plugin_init,
    "1.0",
    "LGPL",
    "prebuffer",
    "https://example.com"
)

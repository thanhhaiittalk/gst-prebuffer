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
GType prebuffer_mode_get_type(void)
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
static void gst_prebuffer_finalize(GObject *object);
static gboolean gst_prebuffer_set_caps(GstBaseTransform *trans,
                       GstCaps *incaps,
                       GstCaps *outcaps);


static void gst_prebuffer_finalize(GObject *object)
{
    GstPrebuffer *self = GST_PREBUFFER(object);

    frame_ring_deinit(&self->ring);
    g_mutex_clear(&self->lock);

    G_OBJECT_CLASS(gst_prebuffer_parent_class)->finalize(object);
}

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);
/* -------------------------- Class Init -------------------------- */
static void gst_prebuffer_class_init(GstPrebufferClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    /* ---------- GObject ---------- */
    gobject_class->set_property = gst_prebuffer_set_property;
    gobject_class->get_property = gst_prebuffer_get_property;
    gobject_class->finalize = gst_prebuffer_finalize;
    
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

    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&sink_template));

    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&src_template));

    /* ---------- GstBaseTransform ---------- */

    /* We decide per-buffer whether to drop or forward */
    base_transform_class->transform_ip = gst_prebuffer_transform_ip;
    base_transform_class->start = gst_prebuffer_start;
    base_transform_class->stop  = gst_prebuffer_stop;
    base_transform_class->set_caps = gst_prebuffer_set_caps;

    /* We are not a pure passthrough element */
    base_transform_class->passthrough_on_same_caps = FALSE;
}

static gboolean gst_prebuffer_set_caps(GstBaseTransform *trans,
                       GstCaps *incaps,
                       GstCaps *outcaps)
{
    GstPrebuffer *self = GST_PREBUFFER(trans);
    GstStructure *s;
    gint num, den;
    s = gst_caps_get_structure(incaps, 0);

    if (gst_structure_get_fraction(s, "framerate", &num, &den)) {

        if (den > 0) {
            g_mutex_lock(&self->lock);

            self->fps = (gdouble)num / (gdouble)den;

            /* duration_sec already set */
            guint max_frames =
                (guint)(self->duration_sec * self->fps);

            if (max_frames == 0)
                max_frames = 1;

            frame_ring_deinit(&self->ring);
            frame_ring_init(&self->ring, max_frames);
            g_mutex_unlock(&self->lock);
        }
    }
    
    return TRUE;
}
/* -------------------------- Instance Init -------------------------- */
static void gst_prebuffer_init(GstPrebuffer *self)
{
    g_mutex_init(&self->lock);

    self->mode = PREBUFFER_MODE_PRE_RECORD;
    self->pending_mode = PREBUFFER_MODE_PRE_RECORD;

    self->duration_sec = 30;
    self->fps = 30.0; 

    self->flush_pending = FALSE;
    self->buffering_enabled = TRUE;
    /* SAFE DEFAULT INIT */
    frame_ring_init(&self->ring,
                    self->duration_sec * self->fps);

    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
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
    GstPrebuffer *self = GST_PREBUFFER(object);

    switch (prop_id) {

    case PROP_MODE:
        g_value_set_enum(value, self->mode);
        break;

    case PROP_DURATION:
        g_value_set_uint(value, self->duration_sec);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* -------------------------- start() -------------------------- */
static gboolean gst_prebuffer_start(GstBaseTransform *trans)
{
    GstPrebuffer *self = GST_PREBUFFER(trans);

    g_mutex_lock(&self->lock);

    self->flush_pending = FALSE;
    self->buffering_enabled = (self->mode == PREBUFFER_MODE_PRE_RECORD);

    frame_ring_clear(&self->ring);

    g_mutex_unlock(&self->lock);

    return TRUE;
}

/* -------------------------- stop() -------------------------- */
static gboolean gst_prebuffer_stop(GstBaseTransform *trans)
{
    GstPrebuffer *self = GST_PREBUFFER(trans);

    g_mutex_lock(&self->lock);

    frame_ring_clear(&self->ring);

    self->flush_pending = FALSE;
    self->buffering_enabled = FALSE;

    g_mutex_unlock(&self->lock);

    return TRUE;
}

static void gst_prebuffer_apply_mode_transition(GstPrebuffer *self,
                                    PrebufferMode old_mode,
                                    PrebufferMode new_mode)
{
    /* No-op safety */
    if (old_mode == new_mode)
        return;

    /* --------------------------------------------------
     * PRE_RECORD → RECORD
     * -------------------------------------------------- */
    if (old_mode == PREBUFFER_MODE_PRE_RECORD &&
        new_mode == PREBUFFER_MODE_RECORD) {

        /*
         * We are starting a recording.
         *
         * Do NOT flush buffers here.
         * Just mark intent — actual flush happens
         * incrementally in transform_ip().
         */
        self->flush_pending = TRUE;
        self->buffering_enabled = FALSE;

        return;
    }

    /* --------------------------------------------------
     * RECORD → PRE_RECORD
     * -------------------------------------------------- */
    if (old_mode == PREBUFFER_MODE_RECORD &&
        new_mode == PREBUFFER_MODE_PRE_RECORD) {

        /*
         * Recording stopped.
         *
         * One-time actions only:
         * - finalize downstream (EOS or equivalent)
         * - reset internal state
         */

        self->send_eos = TRUE;

        /* Reset buffer state */
        /* TODO: clear ring buffer */
        frame_ring_clear(&self->ring);

        self->flush_pending = FALSE;
        self->buffering_enabled = TRUE;

        return;
    }

    /* --------------------------------------------------
     * ANY → DISABLED
     * -------------------------------------------------- */
    if (new_mode == PREBUFFER_MODE_DISABLED) {

        /*
         * Full bypass mode.
         *
         * - No buffering
         * - No flushing
         * - Passthrough only
         */

        /* Drop all prebuffered frames */
        frame_ring_clear(&self->ring);

        self->flush_pending = FALSE;
        self->buffering_enabled = FALSE;

        return;
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
    PrebufferMode cur_mode;
    g_mutex_lock(&self->lock);
    cur_mode = self->mode; // Old mode
    if (self->pending_mode != self->mode) {
        gst_prebuffer_apply_mode_transition(self,
                                             self->mode,
                                             self->pending_mode);
        self->mode = self->pending_mode;
    }

    g_mutex_unlock(&self->lock);

    /* --------------------------------------------------
     * 2. Per-buffer data path decision
     * -------------------------------------------------- */
    switch (cur_mode)
    {

    case PREBUFFER_MODE_DISABLED:
        /*
         * Passthrough mode
         *
         * - Prebuffer fully disabled
         * - No buffering
         * - No flushing
         * - Just forward buffer downstream
         */
        return GST_FLOW_OK;

    case PREBUFFER_MODE_PRE_RECORD:
        /*
         * Pre-record mode
         *
         * - Continuously buffer encoded frames
         * - Maintain rolling window (duration-based)
         * - Track keyframes
         * - Downstream must see nothing
         */

        if (self->buffering_enabled)
        {
            gboolean keyframe =
                !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
            frame_ring_push(&self->ring, buf, keyframe);
        }

        /* Consume buffer, do NOT forward */
        return GST_BASE_TRANSFORM_FLOW_DROPPED;

    case PREBUFFER_MODE_RECORD:
        /*
         * Record mode
         *
         * - If flush_pending is set:
         *     flush pre-record buffers FIRST
         *     (from nearest preceding keyframe)
         *
         * - Then forward live buffers normally
         */

        if (self->flush_pending) {
            GList *start =
                frame_ring_get_from_last_keyframe(&self->ring);

            if (start)
            {
                for (GList *l = start; l; l = l->next)
                {
                    PrebufferFrame *f = l->data;
                    gst_pad_push(
                        GST_BASE_TRANSFORM_SRC_PAD(base),
                        gst_buffer_ref(f->buffer));
                }
            }

            frame_ring_clear(&self->ring);
            self->flush_pending = FALSE;
        }

        if (self->send_eos)
        {
            gst_pad_push_event(
                GST_BASE_TRANSFORM_SRC_PAD(base),
                gst_event_new_eos());
            self->send_eos = FALSE;
        }


    return GST_FLOW_OK;
    }
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

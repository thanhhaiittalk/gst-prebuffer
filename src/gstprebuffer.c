#include "gstprebuffer.h"

/* Type registration */
G_DEFINE_TYPE(GstPrebuffer, gst_prebuffer, GST_TYPE_ELEMENT)

/* Property enum */
enum {
    PROP_0,
    PROP_MODE,
    PROP_DURATION,
};

/* ============================================================
 * Pad templates
 * ============================================================ */

/* Request sink pads: audio + video */
static GstStaticPadTemplate audio_sink_template =
GST_STATIC_PAD_TEMPLATE(
    "audio_sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS("audio/mpeg, mpegversion=(int)4")
);


static GstStaticPadTemplate video_sink_template =
GST_STATIC_PAD_TEMPLATE(
    "video_sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS("video/x-h264")
);


/* Single src pad */
static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

/* ============================================================
 * Forward declarations
 * ============================================================ */
static GstPad *gst_prebuffer_request_new_pad(GstElement *element,
                                             GstPadTemplate *templ,
                                             const gchar *name,
                                             const GstCaps *caps);

static void gst_prebuffer_release_pad(GstElement *element, GstPad *pad);

static GstFlowReturn gst_prebuffer_chain(GstPad *pad,
                                        GstObject *parent,
                                        GstBuffer *buf);

static gboolean gst_prebuffer_event(GstPad *pad,
                                    GstObject *parent,
                                    GstEvent *event);

static void gst_prebuffer_set_property(GObject *object,
                                       guint prop_id,
                                       const GValue *value,
                                       GParamSpec *pspec);

static void gst_prebuffer_get_property(GObject *object,
                                       guint prop_id,
                                       GValue *value,
                                       GParamSpec *pspec);

static void gst_prebuffer_finalize(GObject *object);

static GstFlowReturn gst_prebuffer_video_chain(GstPad *pad,
                          GstObject *parent,
                          GstBuffer *buf);

static GstFlowReturn gst_prebuffer_audio_chain(GstPad *pad,
                          GstObject *parent,
                          GstBuffer *buf);

static GstStateChangeReturn gst_prebuffer_change_state(GstElement *element,
                           GstStateChange transition);

static gboolean gst_prebuffer_sink_event(GstPad *pad,
                            GstObject *parent,
                            GstEvent *event);

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

/* ============================================================
 * Class init
 * ============================================================ */
static void gst_prebuffer_class_init(GstPrebufferClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_prebuffer_set_property;
    gobject_class->get_property = gst_prebuffer_get_property;
    gobject_class->finalize     = gst_prebuffer_finalize;

    /* Properties */
    g_object_class_install_property(
        gobject_class,
        PROP_MODE,
        g_param_spec_enum(
            "mode", "Mode", "Prebuffer mode",
            PREBUFFER_TYPE_MODE,
            PREBUFFER_MODE_PRE_RECORD,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        PROP_DURATION,
        g_param_spec_uint(
            "duration", "Duration",
            "Prebuffer duration (seconds)",
            0, G_MAXUINT, 30,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /* Metadata */
    gst_element_class_set_static_metadata(
        element_class,
        "Prebuffer",
        "Filter",
        "Audio/Video pre-record buffer",
        "You");

    /* Pads */
    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get(&video_sink_template));
    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get(&audio_sink_template));

    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&src_template));

    element_class->request_new_pad = gst_prebuffer_request_new_pad;
    element_class->release_pad     = gst_prebuffer_release_pad;
    element_class->change_state = gst_prebuffer_change_state;
}

/* ============================================================
 * Instance init
 * ============================================================ */
static void gst_prebuffer_init(GstPrebuffer *self)
{
    g_mutex_init(&self->lock);

    self->mode         = PREBUFFER_MODE_PRE_RECORD;
    self->pending_mode = PREBUFFER_MODE_PRE_RECORD;

    self->duration_sec = 30;

    self->flush_pending = FALSE;

    self->record_start_pts = GST_CLOCK_TIME_NONE;
    self->record_start_pts_valid = FALSE;

    self->video_pad_count = 0;
    self->audio_pad_count = 0;

    // frame_ring_init(&self->video_ring, 300);
    // frame_ring_init(&self->audio_ring, 300);
    self->video_ring_inited = FALSE;
    self->audio_ring_inited = FALSE;

    /* src pad */
    self->src = gst_pad_new_from_static_template(&src_template, "src");
    gst_pad_set_event_function(self->src, gst_prebuffer_event);
    gst_element_add_pad(GST_ELEMENT(self), self->src);
}

/* ============================================================
 * Pad creation
 * ============================================================ */
static GstPad * gst_prebuffer_request_new_pad(GstElement *element,
                              GstPadTemplate *templ,
                              const gchar *name,
                              const GstCaps *caps)
{
    GstPrebuffer *self = GST_PREBUFFER(element);
    GstPad *pad;
    gchar *pad_name = NULL;

    g_mutex_lock(&self->lock);

    if (templ == gst_element_class_get_pad_template(
                    GST_ELEMENT_CLASS(G_OBJECT_GET_CLASS(element)),
                    "video_sink_%u")) {

        /* Enforce max 1 video pad if desired */
        if (self->video_sink) {
            g_mutex_unlock(&self->lock);
            return NULL;
        }

        pad_name = g_strdup_printf("video_sink_%u",
                                   self->video_pad_count++);

        pad = gst_pad_new_from_template(templ, pad_name);
        gst_pad_set_chain_function(pad, gst_prebuffer_video_chain);
        self->video_sink = pad;

    } else {

        /* Enforce max 1 audio pad if desired */
        if (self->audio_sink) {
            g_mutex_unlock(&self->lock);
            return NULL;
        }

        pad_name = g_strdup_printf("audio_sink_%u",
                                   self->audio_pad_count++);

        pad = gst_pad_new_from_template(templ, pad_name);

        gst_pad_set_chain_function(pad, gst_prebuffer_audio_chain);
        gst_pad_set_event_function(pad, gst_prebuffer_sink_event);
        
        self->audio_sink = pad;
    }

    gst_pad_set_event_function(pad, gst_prebuffer_event);
    gst_element_add_pad(element, pad);

    g_free(pad_name);
    g_mutex_unlock(&self->lock);

    return pad;
}


static void gst_prebuffer_release_pad(GstElement *element, GstPad *pad)
{
    GstPrebuffer *self = GST_PREBUFFER(element);

    g_mutex_lock(&self->lock);

    if (pad == self->video_sink)
        self->video_sink = NULL;
    else if (pad == self->audio_sink)
        self->audio_sink = NULL;

    g_mutex_unlock(&self->lock);

    gst_element_remove_pad(element, pad);
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

/* ============================================================
 * Handle change state
 * ============================================================ */
static GstStateChangeReturn gst_prebuffer_change_state(GstElement *element,
                           GstStateChange transition)
{
    GstPrebuffer *self = GST_PREBUFFER(element);
    GstStateChangeReturn ret;

    /* -------- BEFORE parent change_state -------- */
    if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
        /* equivalent of start() */
        frame_ring_clear(&self->video_ring);
        frame_ring_clear(&self->audio_ring);
        self->record_start_pts_valid = FALSE;
    }

    /* Let parent do the real work */
    ret = ret = GST_ELEMENT_CLASS(gst_prebuffer_parent_class)
          ->change_state(element, transition);

    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    /* -------- AFTER parent change_state -------- */
    if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
        /* equivalent of stop() */
        frame_ring_clear(&self->video_ring);
        frame_ring_clear(&self->audio_ring);
    }

    return ret;
}

/* ============================================================
 * Event handling
 * ============================================================ */
static gboolean gst_prebuffer_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    GstPrebuffer *self = GST_PREBUFFER(parent);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_EOS:
        return gst_pad_push_event(self->src, event);
    default:
        return gst_pad_push_event(self->src, event);
    }
}

/* ============================================================
 * Finalize
 * ============================================================ */
static void gst_prebuffer_finalize(GObject *object)
{
    GstPrebuffer *self = GST_PREBUFFER(object);

    frame_ring_deinit(&self->video_ring);
    frame_ring_deinit(&self->audio_ring);
    g_mutex_clear(&self->lock);

    G_OBJECT_CLASS(gst_prebuffer_parent_class)->finalize(object);
}

static void gst_prebuffer_apply_mode_transition(GstPrebuffer *self,
                                                PrebufferMode old_mode,
                                                PrebufferMode new_mode)
{
    if (old_mode == new_mode)
        return;

    /* PRE_RECORD → RECORD */
    if (old_mode == PREBUFFER_MODE_PRE_RECORD &&
        new_mode == PREBUFFER_MODE_RECORD) {

        self->flush_pending = TRUE;
        return;
    }

    /* RECORD → PRE_RECORD */
    if (old_mode == PREBUFFER_MODE_RECORD &&
        new_mode == PREBUFFER_MODE_PRE_RECORD) {

        frame_ring_clear(&self->video_ring);
        frame_ring_clear(&self->audio_ring);

        self->flush_pending = FALSE;

        return;
    }

    /* ANY → DISABLED */
    if (new_mode == PREBUFFER_MODE_DISABLED) {

        frame_ring_clear(&self->video_ring);
        frame_ring_clear(&self->audio_ring);

        self->flush_pending = FALSE;

        return;
    }
}

/* ============================================================
 * Handle Video frame
 * ============================================================ */
static GstFlowReturn gst_prebuffer_video_chain(GstPad *pad,
                           GstObject *parent,
                           GstBuffer *buf)
{
    GstPrebuffer *self = GST_PREBUFFER(parent);

    GList *flush_list = NULL;
    gboolean do_flush = FALSE;
    GstClockTime start_pts = GST_CLOCK_TIME_NONE;

    /* ================= LOCK ================= */
    g_mutex_lock(&self->lock);

    /* Apply pending mode transition (VIDEO ONLY) */
    if (self->pending_mode != self->mode) {
        gst_prebuffer_apply_mode_transition(self,
                                            self->mode,
                                            self->pending_mode);
        self->mode = self->pending_mode;
    }


    /* Track Keyframes */
    gboolean is_keyframe = !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);

    switch (self->mode) {

    case PREBUFFER_MODE_PRE_RECORD:
        /* Buffer video */
        frame_ring_push(&self->video_ring, buf, is_keyframe);
        g_mutex_unlock(&self->lock);
        return GST_FLOW_OK;

    case PREBUFFER_MODE_RECORD:
        /* First RECORD buffer triggers flush */
        if (self->flush_pending) {

            /* Find keyframe start */
            GList *start =
                frame_ring_get_from_last_keyframe(&self->video_ring);

            if (start) {
                PrebufferFrame *kf = start->data;

                /* Define global A/V start time */
                start_pts = GST_BUFFER_PTS(kf->buffer);
                self->record_start_pts = start_pts;
                self->record_start_pts_valid = TRUE;

                /* Copy list for flushing OUTSIDE lock */
                flush_list = g_list_copy(start);
                do_flush = TRUE;
            }

            frame_ring_clear(&self->video_ring);
            self->flush_pending = FALSE;
        }

        g_mutex_unlock(&self->lock);
        /* ================= UNLOCK ================= */

        /* Flush buffered video (no lock held) */
        if (do_flush) {
            for (GList *l = flush_list; l; l = l->next) {
                PrebufferFrame *f = l->data;
                gst_pad_push(self->src,
                             gst_buffer_ref(f->buffer));
            }
            g_list_free(flush_list);

            /* Ensure continuity */
            GST_BUFFER_FLAG_UNSET(buf, GST_BUFFER_FLAG_DISCONT);
        }

        /* Passthrough live video */
        return gst_pad_push(self->src, buf);

    case PREBUFFER_MODE_DISABLED:
        g_mutex_unlock(&self->lock);
        /* Full passthrough */
        return gst_pad_push(self->src, buf);
    }

    g_mutex_unlock(&self->lock);
    return GST_FLOW_OK;
}


/* ============================================================
 * Handle Audio frame
 * ============================================================ */
static GstFlowReturn gst_prebuffer_audio_chain(GstPad *pad,
                           GstObject *parent,
                           GstBuffer *buf)
{
    GstPrebuffer *self = GST_PREBUFFER(parent);

    GstClockTime pts = GST_BUFFER_PTS(buf);
    GstClockTime start_pts = GST_CLOCK_TIME_NONE;
    gboolean start_valid = FALSE;

    GList *flush_list = NULL;
    gboolean do_flush = FALSE;

    /* ================= LOCK ================= */
    g_mutex_lock(&self->lock);

    switch (self->mode) {

    case PREBUFFER_MODE_PRE_RECORD:
        /* Buffer audio */
        frame_ring_push(&self->audio_ring, buf, TRUE);
        g_mutex_unlock(&self->lock);
        return GST_FLOW_OK;

    case PREBUFFER_MODE_RECORD:

        start_valid = self->record_start_pts_valid;
        start_pts   = self->record_start_pts;

        /* Video has not defined start yet → keep buffering */
        if (!start_valid) {
            frame_ring_push(&self->audio_ring, buf, TRUE);
            g_mutex_unlock(&self->lock);
            return GST_FLOW_OK;
        }

        /* Drop early audio */
        if (pts < start_pts) {
            g_mutex_unlock(&self->lock);
            gst_buffer_unref(buf);
            return GST_FLOW_OK;
        }

        /*
         * First valid audio buffer:
         * flush buffered audio ≥ record_start_pts
         */
        GList *head = g_queue_peek_head_link(self->audio_ring.queue);
        if (head)
        {
            flush_list = g_list_copy(head);
            frame_ring_clear(&self->audio_ring);
            do_flush = TRUE;
        }

        frame_ring_clear(&self->audio_ring);
        do_flush = TRUE;

        g_mutex_unlock(&self->lock);
        /* ================= UNLOCK ================= */

        /* Flush buffered audio (no lock held) */
        if (do_flush) {
            for (GList *l = flush_list; l; l = l->next) {
                PrebufferFrame *f = l->data;
                if (GST_BUFFER_PTS(f->buffer) >= start_pts) {
                    gst_pad_push(self->src,
                                 gst_buffer_ref(f->buffer));
                }
            }
            g_list_free(flush_list);
        }

        /* Passthrough current audio */
        return gst_pad_push(self->src, buf);

    case PREBUFFER_MODE_DISABLED:
        g_mutex_unlock(&self->lock);
        /* Full passthrough */
        return gst_pad_push(self->src, buf);
    }

    g_mutex_unlock(&self->lock);
    return GST_FLOW_OK;
}

static gboolean gst_prebuffer_sink_event(GstPad *pad,
                         GstObject *parent,
                         GstEvent *event)
{
    GstPrebuffer *self = GST_PREBUFFER(parent);

    if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {

        GstCaps *caps;
        gst_event_parse_caps(event, &caps);

        GstStructure *s = gst_caps_get_structure(caps, 0);
        const gchar *name = gst_structure_get_name(s);

        g_mutex_lock(&self->lock);

        /* ================= VIDEO ================= */
        if (g_str_has_prefix(name, "video/") &&
            !self->video_ring_inited) {

            gint fps_n = 0, fps_d = 1;
            guint video_frames = 0;

            if (gst_structure_get_fraction(s, "framerate",
                                           &fps_n, &fps_d) &&
                fps_d > 0) {

                guint fps = fps_n / fps_d;
                video_frames = fps * self->duration_sec;
            } else {
                /* fallback */
                video_frames = 30 * self->duration_sec;
            }

            frame_ring_init(&self->video_ring, video_frames);
            self->video_ring_inited = TRUE;
        }

        /* ================= AUDIO ================= */
        else if (g_str_has_prefix(name, "audio/") &&
                 !self->audio_ring_inited) {

            gint rate = 0;
            guint audio_samples = 0;

            if (gst_structure_get_int(s, "rate", &rate) && rate > 0) {
                audio_samples = rate * self->duration_sec;
            } else {
                /* fallback: 48 kHz */
                audio_samples = 48000 * self->duration_sec;;
            }

            frame_ring_init(&self->audio_ring, audio_samples);
            self->audio_ring_inited = TRUE;
        }

        g_mutex_unlock(&self->lock);
    }

    /* Always forward the event */
    return gst_pad_push_event(self->src, event);
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
    "https://github.com/thanhhaiittalk/gst-prebuffer"
)
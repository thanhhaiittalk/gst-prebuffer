#include "gstprebuffer.h"

/* Type registration */
G_DEFINE_TYPE(GstPrebuffer, gst_prebuffer, GST_TYPE_BASE_TRANSFORM)

/* Property enum */
enum {
    PROP_0,
    PROP_DURATION,
    PROP_RECORD,
};

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
static void gst_prebuffer_class_init(GstPrebufferClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS(klass);

    /* properties (empty handlers) */
    gobject_class->set_property = gst_prebuffer_set_property;
    gobject_class->get_property = gst_prebuffer_get_property;

    g_object_class_install_property(
        gobject_class,
        PROP_DURATION,
        g_param_spec_uint("duration", "Duration",
                          "Empty placeholder",
                          0, 3600, 30,
                          G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class,
        PROP_RECORD,
        g_param_spec_boolean("record", "Record",
                             "Empty placeholder",
                             FALSE,
                             G_PARAM_READWRITE));

    /* Transform methods */
    trans_class->start        = gst_prebuffer_start;
    trans_class->stop         = gst_prebuffer_stop;
    trans_class->transform_ip = gst_prebuffer_transform_ip;

    /* pad templates (generic ANY caps) */
    GstCaps *caps = gst_caps_new_any();
    gst_element_class_add_pad_template(
        GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(
        GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
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
    /* empty */
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

/* -------------------------- transform_ip() -------------------------- */
static GstFlowReturn gst_prebuffer_transform_ip(GstBaseTransform *trans,
                                                GstBuffer *buf)
{
    /* empty */
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

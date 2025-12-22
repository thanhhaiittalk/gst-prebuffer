#include <gst/gst.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    GstElement *pipeline = gst_parse_launch(
        "videotestsrc is-live=true "
        "! x264enc tune=zerolatency key-int-max=30 "
        "! h264parse "
        "! prebuffer name=pb mode=pre-record duration=10 "
        "! mp4mux "
        "! filesink location=out.mp4",
        NULL);
    GstElement *pb = gst_bin_get_by_name(GST_BIN(pipeline), "pb");

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_print("=== PRE-RECORD ===\n");
    sleep(15);  // pre-record buffer

    g_print("=== SWITCH TO RECORD ===\n");
    g_object_set(pb, "mode", 2 /* PREBUFFER_MODE_RECORD */, NULL);

    sleep(2);  // actual recording

    g_print("=== STOP RECORD ===\n");
    g_object_set(pb, "mode", 1 /* PREBUFFER_MODE_PRE_RECORD */, NULL);

    sleep(1);

    gst_element_set_state(pipeline, GST_STATE_NULL);

    gst_object_unref(pb);
    gst_object_unref(pipeline);
    return 0;
}

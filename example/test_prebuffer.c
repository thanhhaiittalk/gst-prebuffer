#include <gst/gst.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/*
 * Test application for GstPrebuffer element
 *
 * Usage:
 *   ./test_prebuffer basic
 *   ./test_prebuffer disabled
 *   ./test_prebuffer short
 *   ./test_prebuffer nokey
 *
 * All outputs are MP4 files in current directory.
 */

static GstElement *pipeline = NULL;
static GstElement *prebuffer = NULL;

/* -------------------------------------------------- */
/* Helpers                                            */
/* -------------------------------------------------- */

static void build_pipeline(const char *outfile, int keyint)
{
    gchar desc[1024];

    snprintf(desc, sizeof(desc),
       "videotestsrc is-live=true pattern=ball "
        /* Force Full HD and 30 FPS here: */
        "! video/x-raw,width=720,height=540,framerate=30/1 "
       /* CHANGE 1: force 'aud' (Access Unit Delimiter) to help player find boundaries */
        "! x264enc tune=zerolatency key-int-max=%d speed-preset=ultrafast aud=true "
        /* CHANGE 2: config-interval=-1 forces SPS/PPS headers at every Keyframe */
        "! h264parse config-interval=-1 "
        "! prebuffer name=pb mode=pre-record duration=30 "
        "! mp4mux "
        "! filesink location=%s",
        keyint, outfile);

    pipeline = gst_parse_launch(desc, NULL);
    prebuffer = gst_bin_get_by_name(GST_BIN(pipeline), "pb");
}

static void start_pipeline(const char *label)
{
    g_print("\n=== %s ===\n", label);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

static void stop_pipeline(void)
{
   g_print("-> Stopping... Sending EOS to pipeline.\n");

    /* 1. Send the EOS event to the pipeline source */
    /* This tells the elements "The stream is finishing", so mp4mux can write the header. */
    gst_element_send_event(pipeline, gst_event_new_eos());

    /* 2. Wait for the pipeline to finish processing the EOS */
    GstBus *bus = gst_element_get_bus(pipeline);
    
    /* We wait up to 2 seconds. This block ensures the application doesn't 
     * quit before mp4mux has finished writing the file to disk. */
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, 
        2 * GST_SECOND, 
        GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

    if (msg) {
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);
            g_print("Error received: %s\n", err->message);
            g_error_free(err);
            g_free(debug);
        } else {
            g_print("EOS Received. File finalized successfully.\n");
        }
        gst_message_unref(msg);
    } else {
        g_print("Warning: EOS timeout! File might be incomplete (moov atom missing).\n");
    }

    gst_object_unref(bus);

    /* 3. NOW it is safe to tear down the pipeline */
    gst_element_set_state(pipeline, GST_STATE_NULL);
}

static void cleanup(void)
{
    if (prebuffer)
        gst_object_unref(prebuffer);
    if (pipeline)
        gst_object_unref(pipeline);

    prebuffer = NULL;
    pipeline  = NULL;
}

/* -------------------------------------------------- */
/* Test cases                                         */
/* -------------------------------------------------- */

/*
 * Test 1: Normal pre-record + record
 * Expect duration ~= 10s (5s prebuffer + 5s record)
 */
static void test_basic(void)
{
    build_pipeline("out_basic.mp4", 30);

    start_pipeline("TEST BASIC: PRE-RECORD");
    sleep(10);

    g_print("-> RECORD START\n");
    g_object_set(prebuffer, "mode", 2 /* RECORD */, NULL);
    sleep(15);

    g_print("-> RECORD STOP\n");
    g_object_set(prebuffer, "mode", 1 /* PRE_RECORD */, NULL);
    sleep(1);

    stop_pipeline();
    cleanup();
}

/*
 * Test 2: Disabled mode passthrough
 * Expect duration ~= 5s, no prebuffer effect
 */
static void test_disabled(void)
{
    build_pipeline("out_disabled.mp4", 30);

    g_object_set(prebuffer, "mode", 0 /* DISABLED */, NULL);

    start_pipeline("TEST DISABLED: PASSTHROUGH");
    sleep(5);

    stop_pipeline();
    cleanup();
}

/*
 * Test 3: Short record window
 * Expect duration ~= 6s (5s prebuffer + 1s record)
 */
static void test_short(void)
{
    build_pipeline("out_short.mp4", 30);

    start_pipeline("TEST SHORT RECORD");
    sleep(5);

    g_print("-> RECORD START\n");
    g_object_set(prebuffer, "mode", 2 /* RECORD */, NULL);
    sleep(1);

    g_print("-> RECORD STOP\n");
    g_object_set(prebuffer, "mode", 1 /* PRE_RECORD */, NULL);
    sleep(1);

    stop_pipeline();
    cleanup();
}

/*
 * Test 4: No keyframe edge case
 * key-int-max very large => practically no keyframes
 * Expect:
 *  - no crash
 *  - mp4 valid
 *  - prebuffer flush may be empty
 */
static void test_nokey(void)
{
    build_pipeline("out_nokey.mp4", 100000);

    start_pipeline("TEST NO KEYFRAME EDGE CASE");
    sleep(5);

    g_print("-> RECORD START\n");
    g_object_set(prebuffer, "mode", 2 /* RECORD */, NULL);
    sleep(3);

    stop_pipeline();
    cleanup();
}

static void test_multi_cycle(void)
{
    build_pipeline("out_multi_cycle.mp4", 30);
    start_pipeline("TEST MULTI-CYCLE: Record twice in one session");
    
    /* Cycle 1 */
    g_print("-> Cycle 1: Recording...\n");
    g_object_set(prebuffer, "mode", 2 /* RECORD */, NULL);
    sleep(3);
    g_print("-> Cycle 1: Stop (Pre-record)\n");
    g_object_set(prebuffer, "mode", 1 /* PRE_RECORD */, NULL);
    sleep(2);

    /* Cycle 2 */
    g_print("-> Cycle 2: Recording again...\n");
    g_object_set(prebuffer, "mode", 2 /* RECORD */, NULL);
    sleep(3);
    g_print("-> Cycle 2: Stop\n");
    g_object_set(prebuffer, "mode", 1 /* PRE_RECORD */, NULL);
    sleep(1);

    stop_pipeline(); // Use your updated stop_pipeline_eos()
    cleanup();
}

/* NEW: Builder WITHOUT prebuffer (Direct connection) */
static void build_reference_pipeline(const char *outfile, int keyint)
{
    gchar desc[1024];

    /* Note: We removed '! prebuffer ...' and linked h264parse directly to mp4mux */
    snprintf(desc, sizeof(desc),
       "videotestsrc is-live=true pattern=ball "
        "! video/x-raw,width=720,height=540,framerate=30/1 "
        "! x264enc tune=zerolatency key-int-max=%d speed-preset=ultrafast "
        "! h264parse "
        "! mp4mux "
        "! filesink location=%s",
        keyint, outfile);

    pipeline = gst_parse_launch(desc, NULL);
    /* prebuffer remains NULL here */
    prebuffer = NULL; 
}

/* NEW CASE: Reference recording without plugin */
static void test_reference(void)
{
    build_reference_pipeline("out_reference.mp4", 30);

    start_pipeline("TEST REFERENCE: Normal recording (No Plugin)");
    
    g_print("-> Recording 30s...\n");
    sleep(30);

    stop_pipeline();
    cleanup();
}


/* -------------------------------------------------- */
/* Run all tests                                      */
/* -------------------------------------------------- */

static void test_all(void)
{
    g_print("\n===== RUNNING ALL PREBUFFER TESTS =====\n");
    test_reference();
    test_basic();
    // test_disabled();
    // test_short();
    // test_nokey();

    g_print("\n===== ALL TESTS COMPLETED =====\n");
}
/* -------------------------------------------------- */
/* main                                               */
/* -------------------------------------------------- */

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    if (argc < 2) {
        printf("Usage: %s [basic | disabled | short | nokey | all]\n", argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], "basic"))
        test_basic();
    else if (!strcmp(argv[1], "disabled"))
        test_disabled();
    else if (!strcmp(argv[1], "short"))
        test_short();
    else if (!strcmp(argv[1], "nokey"))
        test_nokey();
    else if (!strcmp(argv[1], "cycle")) /* Added manual run option */
        test_multi_cycle();
    else if (!strcmp(argv[1], "reference"))
        test_multi_cycle();
    else if (!strcmp(argv[1], "all"))
        test_all();
    else {
        printf("Unknown test: %s\n", argv[1]);
        return 1;
    }

    printf("\nTest finished.\n");
    return 0;
}

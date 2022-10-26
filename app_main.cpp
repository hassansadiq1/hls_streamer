#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

#include "gstnvdsmeta.h"

#define GST_CAPS_FEATURES_NVMM "memory:NVMM"
#define SOURCE_PROPERTIES  "../source_properties.ini"
using namespace std;

vector<string> folder_names;

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;
    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_EOS:
            g_print ("End of stream\n");
            g_main_loop_quit (loop);
            break;
        case GST_MESSAGE_ERROR:{
            gchar *debug;
            GError *error;
            gst_message_parse_error (msg, &error, &debug);
            g_printerr ("ERROR from element %s: %s\n",
                GST_OBJECT_NAME (msg->src), error->message);
            if (debug)
                g_printerr ("Error details: %s\n", debug);
            g_free (debug);
            g_error_free (error);
            g_main_loop_quit (loop);
            break;
        }
        default:
            break;
    }
  return TRUE;
}

static void
cb_newpad (GstElement * decodebin, GstPad * decoder_src_pad, gpointer data)
{
    g_print ("In cb_newpad\n");
    GstCaps *caps = gst_pad_get_current_caps (decoder_src_pad);
    const GstStructure *str = gst_caps_get_structure (caps, 0);
    const gchar *name = gst_structure_get_name (str);
    GstElement *source_bin = (GstElement *) data;
    GstCapsFeatures *features = gst_caps_get_features (caps, 0);

    /* Need to check if the pad created by the decodebin is for video and not
    * audio. */
    if (!strncmp (name, "video", 5)) {
        /* Link the decodebin pad only if decodebin has picked nvidia
        * decoder plugin nvdec_*. We do this by checking if the pad caps contain
        * NVMM memory features. */
        if (gst_caps_features_contains (features, GST_CAPS_FEATURES_NVMM)) {
        /* Get the source bin ghost pad */
        GstPad *bin_ghost_pad = gst_element_get_static_pad (source_bin, "src");
        if (!gst_ghost_pad_set_target (GST_GHOST_PAD (bin_ghost_pad),
                decoder_src_pad)) {
            g_printerr ("Failed to link decoder src pad to source bin ghost pad\n");
        }
        gst_object_unref (bin_ghost_pad);
        } else {
            g_printerr ("Error: Decodebin did not pick nvidia decoder plugin.\n");
        }
    }
}

static void
decodebin_child_added (GstChildProxy * child_proxy, GObject * object,
    gchar * name, gpointer user_data)
{
    g_print ("Decodebin child added: %s\n", name);
    if (g_strrstr (name, "decodebin") == name) {
        g_signal_connect (G_OBJECT (object), "child-added",
            G_CALLBACK (decodebin_child_added), user_data);
    }
}

static GstElement *
create_source_bin (guint index, gchar * uri)
{
    GstElement *bin = NULL, *uri_decode_bin = NULL;
    gchar bin_name[16] = { };

    g_snprintf (bin_name, 15, "source-bin-%02d", index);
    /* Create a source GstBin to abstract this bin's content from the rest of the
    * pipeline */
    bin = gst_bin_new (bin_name);

    /* Source element for reading from the uri.
    * We will use decodebin and let it figure out the container format of the
    * stream and the codec and plug the appropriate demux and decode plugins. */
    uri_decode_bin = gst_element_factory_make ("uridecodebin", "uri-decode-bin");

    if (!bin || !uri_decode_bin) {
        g_printerr ("One element in source bin could not be created.\n");
        return NULL;
    }

    /* We set the input uri to the source element */
    g_object_set (G_OBJECT (uri_decode_bin), "uri", uri, NULL);

    /* Connect to the "pad-added" signal of the decodebin which generates a
    * callback once a new pad for raw data has beed created by the decodebin */
    g_signal_connect (G_OBJECT (uri_decode_bin), "pad-added",
        G_CALLBACK (cb_newpad), bin);
    g_signal_connect (G_OBJECT (uri_decode_bin), "child-added",
        G_CALLBACK (decodebin_child_added), bin);

    gst_bin_add (GST_BIN (bin), uri_decode_bin);

    /* We need to create a ghost pad for the source bin which will act as a proxy
    * for the video decoder src pad. The ghost pad will not have a target right
    * now. Once the decode bin creates the video decoder and generates the
    * cb_newpad callback, we will set the ghost pad target to the video decoder
    * src pad. */
    if (!gst_element_add_pad (bin, gst_ghost_pad_new_no_target ("src",
                GST_PAD_SRC))) {
        g_printerr ("Failed to add ghost pad in source bin\n");
        return NULL;
    }

    return bin;
}

void loadStrConfig(char *str1, string& str2) {
    std::ifstream fin(SOURCE_PROPERTIES);
    std::string line;
    while (getline(fin, line)) {
        std::istringstream sin(line.substr(line.find("=") + 1));
        if(line[0] == '#')
            continue;

        if (line.find(str1) != -1){
            sin >> str2;
            return;
        }
    }
    cout<<"Could not find "<<str1<<" property. Exiting"<<endl;
    exit(0);
}

void loadIntConfig(char *str1, int& str2) {
    std::ifstream fin(SOURCE_PROPERTIES);
    std::string line;
    while (getline(fin, line)) {
        std::istringstream sin(line.substr(line.find("=") + 1));
        if(line[0] == '#')
            continue;

        if (line.find(str1) != -1){
            sin >> str2;
            return;
        }
    }
    cout<<"Could not find "<<str1<<" property. Exiting"<<endl;
    exit(0);
}

void close_ports(){
    for (int i = 0; i < folder_names.size(); i++) {
    string kill_cmd = "kill -9 `cat " + folder_names[i] + "/pid.txt`";
    system(kill_cmd.c_str());
    }
}

void sig_handler(int signo)
{
    close_ports();
    exit(0);
}

int
main (int argc, char *argv[])
{
    char temp_char[150];

    /* Standard GStreamer initialization */
    gst_init (&argc, &argv);

    GMainLoop *loop = NULL;
    GstElement *pipeline = NULL, *streammux = NULL, *streamdemux = NULL, *fakesink = NULL;
    GstBus *bus = NULL;
    guint bus_watch_id;

    loop = g_main_loop_new (NULL, FALSE);

   /* Create Pipeline element that will form a connection of other elements */
    pipeline = gst_pipeline_new ("dynamic-pipeline");
    streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");
    streamdemux = gst_element_factory_make ("nvstreamdemux", "stream-muxer");
    fakesink = gst_element_factory_make ("fakesink", "fake-sink");

    if (!pipeline || !streammux) {
        g_printerr ("Initial elements could not be created. Exiting.\n");
        exit(-1);
    }

    g_object_set (G_OBJECT (streammux), "width", 1920, "height",
        1080, "batched-push-timeout", 1000, NULL);

    // gst_bin_add_many (GST_BIN (pipeline), streammux, fakesink, NULL);
    // gst_element_link (streammux, fakesink);
    /* we add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
    gst_object_unref (bus);
    

    /* Lets add probe to get informed of the meta data generated, we add probe to
        * the sink pad of the osd element, since by that time, the buffer would have
        * had got all the metadata. */
    int num_sources = 0;
    loadIntConfig((char *) "num_sources", num_sources);
    g_object_set (G_OBJECT (streammux), "batch-size", num_sources, NULL);

    for (int index = 0; index < num_sources; index++) {
        string uri = "";
        string uri_prefix = "rtsp_uri_" + to_string(index+1);
        loadStrConfig((char *)uri_prefix.c_str(), uri);
        cout << "Adding: " << uri << endl;

        GstPad *sinkpad, *srcpad;
        gchar pad_name[16] = { };

        GstElement *source_bin = create_source_bin (index, (char *)uri.c_str());

        if (!source_bin) {
            g_printerr ("Failed to create source bin. Exiting.\n");
            return -1;
        }

        gst_bin_add (GST_BIN (pipeline), source_bin);

        GstElement *nvvideoconvert = NULL, *capsfilter = NULL, *encoder = NULL, *parser = NULL, *hlssink = NULL;
        GstCaps *caps = NULL;
        gchar vid_name[50], caps_name[50], enc_name[50], parser_name[50], sink_name[50];

        g_snprintf (vid_name, sizeof (vid_name), "nv-videoconvert%d", index);
        g_snprintf (caps_name, sizeof (caps_name), "capsfilter%d", index);
        g_snprintf (enc_name, sizeof (enc_name), "encoder%d", index);
        g_snprintf (parser_name, sizeof (parser_name), "parser%d", index);
        g_snprintf (sink_name, sizeof (sink_name), "hlssink%d", index);

        nvvideoconvert = gst_element_factory_make ("nvvideoconvert", vid_name);
        capsfilter = gst_element_factory_make ("capsfilter", caps_name);
        encoder = gst_element_factory_make ("nvv4l2h264enc", enc_name);
        parser = gst_element_factory_make ("h264parse", parser_name);
        hlssink = gst_element_factory_make ("hlssink2", sink_name);

        if (!nvvideoconvert || !capsfilter || !encoder || !parser || !hlssink) {
            g_printerr ("One element could not be created. Exiting.\n");
            return -1;
        }

        caps = gst_caps_from_string ("video/x-raw(memory:NVMM), format=I420");
        g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);

        // create folder for each source
        string folder_name = "source_" + to_string(index+1);
        string folder_cmd = "mkdir " + folder_name;
        system(folder_cmd.c_str());
        folder_names.push_back(folder_name);

        string hls_uri = "";
        string hls_uri_prefix = "hls_uri_" + to_string(index+1);
        loadStrConfig((char *)hls_uri_prefix.c_str(), hls_uri);

        string port = hls_uri.substr(hls_uri.find_last_of(":") + 1);

        // start python http server for each source and keep its pid
        string python_cmd = "python3 -m http.server " + port + " --directory " + folder_name + " & echo $! > " + folder_name + "/pid.txt";
        system(python_cmd.c_str());

        string segments_location = folder_name + "/segment_%02d.ts";
        string playlist_location = folder_name + "/playlist.m3u8";

        g_object_set (G_OBJECT (hlssink),
            "playlist-root", hls_uri.c_str(),
            "location", segments_location.c_str(),
            "playlist-location", playlist_location.c_str(),
            "target-duration", 3,
            "max-files", 3,
             NULL);

        gst_bin_add_many (GST_BIN (pipeline), nvvideoconvert, capsfilter, encoder, parser, hlssink, NULL);
        gst_element_link_many (nvvideoconvert, capsfilter, encoder, parser, hlssink, NULL);
        gst_element_link(source_bin, nvvideoconvert);

        // close python http server
        string kill_cmd = "kill -9 `cat " + folder_name + "/pid.txt`";
    }

    // handle signals
    signal(SIGINT, sig_handler);

    // Set the pipeline to "playing" state
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_main_loop_run(loop);
    /* Out of the main loop, clean up nicely */
    g_print ("Returned, stopping playback\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (loop);
    close_ports();
    return 0;
}

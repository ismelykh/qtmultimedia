/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "gstvideoconnector_p.h"
#include <unistd.h>

/* signals */
enum
{
  SIGNAL_RESEND_NEW_SEGMENT,
  SIGNAL_CONNECTION_FAILED,
  LAST_SIGNAL
};
static guint gst_video_connector_signals[LAST_SIGNAL] = { 0 };


GST_DEBUG_CATEGORY_STATIC (video_connector_debug);
#define GST_CAT_DEFAULT video_connector_debug

static GstStaticPadTemplate gst_video_connector_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
                         GST_PAD_SINK,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_video_connector_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
                         GST_PAD_SRC,
                         GST_PAD_ALWAYS,
                         GST_STATIC_CAPS_ANY);


#if GST_CHECK_VERSION(1,0,0)

G_DEFINE_TYPE(GstVideoConnector, gst_video_connector, GST_TYPE_ELEMENT);
#else
#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (video_connector_debug, \
    "video-connector", 0, "An identity like element for reconnecting video stream");

GST_BOILERPLATE_FULL (GstVideoConnector, gst_video_connector, GstElement,
                      GST_TYPE_ELEMENT, _do_init);
#endif

static void gst_video_connector_dispose (GObject * object);

#if GST_CHECK_VERSION(1,0,0)
static GstFlowReturn gst_video_connector_chain (GstPad * pad, GstObject* parent, GstBuffer * buf);
#else
static GstFlowReturn gst_video_connector_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_video_connector_buffer_alloc (GstPad * pad,
                                                       guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
#endif

static GstStateChangeReturn gst_video_connector_change_state (GstElement *
                                                              element, GstStateChange transition);

#if GST_CHECK_VERSION(1,0,0)
static gboolean gst_video_connector_handle_sink_event (GstPad * pad, GstObject* parent,
                                                       GstEvent * event);
#else
static gboolean gst_video_connector_handle_sink_event (GstPad * pad,
                                                       GstEvent * event);
#endif

#if GST_CHECK_VERSION(1,0,0)
static GstPadProbeReturn gst_video_connector_new_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer object);
static GstPadProbeReturn gst_video_connector_new_event_probe(GstPad *pad, GstPadProbeInfo *info, gpointer object);
static GstPadProbeReturn gst_video_connector_new_query_probe(GstPad *pad, GstPadProbeInfo *info, gpointer object);
#else
static gboolean gst_video_connector_new_buffer_probe(GstObject *pad, GstBuffer *buffer, guint * object);
static gboolean gst_video_connector_setcaps (GstPad  *pad, GstCaps *caps);
static GstCaps *gst_video_connector_getcaps (GstPad * pad);
static gboolean gst_video_connector_acceptcaps (GstPad * pad, GstCaps * caps);
#endif

static void gst_video_connector_resend_new_segment(GstElement * element, gboolean emitFailedSignal);

#if GST_CHECK_VERSION(1,0,0)
static void
gst_video_connector_class_init (GstVideoConnectorClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

    gst_element_class_set_details_simple (gstelement_class, "Video Connector",
                                          "Generic",
                                          "An identity like element used for reconnecting video stream",
                                          "Dmytro Poplavskiy <dmytro.poplavskiy@nokia.com>");
    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&gst_video_connector_sink_factory));
    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&gst_video_connector_src_factory));

    gst_video_connector_parent_class = g_type_class_peek_parent (klass);

    gobject_class->dispose = gst_video_connector_dispose;
    gstelement_class->change_state = gst_video_connector_change_state;
    klass->resend_new_segment = gst_video_connector_resend_new_segment;

    gst_video_connector_signals[SIGNAL_RESEND_NEW_SEGMENT] =
            g_signal_new ("resend-new-segment", G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                          G_STRUCT_OFFSET (GstVideoConnectorClass, resend_new_segment), NULL, NULL,
                          g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    gst_video_connector_signals[SIGNAL_CONNECTION_FAILED] =
            g_signal_new ("connection-failed", G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_LAST,
                          0, NULL, NULL,
                          g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    GST_DEBUG_CATEGORY_INIT(video_connector_debug, "video-connector", 0,
                            "An identity like element for reconnecting video stream");

}

#else

static void
gst_video_connector_base_init (gpointer g_class)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

    gst_element_class_set_details_simple (element_class, "Video Connector",
                                          "Generic",
                                          "An identity like element used for reconnecting video stream",
                                          "Dmytro Poplavskiy <dmytro.poplavskiy@nokia.com>");
    gst_element_class_add_pad_template (element_class,
                                        gst_static_pad_template_get (&gst_video_connector_sink_factory));
    gst_element_class_add_pad_template (element_class,
                                        gst_static_pad_template_get (&gst_video_connector_src_factory));
}

static void
gst_video_connector_class_init (GstVideoConnectorClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

    parent_class = g_type_class_peek_parent (klass);

    gobject_class->dispose = gst_video_connector_dispose;
    gstelement_class->change_state = gst_video_connector_change_state;
    klass->resend_new_segment = gst_video_connector_resend_new_segment;

    gst_video_connector_signals[SIGNAL_RESEND_NEW_SEGMENT] =
            g_signal_new ("resend-new-segment", G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                          G_STRUCT_OFFSET (GstVideoConnectorClass, resend_new_segment), NULL, NULL,
                          g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    gst_video_connector_signals[SIGNAL_CONNECTION_FAILED] =
            g_signal_new ("connection-failed", G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_LAST,
                          0, NULL, NULL,
                          g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

#endif

static void
gst_video_connector_init (GstVideoConnector *element
#if GST_CHECK_VERSION(1,0,0)
#else
                          ,GstVideoConnectorClass *g_class
#endif
                          )
{
#if GST_CHECK_VERSION(1,0,0)
#else
    (void) g_class;
#endif
    element->sinkpad =
            gst_pad_new_from_static_template (&gst_video_connector_sink_factory,
                                              "sink");
    gst_pad_set_chain_function(element->sinkpad,
                               GST_DEBUG_FUNCPTR (gst_video_connector_chain));
#if GST_CHECK_VERSION(1,0,0)
    /* gstreamer 1.x uses QUERIES and EVENTS for allocation and caps handiling purposes */
    GST_OBJECT_FLAG_SET (element->sinkpad, GST_PAD_FLAG_PROXY_CAPS);
    GST_OBJECT_FLAG_SET (element->sinkpad, GST_PAD_FLAG_PROXY_ALLOCATION);
#else
    gst_pad_set_event_function(element->sinkpad,
                               GST_DEBUG_FUNCPTR (gst_video_connector_handle_sink_event));

    gst_pad_set_bufferalloc_function(element->sinkpad,
                                     GST_DEBUG_FUNCPTR (gst_video_connector_buffer_alloc));
    gst_pad_set_setcaps_function(element->sinkpad,
                               GST_DEBUG_FUNCPTR (gst_video_connector_setcaps));
    gst_pad_set_getcaps_function(element->sinkpad,
                               GST_DEBUG_FUNCPTR(gst_video_connector_getcaps));
    gst_pad_set_acceptcaps_function(element->sinkpad,
                               GST_DEBUG_FUNCPTR(gst_video_connector_acceptcaps));
#endif
    gst_element_add_pad (GST_ELEMENT (element), element->sinkpad);

    element->srcpad =
            gst_pad_new_from_static_template (&gst_video_connector_src_factory,
                                              "src");
#if GST_CHECK_VERSION(1,0,0)
    gst_pad_add_probe(element->srcpad, GST_PAD_PROBE_TYPE_BUFFER,
                             gst_video_connector_new_buffer_probe, element, NULL);
    gst_pad_add_probe(element->srcpad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
                             gst_video_connector_new_query_probe, element, NULL);
    gst_pad_add_probe(element->sinkpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                             gst_video_connector_new_event_probe, element, NULL);
#else
    gst_pad_add_buffer_probe(element->srcpad,
                             G_CALLBACK(gst_video_connector_new_buffer_probe), element);
#endif
    gst_element_add_pad (GST_ELEMENT (element), element->srcpad);

    element->relinked = FALSE;
    element->failedSignalEmited = FALSE;
    gst_segment_init (&element->segment, GST_FORMAT_TIME);
    element->latest_buffer = NULL;
}

static void
gst_video_connector_reset (GstVideoConnector * element)
{
    element->relinked = FALSE;
    element->failedSignalEmited = FALSE;
    if (element->latest_buffer != NULL) {
        gst_buffer_unref (element->latest_buffer);
        element->latest_buffer = NULL;
    }
    gst_segment_init (&element->segment, GST_FORMAT_UNDEFINED);
}

static void
gst_video_connector_dispose (GObject * object)
{
    GstVideoConnector *element = GST_VIDEO_CONNECTOR (object);

    gst_video_connector_reset (element);

#if GST_CHECK_VERSION(1,0,0)
    G_OBJECT_CLASS (gst_video_connector_parent_class)->dispose (object);
#else
    G_OBJECT_CLASS (parent_class)->dispose (object);
#endif
}

#if GST_CHECK_VERSION(1,0,0)
/* For gstreamer 1.x we handle it in ALLOCATION Query */
#else
// "When this function returns anything else than GST_FLOW_OK,
// the buffer allocation failed and buf does not contain valid data."
static GstFlowReturn
gst_video_connector_buffer_alloc (GstPad * pad, guint64 offset, guint size,
                                  GstCaps * caps, GstBuffer ** buf)
{
    GstVideoConnector *element;
    GstFlowReturn res = GST_FLOW_OK;
    element = GST_VIDEO_CONNECTOR (GST_PAD_PARENT (pad));

    if (!buf)
        return GST_FLOW_ERROR;
    *buf = NULL;

    gboolean isFailed = FALSE;
    while (1) {
        GST_OBJECT_LOCK (element);
        gst_object_ref(element->srcpad);
        GST_OBJECT_UNLOCK (element);

        // Check if downstream element is in NULL state
        // and wait for up to 1 second for it to switch.
        GstPad *peerPad = gst_pad_get_peer(element->srcpad);
        if (peerPad) {
            GstElement *parent = gst_pad_get_parent_element(peerPad);
            gst_object_unref (peerPad);
            if (parent) {
                GstState state;
                GstState pending;
                int totalTimeout = 0;
                // This seems to sleep for about 10ms usually.
                while (totalTimeout < 1000000) {
                    gst_element_get_state(parent, &state, &pending, 0);
                    if (state != GST_STATE_NULL)
                        break;
                    usleep(5000);
                    totalTimeout += 5000;
                }

                gst_object_unref (parent);
                if (state == GST_STATE_NULL) {
                    GST_DEBUG_OBJECT (element, "Downstream element is in NULL state");
                    // Downstream filter seems to be in the wrong state

                    return GST_FLOW_UNEXPECTED;
                }
            }
        }

        res = gst_pad_alloc_buffer(element->srcpad, offset, size, caps, buf);
        gst_object_unref (element->srcpad);

        GST_DEBUG_OBJECT (element, "buffer alloc finished: %s", gst_flow_get_name (res));

        if (res == GST_FLOW_WRONG_STATE) {
            // Just in case downstream filter is still somehow in the wrong state.
            // Pipeline stalls if we report GST_FLOW_WRONG_STATE.
            return GST_FLOW_UNEXPECTED;
        }

        if (res >= GST_FLOW_OK || isFailed == TRUE)
            break;

        //if gst_pad_alloc_buffer failed, emit "connection-failed" signal
        //so colorspace transformation element can be inserted
        GST_INFO_OBJECT(element, "gst_video_connector_buffer_alloc failed, emit connection-failed signal");
        g_signal_emit(G_OBJECT(element), gst_video_connector_signals[SIGNAL_CONNECTION_FAILED], 0);
        isFailed = TRUE;
    }

    return res;
}

static gboolean
gst_video_connector_setcaps (GstPad  *pad, GstCaps *caps)
{
    GstVideoConnector *element;
    element = GST_VIDEO_CONNECTOR (GST_PAD_PARENT (pad));

    /* forward-negotiate */
    gboolean res = gst_pad_set_caps(element->srcpad, caps);

    gchar * debugmsg = NULL;
    GST_DEBUG_OBJECT(element, "gst_video_connector_setcaps %s %i", debugmsg = gst_caps_to_string(caps), res);
    if (debugmsg)
        g_free(debugmsg);

    if (!res) {
        //if set_caps failed, emit "connection-failed" signal
        //so colorspace transformation element can be inserted
        GST_INFO_OBJECT(element, "gst_video_connector_setcaps failed, emit connection-failed signal");
        g_signal_emit(G_OBJECT(element), gst_video_connector_signals[SIGNAL_CONNECTION_FAILED], 0);

        return gst_pad_set_caps(element->srcpad, caps);
    }

    return TRUE;
}

static GstCaps *gst_video_connector_getcaps (GstPad * pad)
{
    GstVideoConnector *element;
    element = GST_VIDEO_CONNECTOR (GST_PAD_PARENT (pad));

#if (GST_VERSION_MICRO > 25)
    GstCaps *caps = gst_pad_peer_get_caps_reffed(element->srcpad);
#else
    GstCaps *caps = gst_pad_peer_get_caps(element->srcpad);
#endif

    if (!caps)
        caps = gst_caps_new_any();

    return caps;
}


static gboolean gst_video_connector_acceptcaps (GstPad * pad, GstCaps * caps)
{
    GstVideoConnector *element;
    element = GST_VIDEO_CONNECTOR (GST_PAD_PARENT (pad));

    return gst_pad_peer_accept_caps(element->srcpad, caps);
}
#endif

static void
gst_video_connector_resend_new_segment(GstElement * element, gboolean emitFailedSignal)
{
    GST_INFO_OBJECT(element, "New segment requested, failed signal enabled: %i", emitFailedSignal);
    GstVideoConnector *connector = GST_VIDEO_CONNECTOR(element);
    connector->relinked = TRUE;
    if (emitFailedSignal)
        connector->failedSignalEmited = FALSE;
}

#if GST_CHECK_VERSION(1,0,0)
static GstPadProbeReturn gst_video_connector_new_event_probe(GstPad *pad, GstPadProbeInfo *info, gpointer object)
{
    GstVideoConnector *connector = GST_VIDEO_CONNECTOR (object);
    GstEvent *event = gst_pad_probe_info_get_event(info);

    GST_DEBUG_OBJECT(connector, "Event %"GST_PTR_FORMAT" received\n", event);

    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn gst_video_connector_new_query_probe(GstPad *pad, GstPadProbeInfo *info, gpointer object)
{
    GstVideoConnector *connector = GST_VIDEO_CONNECTOR (object);
    GstQuery *query = gst_pad_probe_info_get_query(info);

    GST_DEBUG_OBJECT(connector, "Query %"GST_PTR_FORMAT" received\n", query);

    return GST_PAD_PROBE_OK;
}
#endif

#if GST_CHECK_VERSION(1,0,0)
static GstPadProbeReturn gst_video_connector_new_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer object)
{
    (void) info;
#else
static gboolean gst_video_connector_new_buffer_probe(GstObject *pad, GstBuffer *buffer, guint * object)
{
    (void) buffer;
#endif
    (void) pad;


    GstVideoConnector *element = GST_VIDEO_CONNECTOR (object);

    /*
      If relinking is requested, the current buffer should be rejected and
      the new segment + previous buffer should be pushed first
    */

    if (element->relinked)
        GST_LOG_OBJECT(element, "rejected buffer because of new segment request");

#if GST_CHECK_VERSION(1,0,0)
    return element->relinked ? GST_PAD_PROBE_DROP : GST_PAD_PROBE_OK;
#else
    return !element->relinked;
#endif
}

static GstFlowReturn
#if GST_CHECK_VERSION(1,0,0)
gst_video_connector_chain (GstPad * pad, GstObject* parent, GstBuffer * buf)
#else
gst_video_connector_chain (GstPad * pad, GstBuffer * buf)
#endif
{
    GstFlowReturn res;
    GstVideoConnector *element;

#if GST_CHECK_VERSION(1,0,0)
    (void)parent;
#endif

    element = GST_VIDEO_CONNECTOR (gst_pad_get_parent (pad));

    do {
        /*
          Resend the segment message and last buffer to preroll the new sink.
          Sinks can be changed multiple times while paused,
          while loop allows to send the segment message and preroll
          all of them with the same buffer.
        */
        while (element->relinked) {
            element->relinked = FALSE;
#if GST_CHECK_VERSION(1,0,0)
            if (element->latest_buffer && GST_BUFFER_TIMESTAMP_IS_VALID(element->latest_buffer)) {
                element->segment.position = GST_BUFFER_TIMESTAMP (element->latest_buffer);
            }
#else
            gint64 pos = element->segment.last_stop;
            if (element->latest_buffer && GST_BUFFER_TIMESTAMP_IS_VALID(element->latest_buffer)) {
                pos = GST_BUFFER_TIMESTAMP (element->latest_buffer);
            }
#endif

            //push a new segment and last buffer
#if GST_CHECK_VERSION(1,0,0)
            GstEvent *ev = gst_event_new_segment (&element->segment);

#else
            GstEvent *ev = gst_event_new_new_segment (TRUE,
                                                      element->segment.rate,
                                                      element->segment.format,
                                                      pos, //start
                                                      element->segment.stop,
                                                      pos);
#endif

            GST_DEBUG_OBJECT (element, "Pushing new segment event");
            if (!gst_pad_push_event (element->srcpad, ev)) {
                GST_WARNING_OBJECT (element,
                                    "Newsegment handling failed in %" GST_PTR_FORMAT,
                                    element->srcpad);
            }

            if (element->latest_buffer) {
                GST_DEBUG_OBJECT (element, "Pushing latest buffer...");
                gst_buffer_ref(element->latest_buffer);
                gst_pad_push(element->srcpad, element->latest_buffer);
            }
        }

        gst_buffer_ref(buf);

        //it's possible video sink is changed during gst_pad_push blocked by
        //pad lock, in this case ( element->relinked == TRUE )
        //the buffer should be rejected by the buffer probe and
        //the new segment + prev buffer should be sent before

        GST_LOG_OBJECT (element, "Pushing buffer...");
        res = gst_pad_push (element->srcpad, buf);
        GST_LOG_OBJECT (element, "Pushed buffer: %s", gst_flow_get_name (res));

        //if gst_pad_push failed give the service another chance,
        //it may still work with the colorspace element added
        if (!element->failedSignalEmited && res == GST_FLOW_NOT_NEGOTIATED) {
            element->failedSignalEmited = TRUE;
            GST_INFO_OBJECT(element, "gst_pad_push failed, emit connection-failed signal");
            g_signal_emit(G_OBJECT(element), gst_video_connector_signals[SIGNAL_CONNECTION_FAILED], 0);
        }

    } while (element->relinked);


    if (element->latest_buffer) {
        gst_buffer_unref (element->latest_buffer);
        element->latest_buffer = NULL;
    }

    //don't save the last video buffer on maemo6 because of buffers shortage
    //with omapxvsink
#ifndef Q_WS_MAEMO_6
    element->latest_buffer = gst_buffer_ref(buf);
#endif

    gst_buffer_unref(buf);
    gst_object_unref (element);

    return res;
}

static GstStateChangeReturn
gst_video_connector_change_state (GstElement * element,
                                  GstStateChange transition)
{
    GstVideoConnector *connector;
    GstStateChangeReturn result;

    connector = GST_VIDEO_CONNECTOR(element);
#if GST_CHECK_VERSION(1,0,0)
    result = GST_ELEMENT_CLASS (gst_video_connector_parent_class)->change_state(element, transition);
#else
    result = GST_ELEMENT_CLASS (parent_class)->change_state(element, transition);
#endif
    switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_video_connector_reset (connector);
        break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        connector->relinked = FALSE;
        break;
    default:
        break;
    }

    return result;
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean gst_video_connector_handle_sink_event (GstPad * pad, GstObject* parent,
                                                       GstEvent * event)
{
    GstVideoConnector *element = GST_VIDEO_CONNECTOR (gst_pad_get_parent (pad));

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_SEGMENT:
      break;
      case GST_EVENT_CAPS:
      break;
    default:
      break;
    }

    gst_object_unref (element);
    return gst_pad_event_default (pad, parent, event);
}

#else

static gboolean gst_video_connector_handle_sink_event (GstPad * pad,
                                                       GstEvent * event)
{
    if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
        GstVideoConnector *element = GST_VIDEO_CONNECTOR (gst_pad_get_parent (pad));

        gboolean update;
        GstFormat format;
        gdouble rate, arate;
        gint64 start, stop, time;

        gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
                                          &start, &stop, &time);
        GST_LOG_OBJECT (element,
                          "NEWSEGMENT update %d, rate %lf, applied rate %lf, "
                          "format %d, " "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
                          G_GINT64_FORMAT, update, rate, arate, format, start, stop, time);

        gst_segment_set_newsegment_full (&element->segment, update,
                                         rate, arate, format, start, stop, time);
        gst_object_unref (element);
    }

    return gst_pad_event_default (pad, event);
}

#endif

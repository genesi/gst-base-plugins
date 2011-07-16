/* GStreamer
 *
 * gstv4lmjpegsrc.c: hardware MJPEG video source plugin
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "v4lmjpegsrc_calls.h"

GST_DEBUG_CATEGORY (v4lmjpegsrc_debug);
#define GST_CAT_DEFAULT v4lmjpegsrc_debug

/* V4lMjpegSrc signals and args */
enum
{
  SIGNAL_FRAME_CAPTURE,
  SIGNAL_FRAME_DROP,
  SIGNAL_FRAME_INSERT,
  SIGNAL_FRAME_LOST,
  LAST_SIGNAL
};

/* arguments */
enum
{
  ARG_0,
#if 0
  ARG_X_OFFSET,
  ARG_Y_OFFSET,
  ARG_F_WIDTH,
  ARG_F_HEIGHT,
  /* normally, we would want to use subframe capture, however,
   * for the time being it's easier if we disable it first */
#endif
  ARG_QUALITY,
  ARG_NUMBUFS,
  ARG_BUFSIZE,
  ARG_USE_FIXED_FPS
};

GST_FORMATS_FUNCTION (GstPad *, gst_v4lmjpegsrc_get_formats,
    GST_FORMAT_TIME, GST_FORMAT_DEFAULT);
GST_QUERY_TYPE_FUNCTION (GstPad *, gst_v4lmjpegsrc_get_query_types,
    GST_QUERY_POSITION);

/* init functions */
static void gst_v4lmjpegsrc_base_init (gpointer g_class);
static void gst_v4lmjpegsrc_class_init (GstV4lMjpegSrcClass * klass);
static void gst_v4lmjpegsrc_init (GstV4lMjpegSrc * v4lmjpegsrc);

/* pad/info functions */
static gboolean gst_v4lmjpegsrc_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_v4lmjpegsrc_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);

/* buffer functions */
static GstPadLinkReturn gst_v4lmjpegsrc_srcconnect (GstPad * pad,
    const GstCaps * caps);
static GstData *gst_v4lmjpegsrc_get (GstPad * pad);
static GstCaps *gst_v4lmjpegsrc_getcaps (GstPad * pad);

/* get/set params */
static void gst_v4lmjpegsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_v4lmjpegsrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* set_clock function for A/V sync */
static void gst_v4lmjpegsrc_set_clock (GstElement * element, GstClock * clock);

/* state handling */
static GstStateChangeReturn gst_v4lmjpegsrc_change_state (GstElement * element);

/* requeue buffer after use */
static void gst_v4lmjpegsrc_buffer_free (GstBuffer * buffer);

static GstElementClass *parent_class = NULL;
static guint gst_v4lmjpegsrc_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4lmjpegsrc_get_type (void)
{
  static GType v4lmjpegsrc_type = 0;

  if (!v4lmjpegsrc_type) {
    static const GTypeInfo v4lmjpegsrc_info = {
      sizeof (GstV4lMjpegSrcClass),
      gst_v4lmjpegsrc_base_init,
      NULL,
      (GClassInitFunc) gst_v4lmjpegsrc_class_init,
      NULL,
      NULL,
      sizeof (GstV4lMjpegSrc),
      0,
      (GInstanceInitFunc) gst_v4lmjpegsrc_init,
      NULL
    };

    v4lmjpegsrc_type =
        g_type_register_static (GST_TYPE_V4LELEMENT, "GstV4lMjpegSrc",
        &v4lmjpegsrc_info, 0);
  }
  return v4lmjpegsrc_type;
}


static void
gst_v4lmjpegsrc_base_init (gpointer g_class)
{
  static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("image/jpeg, "
          "width = (int) [ 0, MAX ], "
          "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0, MAX ]")
      );
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_sinmple (gstelement_class,
      "Video (video4linux/MJPEG) Source", "Source/Video",
      "Reads MJPEG-encoded frames from a zoran MJPEG/video4linux device",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_v4lmjpegsrc_class_init (GstV4lMjpegSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

#if 0
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_X_OFFSET,
      g_param_spec_int ("x-offset", "x_offset", "x_offset",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_Y_OFFSET,
      g_param_spec_int ("y-offset", "y_offset", "y_offset",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_F_WIDTH,
      g_param_spec_int ("frame-width", "frame_width", "frame_width",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_F_HEIGHT,
      g_param_spec_int ("frame-height", "frame_height", "frame_height",
          G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_int ("quality", "Quality", "JPEG frame quality",
          1, 100, 50, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUMBUFS,
      g_param_spec_int ("num-buffers", "Num Buffers", "Number of Buffers",
          1, 256, 64, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFSIZE,
      g_param_spec_int ("buffer-size", "Buffer Size", "Size of buffers",
          0, 512 * 1024, 128 * 1024,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_USE_FIXED_FPS,
      g_param_spec_boolean ("use-fixed-fps", "Use Fixed FPS",
          "Drop/Insert frames to reach a certain FPS (TRUE) "
          "or adapt FPS to suit the number of grabbed frames",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* signals */
  gst_v4lmjpegsrc_signals[SIGNAL_FRAME_CAPTURE] =
      g_signal_new ("frame-capture", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4lMjpegSrcClass, frame_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4lmjpegsrc_signals[SIGNAL_FRAME_DROP] =
      g_signal_new ("frame-drop", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstV4lMjpegSrcClass, frame_drop), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4lmjpegsrc_signals[SIGNAL_FRAME_INSERT] =
      g_signal_new ("frame-insert", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4lMjpegSrcClass, frame_insert),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4lmjpegsrc_signals[SIGNAL_FRAME_LOST] =
      g_signal_new ("frame-lost", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstV4lMjpegSrcClass, frame_lost), NULL, NULL,
      g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  GST_DEBUG_CATEGORY_INIT (v4lmjpegsrc_debug, "v4lmjpegsrc", 0,
      "V4L MJPEG source element");
  gobject_class->set_property = gst_v4lmjpegsrc_set_property;
  gobject_class->get_property = gst_v4lmjpegsrc_get_property;

  gstelement_class->change_state = gst_v4lmjpegsrc_change_state;

  gstelement_class->set_clock = gst_v4lmjpegsrc_set_clock;
}


static void
gst_v4lmjpegsrc_init (GstV4lMjpegSrc * v4lmjpegsrc)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (v4lmjpegsrc);

  GST_OBJECT_FLAG_SET (GST_ELEMENT (v4lmjpegsrc), GST_ELEMENT_THREAD_SUGGESTED);

  v4lmjpegsrc->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_element_add_pad (GST_ELEMENT (v4lmjpegsrc), v4lmjpegsrc->srcpad);

  gst_pad_set_get_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_get);
  gst_pad_set_getcaps_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_getcaps);
  gst_pad_set_link_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_srcconnect);
  gst_pad_set_convert_function (v4lmjpegsrc->srcpad,
      gst_v4lmjpegsrc_src_convert);
  gst_pad_set_formats_function (v4lmjpegsrc->srcpad,
      gst_v4lmjpegsrc_get_formats);
  gst_pad_set_query_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_src_query);
  gst_pad_set_query_type_function (v4lmjpegsrc->srcpad,
      gst_v4lmjpegsrc_get_query_types);

#if 0
  v4lmjpegsrc->frame_width = 0;
  v4lmjpegsrc->frame_height = 0;
  v4lmjpegsrc->x_offset = -1;
  v4lmjpegsrc->y_offset = -1;
#endif

  v4lmjpegsrc->quality = 50;

  v4lmjpegsrc->numbufs = 64;

  /* no clock */
  v4lmjpegsrc->clock = NULL;

  /* fps */
  v4lmjpegsrc->use_fixed_fps = TRUE;

  v4lmjpegsrc->is_capturing = FALSE;
}


static gboolean
gst_v4lmjpegsrc_get_fps (GstV4lMjpegSrc * v4lmjpegsrc, GValue * fps)
{
  gint norm;

  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION (fps), FALSE);

  if (!v4lmjpegsrc->use_fixed_fps &&
      v4lmjpegsrc->clock != NULL && v4lmjpegsrc->handled > 0) {
    /* try to get time from clock master and calculate fps */
    GstClockTime time =
        gst_clock_get_time (v4lmjpegsrc->clock) - v4lmjpegsrc->substract_time;
    return v4lmjpegsrc->handled * GST_SECOND / time;
  }

  /* if that failed ... */

  if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lmjpegsrc)))
    return FALSE;

  if (!gst_v4l_get_chan_norm (GST_V4LELEMENT (v4lmjpegsrc), NULL, &norm))
    return FALSE;

  if (norm == VIDEO_MODE_NTSC)
    gst_value_set_fraction (fps, 30000, 1001);
  else
    gst_value_set_fraction (fps, 25, 1);

  return TRUE;
}

static gboolean
gst_v4lmjpegsrc_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  GValue fps = { 0 };
  gboolean result = TRUE;

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  g_value_init (&fps, GST_VALUE_FRACTION);
  if (!gst_v4lmjpegsrc_get_fps (v4lmjpegsrc, &fps))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale (src_value,
              gst_value_get_fraction_numerator (&fps),
              gst_value_get_fraction_denominator (&fps) * GST_SECOND);
          break;
        default:
          result = FALSE;
      }
      break;

    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * gst_util_uint64_scale_int (GST_SECOND,
              gst_value_get_fraction_denominator (&fps),
              gst_value_get_fraction_numerator (&fps));
          break;
        default:
          result = FALSE;
      }
      break;

    default:
      result = FALSE;
  }

  g_value_unset (&fps);
  return result;
}

static gboolean
gst_v4lmjpegsrc_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value)
{
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));
  gboolean res = TRUE;
  GValue fps = { 0 };

  g_value_init (&fps, GST_VALUE_FRACTION);
  if (!gst_v4lmjpegsrc_get_fps (v4lmjpegsrc, &fps))
    return FALSE;

  switch (type) {
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = v4lmjpegsrc->handled * gst_util_uint64_scale_int (GST_SECOND,
              gst_value_get_fraction_denominator (&fps),
              gst_value_get_fraction_numerator (&fps));
          break;
        case GST_FORMAT_DEFAULT:
          *value = v4lmjpegsrc->handled;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  g_value_unset (&fps);
  return res;
}

static inline gulong
calc_bufsize (int hor_dec, int ver_dec)
{
  guint8 div = hor_dec * ver_dec;
  guint32 num = (1024 * 512) / (div);
  guint32 result = 2;

  num--;
  while (num) {
    num >>= 1;
    result <<= 1;
  }

  if (result > (512 * 1024))
    return (512 * 1024);
  if (result < 8192)
    return 8192;
  return result;
}

static GstPadLinkReturn
gst_v4lmjpegsrc_srcconnect (GstPad * pad, const GstCaps * caps)
{
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));
  gint hor_dec, ver_dec;
  gint w, h;
  gint max_w = GST_V4LELEMENT (v4lmjpegsrc)->vcap.maxwidth,
      max_h = GST_V4LELEMENT (v4lmjpegsrc)->vcap.maxheight;
  gulong bufsize;
  GstStructure *structure;
  gboolean was_capturing;

  /* in case the buffers are active (which means that we already
   * did capsnego before and didn't clean up), clean up anyways */
  if ((was_capturing = v4lmjpegsrc->is_capturing)) {
    if (!gst_v4lmjpegsrc_capture_stop (v4lmjpegsrc))
      return GST_PAD_LINK_REFUSED;
  }
  if (GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lmjpegsrc))) {
    if (!gst_v4lmjpegsrc_capture_deinit (v4lmjpegsrc))
      return GST_PAD_LINK_REFUSED;
  } else if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lmjpegsrc))) {
    return GST_PAD_LINK_DELAYED;
  }

  /* Note: basically, we don't give a damn about the opposite caps here.
   * that might seem odd, but it isn't. we know that the opposite caps is
   * either NULL or has mime type image/jpeg, and in both cases, we'll set
   * our own mime type back and it'll work. Other properties are to be set
   * by the src, not by the opposite caps */

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);

  /* figure out decimation */
  if (w >= max_w) {
    hor_dec = 1;
  } else if (w * 2 >= max_w) {
    hor_dec = 2;
  } else {
    hor_dec = 4;
  }
  if (h >= max_h) {
    ver_dec = 1;
  } else if (h * 2 >= max_h) {
    ver_dec = 2;
  } else {
    ver_dec = 4;
  }

  /* calculate bufsize */
  bufsize = calc_bufsize (hor_dec, ver_dec);

  /* set buffer info */
  if (!gst_v4lmjpegsrc_set_buffer (v4lmjpegsrc, v4lmjpegsrc->numbufs, bufsize)) {
    return GST_PAD_LINK_REFUSED;
  }

  /* set capture parameters and mmap the buffers */
  if (hor_dec == ver_dec) {
    if (!gst_v4lmjpegsrc_set_capture (v4lmjpegsrc,
            hor_dec, v4lmjpegsrc->quality)) {
      return GST_PAD_LINK_REFUSED;
    }
  } else {
    if (!gst_v4lmjpegsrc_set_capture_m (v4lmjpegsrc,
            0, 0, max_w, max_h, hor_dec, ver_dec, v4lmjpegsrc->quality)) {
      return GST_PAD_LINK_REFUSED;
    }
  }
#if 0
  if (!v4lmjpegsrc->frame_width && !v4lmjpegsrc->frame_height &&
      v4lmjpegsrc->x_offset < 0 && v4lmjpegsrc->y_offset < 0 &&
      v4lmjpegsrc->horizontal_decimation == v4lmjpegsrc->vertical_decimation) {
    if (!gst_v4lmjpegsrc_set_capture (v4lmjpegsrc,
            v4lmjpegsrc->horizontal_decimation, v4lmjpegsrc->quality))
      return GST_PAD_LINK_REFUSED;
  } else {
    if (!gst_v4lmjpegsrc_set_capture_m (v4lmjpegsrc,
            v4lmjpegsrc->x_offset, v4lmjpegsrc->y_offset,
            v4lmjpegsrc->frame_width, v4lmjpegsrc->frame_height,
            v4lmjpegsrc->horizontal_decimation,
            v4lmjpegsrc->vertical_decimation, v4lmjpegsrc->quality))
      return GST_PAD_LINK_REFUSED;
  }
#endif

  if (!gst_v4lmjpegsrc_capture_init (v4lmjpegsrc))
    return GST_PAD_LINK_REFUSED;

  if (was_capturing || GST_STATE (v4lmjpegsrc) == GST_STATE_PLAYING)
    if (!gst_v4lmjpegsrc_capture_start (v4lmjpegsrc))
      return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}


static GstData *
gst_v4lmjpegsrc_get (GstPad * pad)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  GstBuffer *buf;
  gint num;
  GValue fps = { 0 };
  GstClockTime duration;
  GstClockTime cur_frame_time;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  if (v4lmjpegsrc->use_fixed_fps) {
    g_value_init (&fps, GST_VALUE_FRACTION);
    duration = gst_util_uint64_scale_int (GST_SECOND,
        gst_value_get_fraction_denominator (&fps),
        gst_value_get_fraction_numerator (&fps));
    cur_frame_time =
        gst_util_uint64_scale_int (v4lmjpegsrc->handled * GST_SECOND,
        gst_value_get_fraction_denominator (&fps),
        gst_value_get_fraction_numerator (&fps));


    if (!gst_v4lmjpegsrc_get_fps (v4lmjpegsrc, &fps)) {
      g_value_unset (&fps);
      return NULL;
    }
  }

  if (v4lmjpegsrc->need_writes > 0) {
    /* use last frame */
    num = v4lmjpegsrc->last_frame;
    v4lmjpegsrc->need_writes--;
  } else if (v4lmjpegsrc->clock && v4lmjpegsrc->use_fixed_fps) {
    GstClockTime time;
    gboolean have_frame = FALSE;

    do {
      /* by default, we use the frame once */
      v4lmjpegsrc->need_writes = 1;

      /* grab a frame from the device */
      if (!gst_v4lmjpegsrc_grab_frame (v4lmjpegsrc, &num,
              &v4lmjpegsrc->last_size))
        return NULL;

      v4lmjpegsrc->last_frame = num;
      time = GST_TIMEVAL_TO_TIME (v4lmjpegsrc->bsync.timestamp) -
          v4lmjpegsrc->substract_time;

      /* first check whether we lost any frames according to the device */
      if (v4lmjpegsrc->last_seq != 0) {
        if (v4lmjpegsrc->bsync.seq - v4lmjpegsrc->last_seq > 1) {
          v4lmjpegsrc->need_writes =
              v4lmjpegsrc->bsync.seq - v4lmjpegsrc->last_seq;
          g_signal_emit (G_OBJECT (v4lmjpegsrc),
              gst_v4lmjpegsrc_signals[SIGNAL_FRAME_LOST], 0,
              v4lmjpegsrc->bsync.seq - v4lmjpegsrc->last_seq - 1);
        }
      }
      v4lmjpegsrc->last_seq = v4lmjpegsrc->bsync.seq;

      /* decide how often we're going to write the frame - set
       * v4lmjpegsrc->need_writes to (that-1) and have_frame to TRUE
       * if we're going to write it - else, just continue.
       * 
       * time is generally the system or audio clock. Let's
       * say that we've written one second of audio, then we want
       * to have written one second of video too, within the same
       * timeframe. This means that if time - begin_time = X sec,
       * we want to have written X*fps frames. If we've written
       * more - drop, if we've written less - dup... */
      if (cur_frame_time - time > 1.5 * duration) {
        /* yo dude, we've got too many frames here! Drop! DROP! */
        v4lmjpegsrc->need_writes--;     /* -= (v4lmjpegsrc->handled - (time / fps)); */
        g_signal_emit (G_OBJECT (v4lmjpegsrc),
            gst_v4lmjpegsrc_signals[SIGNAL_FRAME_DROP], 0);
      } else if (cur_frame_time - time < -1.5 * duration) {
        /* this means we're lagging far behind */
        v4lmjpegsrc->need_writes++;     /* += ((time / fps) - v4lmjpegsrc->handled); */
        g_signal_emit (G_OBJECT (v4lmjpegsrc),
            gst_v4lmjpegsrc_signals[SIGNAL_FRAME_INSERT], 0);
      }

      if (v4lmjpegsrc->need_writes > 0) {
        have_frame = TRUE;
        v4lmjpegsrc->use_num_times[num] = v4lmjpegsrc->need_writes;
        v4lmjpegsrc->need_writes--;
      } else {
        gst_v4lmjpegsrc_requeue_frame (v4lmjpegsrc, num);
      }
    } while (!have_frame);
  } else {
    /* grab a frame from the device */
    if (!gst_v4lmjpegsrc_grab_frame (v4lmjpegsrc, &num,
            &v4lmjpegsrc->last_size))
      return NULL;

    v4lmjpegsrc->use_num_times[num] = 1;
  }

  buf = gst_buffer_new ();
  GST_BUFFER_FREE_DATA_FUNC (buf) = gst_v4lmjpegsrc_buffer_free;
  GST_BUFFER_PRIVATE (buf) = v4lmjpegsrc;
  GST_BUFFER_DATA (buf) = gst_v4lmjpegsrc_get_buffer (v4lmjpegsrc, num);
  GST_BUFFER_SIZE (buf) = v4lmjpegsrc->last_size;
  GST_BUFFER_MAXSIZE (buf) = v4lmjpegsrc->breq.size;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_READONLY);
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_DONTFREE);
  if (v4lmjpegsrc->use_fixed_fps)
    GST_BUFFER_TIMESTAMP (buf) = cur_frame_time;
  else                          /* calculate time based on our own clock */
    GST_BUFFER_TIMESTAMP (buf) =
        GST_TIMEVAL_TO_TIME (v4lmjpegsrc->bsync.timestamp) -
        v4lmjpegsrc->substract_time;

  v4lmjpegsrc->handled++;
  g_signal_emit (G_OBJECT (v4lmjpegsrc),
      gst_v4lmjpegsrc_signals[SIGNAL_FRAME_CAPTURE], 0);

  return GST_DATA (buf);
}


static GstCaps *
gst_v4lmjpegsrc_getcaps (GstPad * pad)
{
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));
  struct video_capability *vcap = &GST_V4LELEMENT (v4lmjpegsrc)->vcap;
  GstCaps *caps;
  GstStructure *str;
  gint i;
  GValue w = { 0 }, h = {
  0}, w1 = {
  0}, h1 = {
  0}, fps = {
  0};

  if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lmjpegsrc))) {
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  g_value_init (&fps, GST_TYPE_FRACTION);
  gst_return_val_if_fail (gst_v4lmjpegsrc_get_fps (v4lmjpegsrc, &fps), NULL);

  caps = gst_caps_new_simple ("image/jpeg", NULL);
  str = gst_caps_get_structure (caps, 0);
  gst_structure_set_value (str, "framerate", &fps);
  g_value_unset (&fps);

  g_value_init (&w, GST_TYPE_LIST);
  g_value_init (&h, GST_TYPE_LIST);
  g_value_init (&w1, G_TYPE_INT);
  g_value_init (&h1, G_TYPE_INT);
  for (i = 0; i <= 2; i++) {
    g_value_set_int (&w1, vcap->maxwidth / (1 << i));
    g_value_set_int (&h1, vcap->maxheight / (1 << i));
    gst_value_list_append_value (&w, &w1);
    gst_value_list_append_value (&h, &h1);
  }
  g_value_unset (&h1);
  g_value_unset (&w1);
  gst_structure_set_value (str, "width", &w);
  gst_structure_set_value (str, "height", &h);
  g_value_unset (&w);
  g_value_unset (&h);

  return caps;
}


static void
gst_v4lmjpegsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4lMjpegSrc *v4lmjpegsrc;

  g_return_if_fail (GST_IS_V4LMJPEGSRC (object));
  v4lmjpegsrc = GST_V4LMJPEGSRC (object);

  switch (prop_id) {
#if 0
    case ARG_X_OFFSET:
      v4lmjpegsrc->x_offset = g_value_get_int (value);
      break;
    case ARG_Y_OFFSET:
      v4lmjpegsrc->y_offset = g_value_get_int (value);
      break;
    case ARG_F_WIDTH:
      v4lmjpegsrc->frame_width = g_value_get_int (value);
      break;
    case ARG_F_HEIGHT:
      v4lmjpegsrc->frame_height = g_value_get_int (value);
      break;
#endif
    case ARG_QUALITY:
      v4lmjpegsrc->quality = g_value_get_int (value);
      break;
    case ARG_NUMBUFS:
      v4lmjpegsrc->numbufs = g_value_get_int (value);
      break;
    case ARG_USE_FIXED_FPS:
      if (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lmjpegsrc))) {
        v4lmjpegsrc->use_fixed_fps = g_value_get_boolean (value);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lmjpegsrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4lMjpegSrc *v4lmjpegsrc;

  g_return_if_fail (GST_IS_V4LMJPEGSRC (object));
  v4lmjpegsrc = GST_V4LMJPEGSRC (object);

  switch (prop_id) {
#if 0
    case ARG_X_OFFSET:
      g_value_set_int (value, v4lmjpegsrc->x_offset);
      break;
    case ARG_Y_OFFSET:
      g_value_set_int (value, v4lmjpegsrc->y_offset);
      break;
    case ARG_F_WIDTH:
      g_value_set_int (value, v4lmjpegsrc->frame_width);
      break;
    case ARG_F_HEIGHT:
      g_value_set_int (value, v4lmjpegsrc->frame_height);
      break;
#endif
    case ARG_QUALITY:
      g_value_set_int (value, v4lmjpegsrc->quality);
      break;
    case ARG_NUMBUFS:
      if (GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lmjpegsrc)))
        g_value_set_int (value, v4lmjpegsrc->breq.count);
      else
        g_value_set_int (value, v4lmjpegsrc->numbufs);
      break;
    case ARG_BUFSIZE:
      g_value_set_int (value, v4lmjpegsrc->breq.size);
      break;
    case ARG_USE_FIXED_FPS:
      g_value_set_boolean (value, v4lmjpegsrc->use_fixed_fps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstStateChangeReturn
gst_v4lmjpegsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  GTimeVal time;

  g_return_val_if_fail (GST_IS_V4LMJPEGSRC (element), GST_STATE_CHANGE_FAILURE);

  v4lmjpegsrc = GST_V4LMJPEGSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* actual buffer set-up used to be done here - but I moved
       * it to capsnego itself */
      v4lmjpegsrc->handled = 0;
      v4lmjpegsrc->need_writes = 0;
      v4lmjpegsrc->last_frame = 0;
      v4lmjpegsrc->substract_time = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* queue all buffer, start streaming capture */
      if (GST_V4LELEMENT (v4lmjpegsrc)->buffer &&
          !gst_v4lmjpegsrc_capture_start (v4lmjpegsrc))
        return GST_STATE_CHANGE_FAILURE;
      g_get_current_time (&time);
      v4lmjpegsrc->substract_time = GST_TIMEVAL_TO_TIME (time) -
          v4lmjpegsrc->substract_time;
      v4lmjpegsrc->last_seq = 0;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      g_get_current_time (&time);
      v4lmjpegsrc->substract_time = GST_TIMEVAL_TO_TIME (time) -
          v4lmjpegsrc->substract_time;
      /* de-queue all queued buffers */
      if (v4lmjpegsrc->is_capturing &&
          !gst_v4lmjpegsrc_capture_stop (v4lmjpegsrc))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* stop capturing, unmap all buffers */
      if (GST_V4LELEMENT (v4lmjpegsrc)->buffer &&
          !gst_v4lmjpegsrc_capture_deinit (v4lmjpegsrc))
        return GST_STATE_CHANGE_FAILURE;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}


static void
gst_v4lmjpegsrc_set_clock (GstElement * element, GstClock * clock)
{
  GST_V4LMJPEGSRC (element)->clock = clock;
}


#if 0
static GstBuffer *
gst_v4lmjpegsrc_buffer_new (GstBufferPool * pool,
    guint64 offset, guint size, gpointer user_data)
{
  GstBuffer *buffer;
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC (user_data);

  if (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lmjpegsrc)))
    return NULL;

  buffer = gst_buffer_new ();
  if (!buffer)
    return NULL;

  /* TODO: add interlacing info to buffer as metadata */
  GST_BUFFER_MAXSIZE (buffer) = v4lmjpegsrc->breq.size;
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_DONTFREE);

  return buffer;
}
#endif

static void
gst_v4lmjpegsrc_buffer_free (GstBuffer * buf)
{
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC (GST_BUFFER_PRIVATE (buf));
  int n;

  if (gst_element_get_state (GST_ELEMENT (v4lmjpegsrc)) != GST_STATE_PLAYING)
    return;                     /* we've already cleaned up ourselves */

  for (n = 0; n < v4lmjpegsrc->breq.count; n++)
    if (GST_BUFFER_DATA (buf) == gst_v4lmjpegsrc_get_buffer (v4lmjpegsrc, n)) {
      v4lmjpegsrc->use_num_times[n]--;
      if (v4lmjpegsrc->use_num_times[n] <= 0) {
        gst_v4lmjpegsrc_requeue_frame (v4lmjpegsrc, n);
      }
      break;
    }

  if (n == v4lmjpegsrc->breq.count)
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, TOO_LAZY, (NULL),
        ("Couldn't find the buffer"));
}

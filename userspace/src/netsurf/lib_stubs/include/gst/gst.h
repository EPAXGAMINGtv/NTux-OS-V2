#ifndef GST_GST_H
#define GST_GST_H

#include <glib.h>

typedef struct { void *private; } GstElement;
typedef struct { void *private; } GstBus;
typedef struct { void *private; } GstMessage;
typedef struct { void *private; } GstPad;
typedef struct { void *private; } GstObject;
typedef struct { void *private; } GstCaps;

typedef void (*GstBusFunc)(void);
typedef void (*GstBusCallback)(void);

GstElement *gst_element_factory_make(const char *type, const char *name);
GstBus *gst_element_get_bus(GstElement *elem);
void gst_bus_add_watch(GstBus *bus, GstBusFunc func, void *user);
int gst_element_set_state(GstElement *elem, int state);
void gst_object_unref(void *obj);

#define GST_STATE_NULL 0
#define GST_STATE_READY 1
#define GST_STATE_PLAYING 2
#define GST_MESSAGE_EOS 1
#define GST_MESSAGE_ERROR 2

#endif

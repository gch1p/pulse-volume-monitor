/**
 * Author: Jason White (https://gist.github.com/jasonwhite/1df6ee4b5039358701d2)
 * Author: ch1p
 *
 * License: Public Domain
 */
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <pulse/pulseaudio.h>
#include <dbus/dbus.h>

#define DBUS_NAME "com.ch1p.pvm"


class DBus
{
private:
    DBusConnection *_connection;
    DBusError _error;

public:
    DBus()
        : _connection(NULL)
    {
    }

    ~DBus()
    {
        if (_connection) {
            dbus_connection_close(_connection);
        }
    }

    bool initialize()
    {
        dbus_error_init(&_error);
        _connection = dbus_bus_get(DBUS_BUS_SESSION, &_error);

        if (dbus_error_is_set(&_error)) {
            fprintf(stderr, "DBus Connection Error (%s)\n", _error.message);
            dbus_error_free(&_error);
        }

        if (!_connection) {
            fprintf(stderr, "Failed to initialize dbus connection\n");
            return false;
        }

        dbus_bus_request_name(_connection, DBUS_NAME,
            DBUS_NAME_FLAG_REPLACE_EXISTING , &_error);
        if (dbus_error_is_set(&_error)) {
            fprintf(stderr, "DBus Name Error (%s)\n", _error.message);
            dbus_error_free(&_error);
            return false;
        }

        return true;
    }

    bool notify(char *signal_name)
    {
        DBusMessage *msg;
        dbus_uint32_t serial = 0;

        //msg = dbus_message_new_signal("/com/ch1p/Object", DBUS_NAME, "valueChanged");
        msg = dbus_message_new_signal("/com/ch1p/Object", DBUS_NAME, signal_name);
        if (NULL == msg) {
            fprintf(stderr, "DBus: Message Null\n");
            return false;
        }

        if (!dbus_connection_send(_connection, msg, &serial)) {
            fprintf(stderr, "DBus send: Out Of Memory!\n");
            return false;
        }
        dbus_connection_flush(_connection);

        dbus_message_unref(msg);
        return true;
    }
};


struct pa_myuserdata {
    bool use_dbus;
    pa_mainloop_api *mainloop_api;
    DBus *dbus;
};


class PulseAudio
{
private:
    pa_mainloop* _mainloop;
    pa_mainloop_api* _mainloop_api;
    pa_context* _context;
    pa_signal_event* _signal;
    DBus _dbus;
    bool _use_dbus;
    struct pa_myuserdata _myuserdata;

public:
    PulseAudio()
        : _mainloop(NULL),
          _mainloop_api(NULL),
          _context(NULL),
          _signal(NULL),
          _use_dbus(false)
    {
    }

    /**
     * Initializes state and connects to the PulseAudio server.
     */
    bool initialize(bool use_dbus)
    {
        _mainloop = pa_mainloop_new();
        if (!_mainloop)
        {
            fprintf(stderr, "pa_mainloop_new() failed.\n");
            return false;
        }

        _mainloop_api = pa_mainloop_get_api(_mainloop);

        if (pa_signal_init(_mainloop_api) != 0)
        {
            fprintf(stderr, "pa_signal_init() failed\n");
            return false;
        }

        _signal = pa_signal_new(SIGINT, exit_signal_callback, this);
        if (!_signal)
        {
            fprintf(stderr, "pa_signal_new() failed\n");
            return false;
        }
        signal(SIGPIPE, SIG_IGN);

        _context = pa_context_new(_mainloop_api, "PulseAudio Test");
        if (!_context)
        {
            fprintf(stderr, "pa_context_new() failed\n");
            return false;
        }

        if (pa_context_connect(_context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
        {
            fprintf(stderr, "pa_context_connect() failed: %s\n", pa_strerror(pa_context_errno(_context)));
            return false;
        }

        if (use_dbus) {
            _use_dbus = true;
            if (!_dbus.initialize()) {
                return false;
            }
        }

        _myuserdata.use_dbus = _use_dbus;
        _myuserdata.mainloop_api = _mainloop_api;
        _myuserdata.dbus = &_dbus;

        pa_context_set_state_callback(_context, context_state_callback, &_myuserdata);

        return true;
    }

    /**
     * Runs the main PulseAudio event loop. Calling quit will cause the event
     * loop to exit.
     */
    int run()
    {
        int ret = 1;
        if (pa_mainloop_run(_mainloop, &ret) < 0)
        {
            fprintf(stderr, "pa_mainloop_run() failed.\n");
            return ret;
        }

        return ret;
    }

    /**
     * Exits the main loop with the specified return code.
     */
    void quit(int ret = 0)
    {
        _mainloop_api->quit(_mainloop_api, ret);
    }

    /**
     * Called when the PulseAudio system is to be destroyed.
     */
    void destroy()
    {
        if (_context)
        {
            pa_context_unref(_context);
            _context = NULL;
        }

        if (_signal)
        {
            pa_signal_free(_signal);
            pa_signal_done();
            _signal = NULL;
        }

        if (_mainloop)
        {
            pa_mainloop_free(_mainloop);
            _mainloop = NULL;
            _mainloop_api = NULL;
        }
    }

    ~PulseAudio()
    {
        destroy();
    }

private:

    /*
     * Called on SIGINT.
     */
    static void exit_signal_callback(pa_mainloop_api *m, pa_signal_event *e, int sig, void *userdata)
    {
        PulseAudio* pa = (PulseAudio*)userdata;
        if (pa) pa->quit();
    }

    /*
     * Called whenever the context status changes.
     */
    static void context_state_callback(pa_context *c, void *userdata)
    {
        struct pa_myuserdata *myuserdata = (struct pa_myuserdata *)userdata;

        assert(c && myuserdata->mainloop_api);

        PulseAudio* pa = (PulseAudio*)(myuserdata->mainloop_api);

        switch (pa_context_get_state(c))
        {
            case PA_CONTEXT_CONNECTING:
            case PA_CONTEXT_AUTHORIZING:
            case PA_CONTEXT_SETTING_NAME:
                break;

            case PA_CONTEXT_READY:
                fprintf(stderr, "PulseAudio connection established.\n");
                pa_context_get_server_info(c, server_info_callback, userdata);

                // Subscribe to sink events from the server. This is how we get
                // volume change notifications from the server.
                pa_context_set_subscribe_callback(c, subscribe_callback, userdata);
                pa_context_subscribe(c, (pa_subscription_mask_t)(PA_SUBSCRIPTION_MASK_SINK|PA_SUBSCRIPTION_MASK_SOURCE), NULL, NULL);
                break;

            case PA_CONTEXT_TERMINATED:
                pa->quit(0);
                fprintf(stderr, "PulseAudio connection terminated.\n");
                break;

            case PA_CONTEXT_FAILED:
            default:
                fprintf(stderr, "Connection failure: %s\n", pa_strerror(pa_context_errno(c)));
                pa->quit(1);
                break;
        }
    }

    /*
     * Called when an event we subscribed to occurs.
     */
    static void subscribe_callback(pa_context *c,
            pa_subscription_event_type_t type, uint32_t idx, void *userdata)
    {
        unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        //type &= PA_SUBSCRIPTION_EVENT_TYPE_MASK;

        pa_operation *op = NULL;

        switch (facility)
        {
            case PA_SUBSCRIPTION_EVENT_SINK:
                pa_context_get_sink_info_by_index(c, idx, sink_info_callback, userdata);
                break;

            case PA_SUBSCRIPTION_EVENT_SOURCE:
                pa_context_get_source_info_by_index(c, idx, source_info_callback, userdata);
                break;

            default:
                printf("Unknown event %d\n", facility);
                //assert(0); // Got event we aren't expecting.
                break;
        }

        if (op)
            pa_operation_unref(op);
    }

    /*
     * Called when the requested sink information is ready.
     */
    static void sink_info_callback(pa_context *c, const pa_sink_info *i,
            int eol, void *userdata)
    {
        if (!i) {
            return;
        }

        struct pa_myuserdata *myuserdata = (struct pa_myuserdata *)userdata;
        if (myuserdata->use_dbus) {
            myuserdata->dbus->notify("sinkChanged");
        } else {
            float volume = (float)pa_cvolume_avg(&(i->volume)) / (float)PA_VOLUME_NORM;
            printf("[sink  ] percent volume = %.0f%%%s\n", volume * 100.0f, i->mute ? " (muted)" : "");
        }
    }

    /*
     * Called when the requested source information is ready.
     */
    static void source_info_callback(pa_context *c, const pa_source_info *i,
            int eol, void *userdata)
    {
        if (!i) {
            return;
        }

        struct pa_myuserdata *myuserdata = (struct pa_myuserdata *)userdata;
        if (myuserdata->use_dbus) {
            myuserdata->dbus->notify("sourceChanged");
        } else {
            float volume = (float)pa_cvolume_avg(&(i->volume)) / (float)PA_VOLUME_NORM;
            printf("[source] percent volume = %.0f%%%s\n", volume * 100.0f, i->mute ? " (muted)" : "");
        }
    }

    /*
     * Called when the requested information on the server is ready. This is
     * used to find the default PulseAudio sink.
     */
    static void server_info_callback(pa_context *c, const pa_server_info *i,
            void *userdata)
    {
        printf("[info  ] default sink name = %s\n", i->default_sink_name);
        printf("[info  ] default source name = %s\n", i->default_source_name);
        pa_context_get_sink_info_by_name(c, i->default_sink_name, sink_info_callback, userdata);
        pa_context_get_source_info_by_name(c, i->default_source_name, source_info_callback, userdata);
    }
};



void usage(char *name)
{
    fprintf(stderr, "Usage:\n"
        "%s dbus\n"
        "%s stdout\n",
        name, name);

    exit(1);
}


int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
    }

    // Check input
    bool use_dbus = false;
    if (strcmp(argv[1], "dbus") == 0) {
        use_dbus = true;
    } else if (strcmp(argv[1], "stdout") != 0) {
        usage(argv[0]);
    }

    PulseAudio pa = PulseAudio();
    if (!pa.initialize(use_dbus)) {
        return 1;
    }

    int ret = pa.run();

    return ret;
}

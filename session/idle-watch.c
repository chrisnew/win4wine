/* Tell the session supervisor when the user has been idle.
 *
 * The XP session starts its screen savers itself (session/screen.py). This
 * helper follows the X server's IDLETIME counter (XSync) with alarms, so it
 * sleeps in poll() and wakes only when the idle time is reached and when the
 * user comes back: an idle machine is not woken up. Programs that call
 * XResetScreenSaver (mpv, VLC, Wine's dinput for a joystick) reset the
 * counter and so hold the screen saver off.
 *
 * The X server's own blanking is turned off, as the session replaces it, and
 * the monitor's power saving (DPMS) is set as the session asks; the X server
 * then does that by itself.
 *
 *   stdin:  idle SECONDS   report "idle" after SECONDS without input
 *           off            report nothing
 *           dpms SECONDS   turn the monitor off after SECONDS; 0: never
 *   stdout: idle           the idle time was reached
 *           active         input came after "idle"
 *
 * It exits when stdin ends or the X server goes away. */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/sync.h>

/* The longest idle time, so that it fits in the alarm's milliseconds */
#define LONGEST (24 * 60 * 60)

static Display *display;
static XSyncCounter counter;
static XSyncAlarm alarm_id = None;
static int alarm_event;
static int timeout_ms;  /* 0: off */
static int idle;        /* "idle" was reported, and "active" not yet */

static void report(const char *word)
{
    puts(word);
    fflush(stdout);
}

static long long idle_time(void)
{
    XSyncValue value;
    if (!XSyncQueryCounter(display, counter, &value)) return 0;
    return ((long long)XSyncValueHigh32(value) << 32) | XSyncValueLow32(value);
}

static void disarm(void)
{
    if (alarm_id != None) XSyncDestroyAlarm(display, alarm_id);
    alarm_id = None;
}

/* One alarm for the counter crossing MS upwards or downwards. */
static void set_alarm(XSyncTestType test, int ms)
{
    XSyncAlarmAttributes attributes;
    disarm();
    attributes.trigger.counter = counter;
    attributes.trigger.value_type = XSyncAbsolute;
    attributes.trigger.test_type = test;
    XSyncIntToValue(&attributes.trigger.wait_value, ms);
    XSyncIntToValue(&attributes.delta, 0);
    attributes.events = True;
    alarm_id = XSyncCreateAlarm(display, XSyncCACounter | XSyncCAValueType | XSyncCATestType | XSyncCAValue |
                                XSyncCADelta | XSyncCAEvents, &attributes);
}

/* Wait for the next change. The counter is read after the alarm is set: an
 * alarm only sees the counter cross its value, which it may just have done. */
static void arm(void)
{
    for (;;)
    {
        if (idle)
        {
            set_alarm(XSyncNegativeTransition, timeout_ms - 1);
            if (idle_time() >= timeout_ms) return;
            idle = 0;
            report("active");
        }
        else
        {
            if (!timeout_ms)
            {
                disarm();
                return;
            }
            set_alarm(XSyncPositiveTransition, timeout_ms);
            if (idle_time() < timeout_ms) return;
            idle = 1;
            report("idle");
        }
    }
}

static void set_dpms(int seconds)
{
    int event, error;
    if (!DPMSQueryExtension(display, &event, &error) || !DPMSCapable(display))
    {
        if (seconds) fprintf(stderr, "idle-watch: the display has no power saving (DPMS)\n");
        return;
    }
    if (seconds) DPMSEnable(display);
    DPMSSetTimeouts(display, 0, 0, seconds);
}

static void command(char *line)
{
    char *end, *value = strchr(line, ' ');
    long seconds = 0;
    if (value)
    {
        *value++ = 0;
        errno = 0;
        seconds = strtol(value, &end, 10);
        if (errno || end == value || *end || seconds < 0) value = NULL;
        if (seconds > LONGEST) seconds = LONGEST;
    }
    if (!strcmp(line, "idle") && value)
    {
        /* A new time starts afresh: "idle" is reported again if it is reached. */
        idle = 0;
        timeout_ms = seconds * 1000;
        arm();
    }
    else if (!strcmp(line, "off"))
    {
        idle = 0;
        timeout_ms = 0;
        disarm();
    }
    else if (!strcmp(line, "dpms") && value) set_dpms(seconds);
    else fprintf(stderr, "idle-watch: unknown command %s\n", line);
}

int main(void)
{
    int sync_error, major, minor, count, blank, interval, prefer, exposures, i;
    XSyncSystemCounter *counters;
    char input[256];
    size_t used = 0;

    if (!(display = XOpenDisplay(NULL)))
    {
        fprintf(stderr, "idle-watch: cannot open the display\n");
        return 1;
    }
    if (!XSyncQueryExtension(display, &alarm_event, &sync_error) || !XSyncInitialize(display, &major, &minor))
    {
        fprintf(stderr, "idle-watch: the X server has no SYNC extension\n");
        return 1;
    }
    alarm_event += XSyncAlarmNotify;
    counters = XSyncListSystemCounters(display, &count);
    for (i = 0; i < count; i++)
        if (!strcmp(counters[i].name, "IDLETIME")) counter = counters[i].counter;
    if (counters) XSyncFreeSystemCounterList(counters);
    if (!counter)
    {
        fprintf(stderr, "idle-watch: the X server has no IDLETIME counter\n");
        return 1;
    }
    XGetScreenSaver(display, &blank, &interval, &prefer, &exposures);
    XSetScreenSaver(display, 0, interval, prefer, exposures);

    for (;;)
    {
        struct pollfd fds[2] = {{ConnectionNumber(display), POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};

        /* Replies to the requests above may have brought events with them. */
        while (XPending(display))
        {
            XEvent event;
            XSyncAlarmNotifyEvent *notify = (XSyncAlarmNotifyEvent *)&event;
            XNextEvent(display, &event);
            if (event.type != alarm_event || notify->alarm != alarm_id || notify->state == XSyncAlarmDestroyed)
                continue;
            idle = !idle;
            report(idle ? "idle" : "active");
            arm();
        }
        XFlush(display);
        if (poll(fds, 2, -1) < 0)
        {
            if (errno == EINTR) continue;
            perror("idle-watch: poll");
            return 1;
        }
        if (fds[1].revents)
        {
            ssize_t n = read(STDIN_FILENO, input + used, sizeof(input) - 1 - used);
            char *start = input, *end;
            if (n <= 0) return 0;  /* the supervisor is gone */
            used += n;
            input[used] = 0;
            while ((end = strchr(start, '\n')))
            {
                *end = 0;
                command(start);
                start = end + 1;
            }
            used = strlen(start);
            memmove(input, start, used);
            if (used == sizeof(input) - 1) used = 0;  /* no command is that long */
        }
    }
}

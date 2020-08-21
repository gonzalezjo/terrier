#pragma once

#include <libev/event.h>

#include "common/error/exception.h"

namespace terrier {

/**
 * Static utility class with wrappers for libevent functions.
 *
 * Wrapper functions are functions with the same signature and return
 * value as the c-style functions, but consist of an extra return value
 * checking. An exception is thrown instead if something is wrong. Wrappers
 * are great tools for using legacy code in a modern code base.
 */
class EventUtil {
 private:
  template <typename T>
  static bool NotNull(T *ptr) {
    return ptr != nullptr;
  }

  static bool IsZero(int arg) { return arg == 0; }

  static bool NonNegative(int arg) { return arg >= 0; }

  template <typename T>
  static T Wrap(T value, bool (*check)(T), const char *error_msg) {
    if (!check(value)) throw NETWORK_PROCESS_EXCEPTION(error_msg);
    return value;
  }

 public:
  EventUtil() = delete;

  /**
   * @return A new event_base
   */
  static struct event_base *EventBaseNew() {
    return Wrap(event_base_new(), NotNull<struct event_base>, "Can't allocate event base");
  }

  /**
   * @param base The event_base to exit
   * @param timeout
   * @return a positive integer or an exception is thrown on failure
   */
  static int EventBaseLoopExit(struct event_base *base, struct ev_loop *loop, struct timeval *timeout) {
    ev_break(loop, EVBREAK_ONE);
    return 1;
  }

  /**
   * @param event The event to delete
   * @return a positive integer or an exception is thrown on failure
   */
  static int EventDel(struct event *event) { return Wrap(event_del(event), IsZero, "Error when deleting event"); }

  /**
   * @param event The event to add
   * @param timeout
   * @return a positive integer or an exception is thrown on failure
   */
  static int EventAdd(struct event *event, struct timeval *timeout) {
    return Wrap(event_add(event, timeout), IsZero, "Error when adding event");
  }

  /**
   * @brief Allocates a callback event
   * @param event The event being assigned
   * @param base The event_base
   * @param fd The filedescriptor assigned to the event
   * @param flags Flags for the event
   * @param callback Callback function to fire when the event is triggered
   * @param arg Argument to pass to the callback function
   * @return a positive integer or an exception is thrown on failure
   */
  static int EventAssign(struct event *event, struct event_base *base, int fd, int16_t flags,
                         event_callback_fn callback, void *arg) {
    event_set(event, fd, flags, callback, arg);
    return Wrap(event_base_set(base, event), IsZero, "Error when assigning event");
  }

  /**
   * @brief Runs event base dispatch loop
   * @param loop The ev_loop to dispatch on
   * @return a positive integer or an exception is thrown on failure
   */
  static int EventBaseDispatch(struct ev_loop* loop) {
//    return Wrap(event_base_dispatch(base), NonNegative, "Error in event base dispatch");
      return ev_run(loop, 0);
  }
};
}  // namespace terrier

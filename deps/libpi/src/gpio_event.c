/*
 * libpi - GPIO Events
 * Copyright(c) 2013 Jake Luer <jake@alogicalparadox.com>
 * MIT Licensed
 */

#include "pi.h"
#include "common.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Debug macro
 */

#define debug(fmt, args...) \
  pi__debug_print(fmt, "gpio_event", ##args)

/*
 * Gpio sysfs address
 */

#define SYSFS_GPIO_DIR "/sys/class/gpio"
#define MAX_BUF 64

/*
 * Write a value to a given file.
 */

static int
pi__gpio_write(pi_gpio_handle_t *listener, char *path, char *str) {
  debug("(%s) %s", path, str);
  char filename[MAX_BUF];
  snprintf(filename, sizeof(filename), SYSFS_GPIO_DIR "%s", path);
  int fd = open(filename, O_WRONLY);

  if (fd < 0) {
    listener->error = 1;
    return -1;
  }

  write(fd, str, sizeof(str));
  close(fd);
  return 0;
}

/*
 * Export a GPIO pin.
 */

static int
pi__gpio_export(pi_gpio_handle_t *listener) {
  debug("(%i)", listener->gpio);
  char gpio[3];
  int res;
  snprintf(gpio, sizeof(gpio), "%d", listener->gpio);
  res = pi__gpio_write(listener, "/export", gpio);
  return res;
}

/*
 * Unexport a GPIO pin.
 */

static int
pi__gpio_unexport(pi_gpio_handle_t *listener) {
  debug("(%i)", listener->gpio);
  char str[3];
  int res;
  snprintf(str, sizeof(str), "%d", listener->gpio);
  res = pi__gpio_write(listener, "/unexport", str);
  return res;
}

/*
 * Set direction for a GPIO pin.
 */

static int
pi__gpio_set_direction(pi_gpio_handle_t *listener, pi_gpio_direction_t direction) {
  char path[MAX_BUF];
  char str[MAX_BUF];

  switch (direction) {
    case PI_DIR_IN:
      debug("(%i) in", (int)listener->gpio);
      strcpy(str, "in");
      break;
    case PI_DIR_OUT:
      debug("(%i) out", (int)listener->gpio);
      strcpy(str, "out");
      break;
  }

  snprintf(path, sizeof(path), "/gpio%d/direction", listener->gpio);
  int res = pi__gpio_write(listener, path, str);
  return res;
}

/*
 * Set edge for a input pin.
 */

static int
pi__gpio_set_edge(pi_gpio_handle_t *listener, pi_gpio_edge_t edge) {
  char path[MAX_BUF];
  char str[MAX_BUF];
  int res;

  switch (edge) {
    case PI_EDGE_NONE:
      debug("(%i) none", listener->gpio);
      strcpy(str, "none");
      break;
    case PI_EDGE_RISING:
      debug("(%i) rising", listener->gpio);
      strcpy(str, "rising");
      break;
    case PI_EDGE_FALLING:
      debug("(%i) falling", listener->gpio);
      strcpy(str, "falling");
      break;
    case PI_EDGE_BOTH:
      debug("(%i) both", listener->gpio);
      strcpy(str, "both");
      break;
  }

  snprintf(path, sizeof(path), "/gpio%d/edge", listener->gpio);
  res = pi__gpio_write(listener, path, str);
  return res;
}

/*
 * Create a new listener for GPIO pin. Exports pin in sysfs.
 */

pi_gpio_handle_t*
pi_gpio_listener_claim(pi_gpio_pin_t gpio) {
  debug("(%i)", (int)gpio);
  pi_gpio_handle_t *listener = malloc(sizeof(pi_gpio_handle_t));
  listener->gpio = gpio;
  listener->error = 0;
  int r_exp = pi__gpio_export(listener);
  if (r_exp < 0) return listener;
  pi__gpio_set_direction(listener, PI_DIR_IN);
  return listener;
}

/*
 * Release listener for GPIO pin. Unexports pin in sysfs.
 */

void
pi_gpio_listener_release(pi_gpio_handle_t* listener) {
  debug("(%i)", (int)listener->gpio);
  pi__gpio_unexport(listener);
  free(listener);
}

/*
 * Listen for an event on specified edge. Blocking. If being used
 * in threaded environment, avoid more than one listener per pin.
 */

int
pi_gpio_listen(pi_gpio_handle_t *listener, pi_gpio_edge_t edge) {
  char path[MAX_BUF];
  char value[16];

  pi__gpio_set_edge(listener, edge);

  snprintf(path, sizeof(path), SYSFS_GPIO_DIR "/gpio%d/value", listener->gpio);
  int fd = open(path, O_RDONLY);
  if (fd < 0 ) return -1;

  ssize_t size = read(fd, value, sizeof(value));
  if (size < 0) {
    close(fd);
    return -1;
  }

  int res;
  struct pollfd pfd[1];
  pfd[0].fd = fd;
  pfd[0].events = POLLPRI | POLLERR;
  pfd[0].revents = 0;

  debug("(%i) start", (int)listener->gpio);
  int event = poll(pfd, 1, -1);

  if (event == 1 && pfd[0].revents & POLLPRI) {
    off_t offset = lseek(fd, 0, SEEK_SET);
    if (offset < 0) {
      res = -1;
    } else {
      ssize_t size = read(fd, value, sizeof(value));
      if (size < 1) res = -1;
      else if (strcmp(&value[0], "0")) res = PI_GPIO_LOW;
      else if (strcmp(&value[0], "1")) res = PI_GPIO_HIGH;
      else res = -1;
    }
  } else {
    res = -1;
  }

  close(fd);
  debug("(%i) return %i", (int)listener->gpio, (int)res);
  return res;
}



/*!
 * Environment Includes
 */

#include <v8.h>
#include <node.h>
#include <string.h>

/*!
 * API Includes
 */

#include "gpio.h"
#include <pi.h>

/*!
 * Namespaces
 */

using namespace v8;
using namespace node;

/*!
 * Error Macros
 */

#define ERROR(msg) \
  ThrowException(Exception::Error(String::New(msg)))

#define TYPE_ERROR(msg) \
  ThrowException(Exception::TypeError(String::New(msg)))

/*!
 * v8 Function Template
 */

Persistent<FunctionTemplate> GPIO::constructor;

/**
 * Initialize a new function template and create
 * JavaScript Prototype object.
 *
 * @param {v8::Object} target
 */

void
GPIO::Initialize(Handle<Object> target) {
  HandleScope scope;

  // Constructor
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  constructor = Persistent<FunctionTemplate>::New(tpl);
  constructor->InstanceTemplate()->SetInternalFieldCount(1);
  constructor->SetClassName(String::NewSymbol("GPIO"));

  // Prototype (Methods)
  SetPrototypeMethod(constructor, "setup", Setup);
  SetPrototypeMethod(constructor, "destroy", Destroy);
  SetPrototypeMethod(constructor, "claim", PinClaim);
  SetPrototypeMethod(constructor, "release", PinRelease);
  SetPrototypeMethod(constructor, "stat", PinStat);
  SetPrototypeMethod(constructor, "setDirection", PinSetDirection);
  //SetPrototypeMethod(constructor, "setPull", SetPinPull);
  SetPrototypeMethod(constructor, "read", PinRead);
  SetPrototypeMethod(constructor, "write", PinWrite);

  // Prototype (Getters/Setters)
  // Local<ObjectTemplate> proto = constructor->PrototypeTemplate();

  // Export
  target->Set(String::NewSymbol("GPIO"), constructor->GetFunction());
}

/**
 * Constructor for new GPIO instance.
 *
 * @param {Arguments} args
 * @return {Value} v8 argument holder (this)
 */

Handle<Value>
GPIO::New(const Arguments &args) {
  HandleScope scope;
  GPIO *self = new GPIO();
  self->Wrap(args.This());
  return scope.Close(args.Holder());
}

pi_gpio_direction_t
PiDirection(const Handle<String> &v8str) {
  String::AsciiValue str(v8str);
  if (!strcasecmp(*str, "out")) return PI_DIR_OUT;
  if (!strcasecmp(*str, "in")) return PI_DIR_IN;
  return PI_DIR_IN;
}

pi_gpio_pull_t
PiPull(const Handle<String> &v8str) {
  String::AsciiValue str(v8str);
  if (!strcasecmp(*str, "up")) return PI_PULL_UP;
  if (!strcasecmp(*str, "down")) return PI_PULL_DOWN;
  if (!strcasecmp(*str, "none")) return PI_PULL_NONE;
  return PI_PULL_NONE;
}

/**
 * Initialize the GPIO interface asyncronously.
 *
 * @param {Arguments} args
 * @arg {Function} callback (required)
 * @return {Undefined}
 */

Handle<Value>
GPIO::Setup(const Arguments &args) {
  HandleScope scope;

  int len = args.Length();
  if (len < 1) return TYPE_ERROR("Callback required.");
  if (!args[0]->IsFunction()) return TYPE_ERROR("First argument must be a function.");

  Local<Function> callback = Local<Function>::Cast(args[0]);
  GPIO *self = ObjectWrap::Unwrap<GPIO>(args.Holder());

  Baton *baton = new Baton();
  baton->req.data = baton;
  baton->self = self;
  baton->cb = Persistent<Function>::New(callback);

  uv_queue_work(
    uv_default_loop(),
    &baton->req,
    SetupWork,
    (uv_after_work_cb)SetupAfter
  );

  return Undefined();
}

void
GPIO::SetupWork(uv_work_t *req) {
  Baton* baton = static_cast<Baton*>(req->data);
  GPIO* self = static_cast<GPIO*>(baton->self);

  if (!self->active) {
    pi_closure_t *closure = pi_closure_new();
    int success = pi_gpio_setup(closure);

    if (success < 0) {
      pi_closure_delete(closure);
      //return ERROR("gpio setup failed: are you root?");
    }

    self->closure = closure;
    self->active = true;
  }
}

void
GPIO::SetupAfter(uv_work_t *req, int status) {
  Baton* baton = static_cast<Baton*>(req->data);

  Local<Value> argv[1] = {
    Local<Value>::New(Null())
  };

  TryCatch try_catch;
  baton->cb->Call(Context::GetCurrent()->Global(), 1, argv);
  if (try_catch.HasCaught()) FatalException(try_catch);

  baton->cb.Dispose();
  delete baton;
}


Handle<Value>
GPIO::Destroy(const Arguments &args) {
  HandleScope scope;

  int len = args.Length();
  if (len < 1) return TYPE_ERROR("Callback required.");
  if (!args[0]->IsFunction()) return TYPE_ERROR("First argument must be a function.");

  Local<Function> callback = Local<Function>::Cast(args[0]);
  GPIO *self = ObjectWrap::Unwrap<GPIO>(args.Holder());

  Baton *baton = new Baton();
  baton->req.data = baton;
  baton->self = self;
  baton->cb = Persistent<Function>::New(callback);

  uv_queue_work(
    uv_default_loop(),
    &baton->req,
    DestroyWork,
    (uv_after_work_cb)DestroyAfter
  );

  return Undefined();
}

void
GPIO::DestroyWork(uv_work_t *req) {
  Baton* baton = static_cast<Baton*>(req->data);
  GPIO* self = static_cast<GPIO*>(baton->self);

  if (self->active) {
    for (int i = 0; i < PI_MAX_PINS; i++) {
      if (self->pins[i] != NULL) {
        pi_gpio_release(self->pins[i]);
        self->pins[i] = NULL;
      }
    }

    pi_gpio_teardown(self->closure);
    pi_closure_delete(self->closure);
    self->closure = NULL;
    self->active = false;
  }
}

void
GPIO::DestroyAfter(uv_work_t *req, int status) {
  Baton* baton = static_cast<Baton*>(req->data);

  Local<Value> argv[1] = {
    Local<Value>::New(Null())
  };

  TryCatch try_catch;
  baton->cb->Call(Context::GetCurrent()->Global(), 1, argv);
  if (try_catch.HasCaught()) FatalException(try_catch);

  baton->cb.Dispose();
  delete baton;
}

Handle<Value>
GPIO::PinClaim(const Arguments &args) {
  HandleScope scope;
  GPIO *self = ObjectWrap::Unwrap<GPIO>(args.Holder());

  int len = args.Length();
  pi_gpio_direction_t direction = PI_DIR_IN;
  pi_gpio_pull_t pull = PI_PULL_NONE;

  if (len < 1) return TYPE_ERROR("gpio pin required");
  if (!args[0]->IsUint32()) return TYPE_ERROR("gpio pin must be a number");

  pi_gpio_pin_t gpio = args[0]->Int32Value();
  if (self->pins[gpio] != NULL) return ERROR("gpio pin already claimed");

  if (len > 1) {
    if (!args[1]->IsString()) return TYPE_ERROR("direction must be a string");
    direction = PiDirection(args[1]->ToString());
  }

  if (direction == PI_DIR_IN && len > 2) {
    if (!args[2]->IsString()) return TYPE_ERROR("pull must be a string");
    pull = PiPull(args[2]->ToString());
  }

  pi_gpio_handle_t *handle = pi_gpio_claim(self->closure, gpio);
  self->pins[gpio] = handle;

  if (direction != PI_DIR_IN) {
    pi_gpio_set_direction(handle, direction);
  }

  if (direction == PI_DIR_IN && pull != PI_PULL_NONE) {
    pi_gpio_set_pull(handle, pull);
  }

  return scope.Close(args.Holder());
}


Handle<Value>
GPIO::PinRelease(const Arguments &args) {
  HandleScope scope;
  GPIO *self = ObjectWrap::Unwrap<GPIO>(args.Holder());

  int len = args.Length();
  if (len < 1) return TYPE_ERROR("gpio pin required");
  if (!args[0]->IsUint32()) return TYPE_ERROR("gpio pin must be a number");

  pi_gpio_pin_t gpio = args[0]->Int32Value();
  if (self->pins[gpio] == NULL) return ERROR("gpio pin has not been claimed");

  pi_gpio_handle_t *handle = self->pins[gpio];
  pi_gpio_release(handle);
  self->pins[gpio] = NULL;

  // TODO: Error checking
  return scope.Close(args.Holder());
}

Handle<Value>
GPIO::PinStat(const Arguments &args) {
  HandleScope scope;
  GPIO *self = ObjectWrap::Unwrap<GPIO>(args.Holder());

  int len = args.Length();
  if (len < 1) return TYPE_ERROR("gpio pin required");
  if (!args[0]->IsUint32()) return TYPE_ERROR("gpio pin must be a number");

  Local<Object> res = Object::New();
  pi_gpio_pin_t gpio = args[0]->Int32Value();
  bool exist = self->pins[gpio] == NULL ? false : true;
  pi_gpio_direction_t direction;

  res->Set(String::New("pin"), Number::New(gpio));
  res->Set(String::New("claimed"), Boolean::New(exist));

  if (exist) {
    pi_gpio_handle_t *handle = self->pins[gpio];
    direction = pi_gpio_get_direction(handle);
    char str[120];

    switch (direction) {
      case PI_DIR_IN:
        strcpy(str, "in");
        break;
      case PI_DIR_OUT:
        strcpy(str, "out");
        break;
    }

    res->Set(String::New("direction"), String::New(str));
  } else {
    res->Set(String::New("direction"), Null());
  }

  return scope.Close(res);
}

/*
char *get(v8::Local<v8::Value> value, const char *fallback = "") {
  if (value->IsString()) {
      v8::String::AsciiValue string(value);
      char *str = (char *) malloc(string.length() + 1);
      strcpy(str, *string);
      return str;
  }
  char *str = (char *) malloc(strlen(fallback) + 1);
  strcpy(str, fallback);
  return str;
}
*/

Handle<Value>
GPIO::PinSetDirection(const Arguments &args) {
  HandleScope scope;
  GPIO *self = ObjectWrap::Unwrap<GPIO>(args.Holder());

  int len = args.Length();
  if (len < 1) return TYPE_ERROR("gpio pin required");
  if (!args[0]->IsUint32()) return TYPE_ERROR("gpio pin must be a number");
  if (len < 2) return TYPE_ERROR("gpio direction required");
  if (!args[1]->IsString()) return TYPE_ERROR("gpio direction must be a number");

  pi_gpio_pin_t gpio = args[0]->Int32Value();
  if (!self->pins[gpio]) return ERROR("gpio pin has not been claimed");

  pi_gpio_handle_t *handle = self->pins[gpio];
  pi_gpio_direction_t direction = PiDirection(args[1]->ToString());
  pi_gpio_set_direction(handle, direction);

  // TODO: Error checking
  return scope.Close(args.Holder());
}

Handle<Value>
GPIO::PinRead(const Arguments &args) {
  HandleScope scope;
  GPIO *self = ObjectWrap::Unwrap<GPIO>(args.Holder());

  int len = args.Length();
  if (len < 1) return TYPE_ERROR("gpio pin required");
  if (!args[0]->IsUint32()) return TYPE_ERROR("gpio pin must be a number");

  pi_gpio_pin_t gpio = args[0]->Int32Value();
  if (!self->pins[gpio]) return ERROR("gpio pin has not been claimed");

  pi_gpio_handle_t *handle = self->pins[gpio];
  pi_gpio_value_t value = pi_gpio_read(handle);

  return scope.Close(Number::New(value));
}

Handle<Value>
GPIO::PinWrite(const Arguments &args) {
  HandleScope scope;
  GPIO *self = ObjectWrap::Unwrap<GPIO>(args.Holder());

  int len = args.Length();
  if (len < 1) return TYPE_ERROR("gpio pin required");
  if (!args[0]->IsUint32()) return TYPE_ERROR("gpio pin must be a number");
  if (len < 2) return TYPE_ERROR("gpio value required");
  if (!args[1]->IsUint32()) return TYPE_ERROR("gpio value must be a number");

  pi_gpio_pin_t gpio = args[0]->Int32Value();
  if (!self->pins[gpio]) return ERROR("gpio pin has not been claimed");

  pi_gpio_handle_t *handle = self->pins[gpio];
  pi_gpio_value_t value;

  // TODO: error if not 0 or 1
  switch (args[1]->Int32Value()) {
    case 0:
      value = PI_GPIO_LOW;
      break;
    default:
      value = PI_GPIO_HIGH;
      break;
  }

  pi_gpio_write(handle, value);
  return scope.Close(args.Holder());
}

GPIO::GPIO () {
  active = false;
  closure = NULL;

  for (int i = 0; i < PI_MAX_PINS; i++) {
    pins[i] = NULL;
  }
};

GPIO::~GPIO() {};

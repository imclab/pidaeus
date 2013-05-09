
#ifndef __NODE_GPIO_H__
#define __NODE_GPIO_H__

#include <v8.h>
#include <node.h>
#include <pi.h>

#define PI_MAX_PINS 31

using namespace v8;
using namespace node;

class GPIO: public ObjectWrap {
  public:
    static void Initialize(Handle<Object> target);
    pi_closure_t *closure;
    pi_gpio_handle_t *pins[PI_MAX_PINS];
    bool active;

  private:
    GPIO ();
    ~GPIO ();

    static Persistent<FunctionTemplate> constructor;
    static Handle<Value> New(const Arguments &args);
    static Handle<Value> Setup(const Arguments &args);
    static Handle<Value> Teardown(const Arguments &args);
    static Handle<Value> PinStat(const Arguments &args);
    static Handle<Value> PinClaim(const Arguments &args);
    static Handle<Value> PinRelease(const Arguments &args);
    static Handle<Value> PinSetDirection(const Arguments &args);
    //static Handle<Value> GetPinDirection(const Arguments &args);
    //static Handle<Value> SetPinPull(const Arguments &args);
    //static Handle<Value> ReadPin(const Arguments &args);
    //static Handle<Value> WritePin(const Arguments &args);
};

#endif

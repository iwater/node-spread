#define MAX_MESSLEN     102400
#define MAX_VSSETS      10
#define MAX_MEMBERS     100

#include <string.h>

#include <v8.h>
#include <node.h>
#include <node_events.h>
#include <assert.h>

extern "C" {
  #include "/opt/soft/spread/include/sp.h"
}

using namespace v8;
using namespace node;

static Persistent<String> connected_symbol;
static Persistent<String> closed_symbol;
static Persistent<String> error_symbol;
static Persistent<String> message_symbol;

class Connection : public EventEmitter {
  public:
    static void
    Initialize(v8::Handle<v8::Object> target) {
      Local<FunctionTemplate> t = FunctionTemplate::New(Connection::New);
      t->Inherit(EventEmitter::constructor_template);
      t->InstanceTemplate()->SetInternalFieldCount(1);

      closed_symbol = NODE_PSYMBOL("closed");
      connected_symbol = NODE_PSYMBOL("connected");
      error_symbol = NODE_PSYMBOL("error");
      message_symbol = NODE_PSYMBOL("message");

      NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
      NODE_SET_PROTOTYPE_METHOD(t, "close", Close);
      NODE_SET_PROTOTYPE_METHOD(t, "multicast", Multicast);
      NODE_SET_PROTOTYPE_METHOD(t, "join", Join);

      target->Set(String::NewSymbol("Connection"), t->GetFunction());
    }

    void Connect(const char *Spread_name, const char *User) {
      HandleScope scope;

      int ret = SP_connect( Spread_name, User, 0, 1, &Mbox, Private_group );

      Emit((ret<0 ? error_symbol : connected_symbol), 0, NULL);
      Read_message();
    }

    void Close() {
      HandleScope scope;
      SP_disconnect(Mbox);
      Emit(closed_symbol, 0, NULL);
    }

    void Join(const char *Channel) {
      HandleScope scope;
      SP_join( Mbox, Channel );
    }

    void Multicast(const char *Channel, const char *Message) {
      HandleScope scope;

      int ret = SP_multicast(Mbox, UNRELIABLE_MESS, Channel, 1, strlen(Message), Message);

      if(ret < 0){
        Local<Value> args[1];
        args[0] = Local<Value>::New(Integer::New(ret));
        Emit(error_symbol, 1, args);
      }
    }

  protected:
    static Handle<Value> New(const Arguments& args) {
      HandleScope scope;

      Connection *connection = new Connection();
      connection->Wrap(args.This());

      return args.This();
    }

    static Handle<Value> Connect(const Arguments &args) {
      HandleScope scope;

      if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        return ThrowException(
                Exception::TypeError(
                    String::New("Required argument: spread name or user.")));
      }

      String::Utf8Value Spread_name(args[0]->ToString());
      String::Utf8Value User(args[1]->ToString());

      Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
      connection->Connect(*Spread_name, *User);

      return Undefined();
    }

    static Handle<Value> Close(const Arguments &args) {
      HandleScope scope;

      Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
      connection->Close();

      return Undefined();
    }

    static Handle<Value> Multicast(const Arguments &args) {
      HandleScope scope;

      if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        return ThrowException(
                Exception::TypeError(
                    String::New("Required argument: spread name or user.")));
      }

      String::Utf8Value Channel(args[0]->ToString());
      String::Utf8Value Message(args[1]->ToString());

      Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
      connection->Multicast(*Channel, *Message);

      return Undefined();
    }

    static Handle<Value> Join(const Arguments &args) {
      HandleScope scope;

      if (args.Length() < 1 || !args[0]->IsString()) {
        return ThrowException(
                Exception::TypeError(
                    String::New("Required argument: spread name or user.")));
      }

      String::Utf8Value Channel(args[0]->ToString());

      Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
      connection->Join(*Channel);

      return Undefined();
    }

  private:
    char    Private_group[20];
    mailbox Mbox;

    void Read_message(){
        char             mess[MAX_MESSLEN];
        char             sender[MAX_GROUP_NAME];
        char             target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
        int              num_groups;
        int              service_type;
        int16            mess_type;
        int              endian_mismatch;
        int              ret;

      while(true){

        ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups, 
                &mess_type, &endian_mismatch, sizeof(mess), mess );

        if (ret < 0 ){
          Emit(error_symbol, 0, NULL);
        }else if( Is_regular_mess( service_type ) ){
          mess[ret] = 0;
          Local<Value> args[1];
          args[0] = Local<Value>::New(String::New(mess));
          Emit(message_symbol, 1, args);
        }else{
          mess[ret] = 0;
          Local<Value> args[2];
          args[0] = Local<Value>::New(String::New(mess));
          args[1] = Local<Value>::New(Integer::New(service_type));
          Emit(message_symbol, 2, args);
        }
      }
    }
};

extern "C" void
init(Handle<Object> target) {
  HandleScope scope;
  Connection::Initialize(target);
}

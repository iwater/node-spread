#define MAX_MESSLEN    102400
#define MAX_GROUP_NAME 32
#define MAX_MEMBERS    100

#include <v8.h>
#include <node.h>
#include <node_events.h>
#include <string.h>

#include <unistd.h>
#include <cstdlib>
#include <cstring>

extern "C" {
  #include "/opt/soft/spread/include/sp.h"
}

using namespace v8;
using namespace node;

#define REQ_FUN_ARG(I, VAR)                                             \
  if (args.Length() <= (I) || !args[I]->IsFunction())                   \
    return ThrowException(Exception::TypeError(                         \
                  String::New("Argument " #I " must be a function")));  \
  Local<Function> VAR = Local<Function>::Cast(args[I]);

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
      NODE_SET_PROTOTYPE_METHOD(t, "readSync", ReadSync);
      NODE_SET_PROTOTYPE_METHOD(t, "read", Read);

      target->Set(String::NewSymbol("Connection"), t->GetFunction());
    }

    void Connect(const char *Spread_name, const char *User) {
      HandleScope scope;

      int ret = SP_connect( Spread_name, User, 0, 1, &Mbox, Private_group );

      Emit((ret<0 ? error_symbol : connected_symbol), 0, NULL);

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

    Local<String> ReadSync(int* ret) {
        char             mess[MAX_MESSLEN];
        char             sender[MAX_GROUP_NAME];
        char             target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
        int              num_groups;
        int              service_type;
        int16            mess_type;
        int              endian_mismatch;
      
      while(SP_poll(Mbox)>0){

        *ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups,
                &mess_type, &endian_mismatch, sizeof(mess), mess );

        if (*ret < 0 ){
          Emit(error_symbol, 0, NULL);
        }else if( Is_regular_mess( service_type ) ){
          mess[*ret] = 0;
          return String::New(mess);
        }else{
          mess[*ret] = 0;
          Local<Value> args[2];
          args[0] = Local<Value>::New(String::New(mess));
          args[1] = Local<Value>::New(Integer::New(service_type));
          Emit(message_symbol, 2, args);
        }

        *ret = 0;
      }
      return String::New("");
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

    static Handle<Value> ReadSync(const Arguments &args) {
      HandleScope scope;
      Local<Value> result;
      int ret = 0;

      String::Utf8Value Channel(args[0]->ToString());

      Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

      result = connection->ReadSync(&ret);

      if(ret > 0) {
        return scope.Close( result );
      }
      return Undefined();
    }

  struct read_baton_t {
    Connection *hw;
    Local<Value> message;
    Persistent<Function> cb;
  };

    static Handle<Value> Read(const Arguments &args) {
      HandleScope scope;

      REQ_FUN_ARG(0, cb);

      Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

      //read_baton_t *baton = new read_baton_t();
      read_baton_t *baton = (read_baton_t *)malloc(sizeof(*baton));
      baton->hw = connection;
      baton->cb = Persistent<Function>::New(cb);

      connection->Ref();

      eio_custom(EIO_Read, EIO_PRI_DEFAULT, EIO_AfterRead, baton);
      ev_ref(EV_DEFAULT_UC);

      return Undefined();
    }

  static int EIO_Read(eio_req *req)
  {
    read_baton_t *baton = static_cast<read_baton_t *>(req->data);

    int   ret = 0;
    char  mess[10240];
    char  sender[MAX_GROUP_NAME];
    char  target_groups[100][MAX_GROUP_NAME];
    int   num_groups;
    int   service_type;
    int16 mess_type;
    int   endian_mismatch;

    Connection *connection = (Connection *)baton->hw;

    while(true){

      if(SP_poll(connection->Mbox)>0){

        ret = SP_receive( connection->Mbox, &service_type, sender, 100, &num_groups, target_groups,
                &mess_type, &endian_mismatch, sizeof(mess), mess );

        if (ret < 0 ){
        }else if( Is_regular_mess( service_type ) ){
          mess[ret] = 0;
          baton->message = String::New(mess);
          return 0;
        }else{
        }

        ret = 0;
      }
    }
    baton->message = String::New("");
    return 0;
  }

  static int EIO_AfterRead(eio_req *req)
  {
    HandleScope scope;
    read_baton_t *baton = static_cast<read_baton_t *>(req->data);
    ev_unref(EV_DEFAULT_UC);
    baton->hw->Unref();

    Local<Value> argv[1];

    argv[0] = baton->message;

    TryCatch try_catch;

    baton->cb->Call(Context::GetCurrent()->Global(), 1, argv);

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

    baton->cb.Dispose();

    delete baton;
    return 0;
  }

  private:
    char    Private_group[20];
    mailbox Mbox;
};

extern "C" { 
  static void init(Handle<Object> target) {
    Connection::Initialize(target);
  }
  NODE_MODULE(spread, init);
}

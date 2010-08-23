var sys = require('sys'),
    spread = require('../lib/spread');

var con = new spread.Connection();
con.on('connected', function(){

  var self = this;
  var i = 0;

  console.log('connected');
  self.join('NODE');

  setInterval(function(){
    var msg = 'NODE' + (i++);
    console.log('send:' + msg);
    self.multicast('NODE', msg);
  }, 1000);

  setInterval(function () {
    var msg;
    while((msg = self.readSync()) != undefined){
      console.log('recv:' + msg);
    }
  }, 100);

});

con.on('error', function(){
  console.log('error'+sys.inspect(arguments));
  
});

con.on('message', function(msg){
  console.log(msg);
});

con.connect('4803@localhost', 'nodejs1');

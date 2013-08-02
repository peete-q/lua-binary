#lua-binary

A lua library for serializing lua value to a binary string, like MessagePack

##feature

* supported type : nil, number, boolean, string, table.
* extendable type : lightuserdata, userdata, closure. convert to string, first.
* compression : yes
* loop nest: ok

##example

    local binary = require 'binary'
    local tb = {
        [1] = 1,
        [2] = false,
        s = 'string',
    }
    tb.ref = tb
    local s = binary.pack(nil, 1, true, false, 'string', tb)
    local a, b, c, d, e, f = binary.unpack(s)
  

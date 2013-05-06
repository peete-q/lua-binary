#lua-binary

A lua library for serializing lua value to a binary string

##description

* supported : nil, number, boolean, string, table.
* extendable : lightuserdata, userdata, lua closure. you can convert them to string, first.
* unsupported : c function.

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
  

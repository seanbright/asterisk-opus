# These aren't the files you're looking for...

Because [Digium](http://www.digium.com/) has now [released their own `codec_opus`](http://downloads.digium.com/pub/telephony/codec_opus/) for Asterisk 13 and Asterisk 14, I don't see a compelling reason for anyone to be using this one.

The code is still here, but if you are looking for continued development I suggest you take a look at [Alexander Traud's fork](https://github.com/traud/asterisk-opus).

If you're concerned that Digium's `codec_opus` "phones home," just set up [`dnsmasq`](http://www.thekelleys.org.uk/dnsmasq/doc.html) with the following config:

    local=/stats.asterisk.org/

And you're done. You could probably also do something with `/etc/hosts`, but using `dnsmasq` is cleaner.

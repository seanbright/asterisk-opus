# Asterisk Opus/VP8 patch

**Note:** This code applies to Asterisk 13.  Other versions of this patch are available:

* [Asterisk 11](https://github.com/seanbright/asterisk-opus/tree/asterisk-11)
* [Asterisk 12](https://github.com/seanbright/asterisk-opus/tree/asterisk-12)

Since Opus and VP8 cannot, as of now, be integrated into the main Asterisk repositories (learn why [in this thread](http://lists.digium.com/pipermail/asterisk-dev/2013-May/060356.html)), we prepared a module that adds support for both codecs (Opus transcoding, VP8 passthrough) to [Asterisk 13](https://github.com/asterisk/asterisk):

**Note:** There is no longer any need to patch the core Asterisk 13 files!

Because this module does not patch any core Asterisk files, it can be compiled either inside the full Asterisk source tree, or it can be compiled against just the Asterisk headers (available in the "asterisk-devel" package on most distros).

## Building inside Asterisk
To support Opus, you'll need to install [libopus](http://www.opus-codec.org/downloads/) first. No library is needed for VP8, as its support is passthrough only.

    cd asterisk-13-git
    cp /path/to/asterisk-opus/codecs/* codecs/
    cp /path/to/asterisk-opus/formats/* formats/

Run the bootstrap script to regenerate the configure:

	./bootstrap.sh

Configure Asterisk.

	./configure --prefix=/usr

Make sure that codec\_opus and format\_vp8 are enabled in menuselect before going on. For better results, install the slin16 versions of the Asterisk sounds, which are not enabled by default.

	make menuselect

Compile and install.

	make
	make install

## Building outside Asterisk
First, make sure you have the Asterisk headers (often available as "asterisk-devel") as well as libopus.  Then use the following commands to build the Opus and VP8 modules:

	cd /path/to/asterisk-opus
	make
	make install

## Testing
You can test Opus using the free softphone [PhonerLite](http://phonerlite.de/download_en.htm). Make sure you choose the beta version, as the stable one does not comply with [draft-ietf-payload-rtp-opus](http://tools.ietf.org/html/draft-ietf-payload-rtp-opus-00) (RTP timestamp increment).

For VP8 you can make use of the open source softphone [Linphone](http://www.linphone.org/eng/linphone/news/linphone-3.5.0-released-for-desktop.html), which added support for VP8 in version 3.5.0.

## What is missing
SDP fmtp parameters related to Opus and defined in [draft-ietf-payload-rtp-opus](http://tools.ietf.org/html/draft-ietf-payload-rtp-opus-00) are parsed but currently ignored: this means that there's no interaction between chan\_sip and codec\_opus in that sense. There is also no ad-hoc Opus configuration file for codec defaults. VP8, as anticipated, is passthrough only: besides, there's currently no way to read VP8 files for Playback.

## Help us improve the support!
Found an issue? Solved one? Added something that was missing? Help us make it better!

Maintained by [@seanbright](https://github.com/seanbright)
Originally Developed by [@meetecho](https://github.com/meetecho)

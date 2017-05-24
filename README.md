# Syphonium
Google Chromium web browser with Syphon client support.

This is a custom fork / branch of Googles Chrome/Chromium browser which implements Syphon Clients as video devices via Chromiums media/capture/video API.

Syphon Servers running locally can be discovered by Chrome automatically and seen via getUserMedia js API's as though they are native cameras.

This allows for webRTC and HTML5 video applications to benefit from native applications with Syphon output.

There are currently no plans to implement a Syphon Server for Chrome/Chromium - as its unclear what the mechanism and expected behaviour should be. Suggestions welcome.


Enables audio outputs on OSX for CM6206 Audio Card. By default, although it is fully USB audio compliant, outputs are not enabled and there is no sound

# Status

Fixes 
+ works on x64 platforms
+ works with Catalina 10.15.6

# Notes

kIOReturnExclusiveAccess
case kIOReturnExclusiveAccess: sprintf(out_buf,"kIOReturnExclusiveAccess - exclusive access and device already open"); break;

Succeeded! Here they say, it doesn't matter if it happens :)
+ https://tewarid.github.io/2012/04/27/access-usb-device-on-mac-os-x-using-i-o-kit.html

```cpp
else if (ret == kIOReturnExclusiveAccess)
{
    // this is not a problem as we can still do some things
}
```

MOZGIII / cm6206-mac-codeless-kext
+ https://github.com/MOZGIII/cm6206-mac-codeless-kext
+ Codeless Kext for CM6206 USB Sound Card to prevent the Apple drivers from hijacking a device
+ /System/Library/Extensions/
    + became readonly
        + From within the recovery mode might be possible
    + Kexts are deprecated

[How to replace the missing optical-digital audio output on newer Macs](https://www.macworld.com/article/3205890/how-to-replace-the-missing-optical-digital-audio-output-on-newer-macs.html)

[libusb-devel](https://sourceforge.net/p/libusb/mailman/libusb-devel/thread/4A12EB24.8080105%40probo.com/)


[Technical Q&A QA1370 - Common QA and Roadmap for USB Software Development on Mac OS X](http://mirror.informatimago.com/next/developer.apple.com/qa/qa2004/qa1370.html)

[Technical Q&A QA1075 - Making sense of IOKit error codes](http://mirror.informatimago.com/next/developer.apple.com/qa/qa2001/qa1075.html)

[USBInterfaceOpen](https://developer.apple.com/documentation/iokit/iousbinterfaceinterface192/1558701-usbinterfaceopen?language=objc)

[USBInterfaceOpen fails always](https://developer.apple.com/forums/thread/20953)

[USBInterfaceOpen always report kIOReturnExclusiveAccess error](https://stackoverflow.com/questions/31699051/usbinterfaceopen-always-report-kioreturnexclusiveaccess-error)

# kernel extensions (KEXTs)

[Apple deprecating macOS kernel extensions (KEXTs) is a great win for security](https://www.zdnet.com/article/apple-deprecating-macos-kernel-extensions-kexts-is-a-great-win-for-security/)

[How to delete kexts in Catalina?](https://apple.stackexchange.com/questions/372684/how-to-delete-kexts-in-catalina)

[“Read-only file system” when I try to touch in Mac OS Catalina](https://superuser.com/questions/1495124/read-only-file-system-when-i-try-to-touch-in-mac-os-catalina)
+ Apple kernel extension APIs to be deprecated in macOS 10.15.4.

[How to Add and Remove Kexts from macOS](https://www.maketecheasier.com/add-remove-kexts-from-macos/)
+ https://www.tonymacx86.com/resources/kextbeast-2-0-2.399/

[MultiBeast 12.3.0 - Catalina 12.3.0](https://www.tonymacx86.com/resources/multibeast-12-3-0-catalina.491/)
+ MultiBeast, the ultimate post-installation utility, has been updated to version 12.3.0 for macOS Catalina 10.15.5+.

# USB Audio

https://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25003.html
https://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25017.html
[Linux Kernel & Device Driver Programming](http://www.cs.fsu.edu/~baker/devices/)
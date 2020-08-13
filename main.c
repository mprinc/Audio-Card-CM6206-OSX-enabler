/*
 * CM6206 Enabler by Alexander Thomas, 2009/06 - 2011/01
 * This will activate sound output on some of the cheapest USB 5.1 adaptors
 *   available, more specifically the ones that use the C-Media CM6206 chip.
 *   This chip is also used in some products from major brands, e.g. the
 *   Zalman ZM-RS6F.
 *   The CM6206 is fully USB audio compliant and strictly spoken does not
 *   require drivers under any OS that supports USB audio like OS X, but for
 *   some reason it boots with its outputs disabled. All that's required are
 *   some initialisation commands, and that's exactly what this program does.
 *
 * This is genuine Frankenstein software, composed from lesser parts of Apple
 *   sample code, some previous USB camera thing I wrote, SleepWatcher, USB
 *   sniff logs, and the Linux ALSA drivers.
 * I'm not very experienced in writing software that deals with USB, so it is
 *   entirely possible that this program will cause kernel panics under
 *   special circumstances. Use at your own risk.
 * There's probably also a lot of opportunity to simplify and/or do things
 *   more efficiently.
 *
 * Versions: 1.0  2009/06: initial release
 *           2.0  2011/01: implemented 'daemon mode'
 *           2.1  2011/02: fixed the program causing a delay when entering sleep
 *
 * TODO:
 *   - figure out all the commands supported by the CM6206 and make a GUI
 *     that allows to change those settings (like S/PDIF on/off, channels,
 *     microphone stereo/mono/bias voltage...)
 *   - check if the CM6206 has a built-in 'virtual headphone surround' mode,
 *     and if so, allow to enable it.
 *   - make it work in OS X 10.4.* and 10.3.9. For some reason, interface 2
 *     cannot be opened in those OSs because it is 'in use' (error 2c5)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 *
 * Some useful links that helped to make this possible:
 * http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25003.html
 * http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25017.html
 * http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/sound/usb/usbaudio.c#L3276
 *
 * Many thanks to Mark Hempelmann for donating enough to motivate me to
 *   implement daemon mode :))
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mach/mach.h>

#include <CoreFoundation/CFNumber.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#define CMVERSION "2.1"

// for debugging
//#define VERBOSE

#define kVendorID	0x0d8c
#define kProductID	0x0102

typedef struct MyPrivateData {
    io_object_t				notification;
    IOUSBDeviceInterface	**deviceInterface;
    CFStringRef				deviceName;
} MyPrivateData;


static IONotificationPortRef	gNotifyPort;
static io_iterator_t			gAddedIter;
static CFRunLoopRef				gRunLoop;
static int						gVerbose;


void printUsage( const char *progName )
{
	printf("Usage: %s [-s] [-d] [-v] [-V]\n", progName );
	printf("  Activates sound outputs on CM6206 USB devices.\n");
	printf("  -s: Silent mode (default in daemon mode)\n");
	printf("  -v: Verbose mode (default in non-daemon mode)\n");
	printf("  -d: Daemon mode: the program keeps running and automatically activates any\n");
	printf("      devices that are connected, or all devices upon wake-from-sleep.\n");
	printf("  -V: Print version number and exit.\n");
}


/**** Error handlers ****/
/* Utter overkill, but copy&paste is so easy. */
int ErrorName (IOReturn err, char* out_buf) {
    int ok=true;
    switch (err) {
		case 0: sprintf(out_buf,"ok"); break;
		case kIOReturnError: sprintf(out_buf,"kIOReturnError - general error"); break;
		case kIOReturnNoMemory: sprintf(out_buf,"kIOReturnNoMemory - can't allocate memory");  break;
		case kIOReturnNoResources: sprintf(out_buf,"kIOReturnNoResources - resource shortage"); break;
		case kIOReturnIPCError: sprintf(out_buf,"kIOReturnIPCError - error during IPC"); break;
		case kIOReturnNoDevice: sprintf(out_buf,"kIOReturnNoDevice - no such device"); break;
		case kIOReturnNotPrivileged: sprintf(out_buf,"kIOReturnNotPrivileged - privilege violation"); break;
		case kIOReturnBadArgument: sprintf(out_buf,"kIOReturnBadArgument - invalid argument"); break;
		case kIOReturnLockedRead: sprintf(out_buf,"kIOReturnLockedRead - device read locked"); break;
		case kIOReturnLockedWrite: sprintf(out_buf,"kIOReturnLockedWrite - device write locked"); break;
		case kIOReturnExclusiveAccess: sprintf(out_buf,"kIOReturnExclusiveAccess - exclusive access and device already open"); break;
		case kIOReturnBadMessageID: sprintf(out_buf,"kIOReturnBadMessageID - sent/received messages had different msg_id"); break;
		case kIOReturnUnsupported: sprintf(out_buf,"kIOReturnUnsupported - unsupported function"); break;
		case kIOReturnVMError: sprintf(out_buf,"kIOReturnVMError - misc. VM failure"); break;
		case kIOReturnInternalError: sprintf(out_buf,"kIOReturnInternalError - internal error"); break;
		case kIOReturnIOError: sprintf(out_buf,"kIOReturnIOError - General I/O error"); break;
		case kIOReturnCannotLock: sprintf(out_buf,"kIOReturnCannotLock - can't acquire lock"); break;
		case kIOReturnNotOpen: sprintf(out_buf,"kIOReturnNotOpen - device not open"); break;
		case kIOReturnNotReadable: sprintf(out_buf,"kIOReturnNotReadable - read not supported"); break;
		case kIOReturnNotWritable: sprintf(out_buf,"kIOReturnNotWritable - write not supported"); break;
		case kIOReturnNotAligned: sprintf(out_buf,"kIOReturnNotAligned - alignment error"); break;
		case kIOReturnBadMedia: sprintf(out_buf,"kIOReturnBadMedia - Media Error"); break;
		case kIOReturnStillOpen: sprintf(out_buf,"kIOReturnStillOpen - device(s) still open"); break;
		case kIOReturnRLDError: sprintf(out_buf,"kIOReturnRLDError - rld failure"); break;
		case kIOReturnDMAError: sprintf(out_buf,"kIOReturnDMAError - DMA failure"); break;
		case kIOReturnBusy: sprintf(out_buf,"kIOReturnBusy - Device Busy"); break;
		case kIOReturnTimeout: sprintf(out_buf,"kIOReturnTimeout - I/O Timeout"); break;
		case kIOReturnOffline: sprintf(out_buf,"kIOReturnOffline - device offline"); break;
		case kIOReturnNotReady: sprintf(out_buf,"kIOReturnNotReady - not ready"); break;
		case kIOReturnNotAttached: sprintf(out_buf,"kIOReturnNotAttached - device not attached"); break;
		case kIOReturnNoChannels: sprintf(out_buf,"kIOReturnNoChannels - no DMA channels left"); break;
		case kIOReturnNoSpace: sprintf(out_buf,"kIOReturnNoSpace - no space for data"); break;
		case kIOReturnPortExists: sprintf(out_buf,"kIOReturnPortExists - port already exists"); break;
		case kIOReturnCannotWire: sprintf(out_buf,"kIOReturnCannotWire - can't wire down physical memory"); break;
		case kIOReturnNoInterrupt: sprintf(out_buf,"kIOReturnNoInterrupt - no interrupt attached"); break;
		case kIOReturnNoFrames: sprintf(out_buf,"kIOReturnNoFrames - no DMA frames enqueued"); break;
		case kIOReturnMessageTooLarge: sprintf(out_buf,"kIOReturnMessageTooLarge - oversized msg received on interrupt port"); break;
		case kIOReturnNotPermitted: sprintf(out_buf,"kIOReturnNotPermitted - not permitted"); break;
		case kIOReturnNoPower: sprintf(out_buf,"kIOReturnNoPower - no power to device"); break;
		case kIOReturnNoMedia: sprintf(out_buf,"kIOReturnNoMedia - media not present"); break;
		case kIOReturnUnformattedMedia: sprintf(out_buf,"kIOReturnUnformattedMedia - media not formatted"); break;
		case kIOReturnUnsupportedMode: sprintf(out_buf,"kIOReturnUnsupportedMode - no such mode"); break;
		case kIOReturnUnderrun: sprintf(out_buf,"kIOReturnUnderrun - data underrun"); break;
		case kIOReturnOverrun: sprintf(out_buf,"kIOReturnOverrun - data overrun"); break;
		case kIOReturnDeviceError: sprintf(out_buf,"kIOReturnDeviceError - the device is not working properly!"); break;
		case kIOReturnNoCompletion: sprintf(out_buf,"kIOReturnNoCompletion - a completion routine is required"); break;
		case kIOReturnAborted: sprintf(out_buf,"kIOReturnAborted - operation aborted"); break;
		case kIOReturnNoBandwidth: sprintf(out_buf,"kIOReturnNoBandwidth - bus bandwidth would be exceeded"); break;
		case kIOReturnNotResponding: sprintf(out_buf,"kIOReturnNotResponding - device not responding"); break;
		case kIOReturnIsoTooOld: sprintf(out_buf,"kIOReturnIsoTooOld - isochronous I/O request for distant past!"); break;
		case kIOReturnIsoTooNew: sprintf(out_buf,"kIOReturnIsoTooNew - isochronous I/O request for distant future"); break;
		case kIOReturnNotFound: sprintf(out_buf,"kIOReturnNotFound - data was not found"); break;
		case kIOReturnInvalid: sprintf(out_buf,"kIOReturnInvalid - should never be seen"); break;
		case kIOUSBUnknownPipeErr:sprintf(out_buf,"kIOUSBUnknownPipeErr - Pipe ref not recognised"); break;
		case kIOUSBTooManyPipesErr:sprintf(out_buf,"kIOUSBTooManyPipesErr - Too many pipes"); break;
		case kIOUSBNoAsyncPortErr:sprintf(out_buf,"kIOUSBNoAsyncPortErr - no async port"); break;
		case kIOUSBNotEnoughPipesErr:sprintf(out_buf,"kIOUSBNotEnoughPipesErr - not enough pipes in interface"); break;
		case kIOUSBNotEnoughPowerErr:sprintf(out_buf,"kIOUSBNotEnoughPowerErr - not enough power for selected configuration"); break;
		case kIOUSBEndpointNotFound:sprintf(out_buf,"kIOUSBEndpointNotFound - Not found"); break;
		case kIOUSBConfigNotFound:sprintf(out_buf,"kIOUSBConfigNotFound - Not found"); break;
		case kIOUSBTransactionTimeout:sprintf(out_buf,"kIOUSBTransactionTimeout - time out"); break;
		case kIOUSBTransactionReturned:sprintf(out_buf,"kIOUSBTransactionReturned - The transaction has been returned to the caller"); break;
		case kIOUSBPipeStalled:sprintf(out_buf,"kIOUSBPipeStalled - Pipe has stalled, error needs to be cleared"); break;
		case kIOUSBInterfaceNotFound:sprintf(out_buf,"kIOUSBInterfaceNotFound - Interface ref not recognised"); break;
		case kIOUSBLinkErr:sprintf(out_buf,"kIOUSBLinkErr - <no error description available>"); break;
		case kIOUSBNotSent2Err:sprintf(out_buf,"kIOUSBNotSent2Err - Transaction not sent"); break;
		case kIOUSBNotSent1Err:sprintf(out_buf,"kIOUSBNotSent1Err - Transaction not sent"); break;
		case kIOUSBBufferUnderrunErr:sprintf(out_buf,"kIOUSBBufferUnderrunErr - Buffer Underrun (Host hardware failure on data out, PCI busy?)"); break;
		case kIOUSBBufferOverrunErr:sprintf(out_buf,"kIOUSBBufferOverrunErr - Buffer Overrun (Host hardware failure on data out, PCI busy?)"); break;
		case kIOUSBReserved2Err:sprintf(out_buf,"kIOUSBReserved2Err - Reserved"); break;
		case kIOUSBReserved1Err:sprintf(out_buf,"kIOUSBReserved1Err - Reserved"); break;
		case kIOUSBWrongPIDErr:sprintf(out_buf,"kIOUSBWrongPIDErr - Pipe stall, Bad or wrong PID"); break;
		case kIOUSBPIDCheckErr:sprintf(out_buf,"kIOUSBPIDCheckErr - Pipe stall, PID CRC Err:or"); break;
		case kIOUSBDataToggleErr:sprintf(out_buf,"kIOUSBDataToggleErr - Pipe stall, Bad data toggle"); break;
		case kIOUSBBitstufErr:sprintf(out_buf,"kIOUSBBitstufErr - Pipe stall, bitstuffing"); break;
		case kIOUSBCRCErr:sprintf(out_buf,"kIOUSBCRCErr - Pipe stall, bad CRC"); break;
			
		default: sprintf(out_buf,"Unknown Error:%d Sub:%d System:%d",err_get_code(err),
						 err_get_sub(err),err_get_system(err)); ok=false; break;
    }
    return ok;
}

void ShowError(IOReturn err, char* where) {
    char buf[256];
    if (where) {
		fprintf(stderr, "%s: ", where);
    }
    if (err==0) {
		fprintf(stderr, "ok");
    } else {
		ErrorName(err,buf);
		fprintf(stderr, "Error: %s ", buf);
    }
    fprintf(stderr, "\n");
}

void CheckError(IOReturn err, char* where) {
    if (err) {
		ShowError(err,where);
    }
}


//================================================================================================
//
// "interface" handlers
//
//================================================================================================

int writeCM6206Registers( IOUSBInterfaceInterface183 **intf, UInt8 byte1, UInt8 byte2, UInt8 regNo )
{
    UInt8 buf[8];
    IOReturn err;
    IOUSBDevRequest req;
    UInt8 pipeNo = 0; // 0 is the default pipe (and the only one that works here)
    
    buf[0] = 0x20;
    buf[1] = byte1;
    buf[2] = byte2;
    buf[3] = regNo;
    
    req.bmRequestType=USBmakebmRequestType(kUSBOut, kUSBClass, kUSBInterface );
    req.bRequest=0x09; // these values are taken from the SPDIF enable log
    req.wValue=0x0200;
    req.wIndex=0x03;
    req.wLength=4;
    req.pData=buf;
    err=(*intf)->ControlRequest(intf,pipeNo,&req);
    CheckError(err,"usbWriteCmdWithBRequest");
    if (err==kIOUSBPipeStalled) (*intf)->ClearPipeStall(intf,pipeNo);
    
    return (err != 0);
}

//================================================================================================
// This sends the actual activation commands
void initCM6206(IOUSBInterfaceInterface183 **intf)
{
    int err = 0;
    
    // This should reset the registers
    if( writeCM6206Registers(intf, 0x00,0x00,0x00) ) {
    	fprintf(stderr, "Error while resetting registers\n");
		err = 1;
    }
    
    // This enables SPDIF, values copied from SniffUSB log (this one was easy)
    // I'm not sure if the SPDIF outputs surround data, as I don't have the means to test it.
    if( writeCM6206Registers(intf, 0x00,0x30,0x01) ) {
    	fprintf(stderr, "Error while attempting to enable SPDIF\n");
		err = 1;
    }
    
    // This enables sound output. Why on earth it's disabled upon power-on,
    // nobody knows (except maybe some Taiwanese engineer).
    // These values were taken from the ALSA USB driver: "Enable line-out driver mode,
    // set headphone source to front channels, enable stereo mic."
    // That's for the CM106, however. On the CM6206 they appear to enable everything.
    if( writeCM6206Registers(intf, 0x04,0x80,0x02) ) {
    	fprintf(stderr, "Error while attempting to enable analog out\n");
    	err = 1;
    }
    
    // Extra stuff, taken from the Alsa-user mailinglist.
    // The above works for me, so I didn't bother testing the following.
    // It may be completely redundant or make your Mac explode. Try at your own risk.
    
    // "Enable DACx2, PLL binary, Soft Mute, and SPDIF-out"
    //writeCM6206Registers(intf, 0x00,0xb0,0x01);
    // "Enable all channels and select 48-pin chipset"
    //writeCM6206Registers(intf, 0x7f,0x00,0x03);
    
    if(!err && gVerbose)
		fprintf(stderr, "Successfully sent CM6206 activation commands!\n");
}


void dealWithInterface(io_service_t usbInterfaceRef)
{
    IOReturn					err;
    IOCFPlugInInterface 		**iodev;	// requires <IOKit/IOCFPlugIn.h>
    IOUSBInterfaceInterface183	**intf;
    SInt32						score;
	
	
    err = IOCreatePlugInInterfaceForService(usbInterfaceRef, kIOUSBInterfaceUserClientTypeID,
											kIOCFPlugInInterfaceID, &iodev, &score);
    if (err || !iodev) {
		fprintf(stderr, "dealWithInterface: unable to create plugin. ret = %08x, iodev = %p\n", err, iodev);
		return;
    }
    err = (*iodev)->QueryInterface(iodev, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID183), (LPVOID)&intf);
    (*iodev)->Release(iodev);				// done with this
    if (err || !intf) {
		fprintf(stderr, "dealWithInterface: unable to create a device interface. ret = %08x, intf = %p\n", err, intf);
		return;
    }
    err = (*intf)->USBInterfaceOpen(intf);
    if (err) {
		fprintf(stderr, "dealWithInterface: unable to open interface. ret = %08x\n", err);
		// Alas, this doesn't solve the problem in OS X 10.4.*
		err = (*intf)->USBInterfaceOpenSeize(intf);
		if (err) {
			fprintf(stderr, "dealWithInterface: unable to seize interface. ret = %08x\n", err);
			return;
		}
    }
#ifdef VERBOSE
	{
		UInt8 numPipes;
		err = (*intf)->GetNumEndpoints(intf, &numPipes);
		if (err) {
			fprintf(stderr, "dealWithInterface: unable to get number of endpoints. ret = %08x\n", err);
			(*intf)->USBInterfaceClose(intf);
			(*intf)->Release(intf);
			return;
		}
		fprintf(stderr, "numPipes = %d\n", numPipes);
    }
#endif

	initCM6206(intf);
    
    err = (*intf)->USBInterfaceClose(intf);
    if (err) {
		fprintf(stderr, "dealWithInterface: unable to close interface. ret = %08x\n", err);
		return;
    }
    err = (*intf)->Release(intf);
    if (err) {
		fprintf(stderr, "dealWithInterface: unable to release interface. ret = %08x\n", err);
		return;
    }
}


void dealWithDevice(io_service_t usbDeviceRef)
{
    IOReturn					err;
    IOCFPlugInInterface			**iodev;	// requires <IOKit/IOCFPlugIn.h>
    IOUSBDeviceInterface		**dev;
    SInt32						score;
    UInt8						numConf;
    IOUSBConfigurationDescriptorPtr	confDesc;
    IOUSBFindInterfaceRequest		interfaceRequest;
    io_iterator_t				iterator;
    io_service_t				usbInterfaceRef;
    int nCount;
    int nAttempts = 20;
    
    err = IOCreatePlugInInterfaceForService(usbDeviceRef, kIOUSBDeviceUserClientTypeID,
											kIOCFPlugInInterfaceID, &iodev, &score);
    if (err || !iodev) {
		fprintf(stderr, "dealWithDevice: unable to create plugin. ret = %08x, iodev = %p\n", err, iodev);
		return;
    }
    err = (*iodev)->QueryInterface(iodev, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID197), (LPVOID)&dev);
    (*iodev)->Release(iodev);	// done with this
    if (err || !dev) {
		fprintf(stderr, "dealWithDevice: unable to create a device interface. ret = %08x, dev = %p\n", err, dev);
		return;
    }
	
    // This is from another USB program I wrote where the device was sometimes slow.
	// It doesn't hurt to leave it in.
    do {
		err = (*dev)->USBDeviceOpen(dev);
		if(err) {
			fprintf(stderr, "Trying to open device, %d seconds left...\n",nAttempts);
			if( nAttempts > 1 )
				sleep(1); // wait a second
		}
		else
			nAttempts = 1;
    }
    while( --nAttempts > 0 );
    if (err) {
		fprintf(stderr, "dealWithDevice: unable to open device. ret = %08x\n", err);
		return;
    }
    
    err = (*dev)->GetNumberOfConfigurations(dev, &numConf);
    if (err || !numConf) {
		fprintf(stderr, "dealWithDevice: unable to obtain the number of configurations. ret = %08x\n", err);
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
		return;
    }
#ifdef VERBOSE
    fprintf(stderr, "found %d configurations\n", numConf);
#endif
    
    err = (*dev)->GetConfigurationDescriptorPtr(dev, 0, &confDesc);	// get the first config desc (index 0)
    if (err) {
		fprintf(stderr, "dealWithDevice:unable to get config descriptor for index 0\n");
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
		return;
    }
    err = (*dev)->SetConfiguration(dev, confDesc->bConfigurationValue);
    if (err) {
		fprintf(stderr, "dealWithDevice: unable to set the configuration\n");
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
		return;
    }
    
	// It's probably possible to get the identifiers of the interface we want and
	// directly query that interface, but this works too.
    interfaceRequest.bInterfaceClass = kIOUSBFindInterfaceDontCare;	// requested class
    interfaceRequest.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;	// requested subclass
    interfaceRequest.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;	// requested protocol
    interfaceRequest.bAlternateSetting = kIOUSBFindInterfaceDontCare;	// requested alt setting
    
    err = (*dev)->CreateInterfaceIterator(dev, &interfaceRequest, &iterator);
    if (err) {
		fprintf(stderr, "dealWithDevice: unable to create interface iterator\n");
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
		return;
    }
	
    nCount = 0;
    while( (usbInterfaceRef = IOIteratorNext(iterator)) ) {
#ifdef VERBOSE
		fprintf(stderr, "found interface: %p\n", (void*)usbInterfaceRef);
#endif
		if( nCount == 1 ) // The second interface is the one we need
			dealWithInterface(usbInterfaceRef); // Here the actual interesting stuff happens!!!
		IOObjectRelease(usbInterfaceRef);
		nCount++;
    }
    
    IOObjectRelease(iterator);
    iterator = 0;
	
    err = (*dev)->USBDeviceClose(dev);
    if (err) {
		fprintf(stderr, "dealWithDevice: error closing device - %08x\n", err);
		(*dev)->Release(dev);
		return;
    }
    err = (*dev)->Release(dev);
    if (err) {
		fprintf(stderr, "dealWithDevice: error releasing device - %08x\n", err);
		return;
    }
}


//================================================================================================
//
//	DeviceNotification
//
//	This routine will get called whenever any kIOGeneralInterest notification happens.  We are
//	interested in the kIOMessageServiceIsTerminated message so that's what we look for.  Other
//	messages are defined in IOMessage.h.
//
//================================================================================================
void DeviceNotification(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
    kern_return_t	kr;
    MyPrivateData	*privateDataRef = (MyPrivateData *) refCon;
    
    if (messageType == kIOMessageServiceIsTerminated) {
		if(gVerbose) {
			fprintf(stderr, "CM6206 device removed.\n");
			// Dump our private data just to see what it looks like.
			fprintf(stderr, "privateDataRef->deviceName: ");
			CFShow(privateDataRef->deviceName);
		}
		
        // Free the data we're no longer using now that the device is going away
        CFRelease(privateDataRef->deviceName);
        
        if (privateDataRef->deviceInterface) {
            kr = (*privateDataRef->deviceInterface)->Release(privateDataRef->deviceInterface);
        }
        
        kr = IOObjectRelease(privateDataRef->notification);
        
        free(privateDataRef);
    }
}


//================================================================================================
//
//	DeviceAdded
//
//	This routine is the callback for our IOServiceAddMatchingNotification.  When we get called
//	we will look at all the devices that were added and we will:
//
//	1.  Create some private data to relate to each device
//	2.  Submit an IOServiceAddInterestNotification of type kIOGeneralInterest for this device,
//	    using the refCon field to store a pointer to our private data.  When we get called with
//	    this interest notification, we can grab the refCon and access our private data.
//  3.  Run the CM6206 activation routine.
//
//================================================================================================
void DeviceAdded(void *refCon, io_iterator_t iterator)
{
    kern_return_t		kr;
    io_service_t		usbDevice;
    
    while ((usbDevice = IOIteratorNext(iterator))) {
        io_name_t		deviceName;
        CFStringRef		deviceNameAsCFString;	
        MyPrivateData	*privateDataRef = NULL;
        
        fprintf(stderr, "CM6206 device added.\n");
        
        // Add some app-specific information about this device.
        // Create a buffer to hold the data.
        privateDataRef = malloc(sizeof(MyPrivateData));
        bzero(privateDataRef, sizeof(MyPrivateData));
		
        // Get the USB device's name.
        kr = IORegistryEntryGetName(usbDevice, deviceName);
		if (KERN_SUCCESS != kr) {
            deviceName[0] = '\0';
        }
        
        deviceNameAsCFString = CFStringCreateWithCString(kCFAllocatorDefault, deviceName, 
                                                         kCFStringEncodingASCII);
        
        // Dump our data to stderr just to see what it looks like.
		if(gVerbose) {
			fprintf(stderr, "deviceName: ");
			CFShow(deviceNameAsCFString);
		}
        
        // Save the device's name to our private data.        
        privateDataRef->deviceName = deviceNameAsCFString;
		
        // Register for an interest notification of this device being removed. Use a reference to our
        // private data as the refCon which will be passed to the notification callback.
        kr = IOServiceAddInterestNotification(gNotifyPort,						// notifyPort
											  usbDevice,						// service
											  kIOGeneralInterest,				// interestType
											  DeviceNotification,				// callback
											  privateDataRef,					// refCon
											  &(privateDataRef->notification)	// notification
											  );
		
        if (KERN_SUCCESS != kr) {
            fprintf(stderr, "IOServiceAddInterestNotification returned 0x%08x.\n", kr);
        }
		
		// This is not strictly necessary but it seems to avoid kernel panics when some
		// third-party audio enhancers are active.
		sleep(1);
		
		dealWithDevice(usbDevice);  // here the important stuff happens
		
        // Done with this USB device; release the reference added by IOIteratorNext
        kr = IOObjectRelease(usbDevice);
    }
}

//================================================================================================
//
//	SignalHandler
//
//	This routine will get called when we interrupt the program (usually with a Ctrl-C from the
//	command line).
//
//================================================================================================
void SignalHandler( int sigraised )
{
	if(gVerbose)
		fprintf(stderr, "CM6206Init caught signal %d, exiting\n", sigraised);
	
    exit(0);
}


//================================================================================================
// Make a matching dictionary to find all devices with the given vendor & product ID
//
int makeDictionary( CFMutableDictionaryRef *matchingDictionary, SInt32 idVendor, SInt32 idProduct )
{
    CFNumberRef			numberRef = 0;
	
    *matchingDictionary = IOServiceMatching(kIOUSBDeviceClassName);	// requires <IOKit/usb/IOUSBLib.h>
    if (!*matchingDictionary) {
        fprintf(stderr, "Error: Could not create matching dictionary\n");
        return -1;
    }
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &idVendor);
    if (!numberRef) {
        fprintf(stderr, "Error: Could not create CFNumberRef for vendor\n");
        return -1;
    }
    CFDictionaryAddValue(*matchingDictionary, CFSTR(kUSBVendorID), numberRef);
    CFRelease(numberRef);
    numberRef = 0;
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &idProduct);
    if (!numberRef) {
        fprintf(stderr, "Error: Could not create CFNumberRef for product\n");
        return -1;
    }
    CFDictionaryAddValue(*matchingDictionary, CFSTR(kUSBProductID), numberRef);
    CFRelease(numberRef);
    numberRef = 0;
	
	return 0;
}


//================================================================================================
// Look for all matching devices and deal with them once.
//
int ActivateDevices()
{
    kern_return_t		kr;
	mach_port_t			masterPort = 0;	// requires <mach/mach.h>
    CFMutableDictionaryRef 	matchingDictionary = 0;	// requires <IOKit/IOKitLib.h>
    io_iterator_t 		iterator = 0;
    io_service_t		usbDeviceRef;
    int					nRet, foundDevice = 0;
	
	kr = IOMasterPort(MACH_PORT_NULL, &masterPort);
	if (kr) {
		fprintf(stderr, "Error: Could not create master port, err = %08x\n", kr);
		return kr;
	}
	
	nRet = makeDictionary( &matchingDictionary, kVendorID, kProductID );
	if (nRet)
		return nRet;
	
	kr = IOServiceGetMatchingServices(masterPort, matchingDictionary, &iterator);
	matchingDictionary = 0;		// this was consumed by the above call
	
	while ( (usbDeviceRef = IOIteratorNext(iterator)) ) {
		foundDevice = 1;
		if(gVerbose)
			fprintf(stderr, "CM6206 found (device %p)\n", (void*)usbDeviceRef);
		dealWithDevice(usbDeviceRef);  // here the important stuff happens
		IOObjectRelease(usbDeviceRef);	// no longer need this reference
	}
	if(! foundDevice && gVerbose)
		fprintf(stderr, "No CM6206 device found on the USB bus.\n");
	
	IOObjectRelease(iterator);
	iterator = 0;
	
	mach_port_deallocate(mach_task_self(), masterPort);
	
	return 0;
}	


//================================================================================================
// Callback for power events (sleep, wake).
//
void powerCallback(void *rootPort, io_service_t y, natural_t msgType, void *msgArgument)
{	
	if( msgType == kIOMessageSystemHasPoweredOn ) {
		if(gVerbose)
			fprintf(stderr, "Waking from sleep, re-activating any CM6206 devices...\n");
		sleep(1);
		ActivateDevices();
	}
	else if( msgType == kIOMessageCanSystemSleep ||
	         msgType == kIOMessageSystemWillSleep ) {
		// This case must be treated, otherwise the system will wait in vain for the program
		// to allow sleep, and only sleep after a timeout.
		IOAllowPowerChange(* (io_connect_t *) rootPort, (long) msgArgument);
	}
}


//================================================================================================
//
int main(int argc, const char * argv[])
{
	int					bDaemon = 0;
    sig_t				oldHandler;
	gVerbose = 1;
	
	for( int a=1; a<argc; a++ ) {
		if( strcmp( argv[a], "-d" ) == 0 ) {
			bDaemon = 1;
			gVerbose = 0;
		}
		else if( strcmp( argv[a], "-v" ) == 0 )
			gVerbose = 1;
		else if( strcmp( argv[a], "-s" ) == 0 )
			gVerbose = 0;
		else if( strcmp( argv[a], "-V" ) == 0 ) {
			printf( "CM6206Init version %s\n", CMVERSION );
			return 0;
		}
		else if( strcmp( argv[a], "-h" ) == 0 ) {
			printUsage(argv[0]);
			return 0;
		}
		else {
			fprintf(stderr, "Ignoring unknown argument `%s'\n", (argv[a]));
		}
	}
	
	
	// Set up a signal handler so we can clean up when we're interrupted from the command line
    // Otherwise we stay in our run loop forever.
    oldHandler = signal(SIGINT, SignalHandler);
    if (oldHandler == SIG_ERR) {
        fprintf(stderr, "Could not establish new signal handler.");
	}
	signal(SIGHUP, (void*)ActivateDevices);
	
	
	if(bDaemon) {
		kern_return_t			kr;
		CFMutableDictionaryRef 	matchingDictionary = 0;	// requires <IOKit/IOKitLib.h>
		CFRunLoopSourceRef		runLoopSource;
		static io_connect_t		rootPort;
		IONotificationPortRef	notificationPort;
		io_object_t				notifier;		
		int						nRet;
		
		// Start run loop:
		// if a device is found, send activation commands
		// if a wake-from-sleep is detected, resend activation commands to all devices
		// if a device disconnects, remove its reference
		nRet = makeDictionary( &matchingDictionary, kVendorID, kProductID );
		if (nRet)
			return nRet;

		gNotifyPort = IONotificationPortCreate(kIOMasterPortDefault);
		runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);
		
		gRunLoop = CFRunLoopGetCurrent();
		CFRunLoopAddSource(gRunLoop, runLoopSource, kCFRunLoopDefaultMode);
		
		// Now set up a notification to be called when a device is first matched by I/O Kit.
		kr = IOServiceAddMatchingNotification(gNotifyPort,					// notifyPort
											  kIOFirstMatchNotification,	// notificationType
											  matchingDictionary,			// matching
											  DeviceAdded,					// callback
											  NULL,							// refCon
											  &gAddedIter					// notification
											  );
		
		// Set up callback for when system wakes from sleep
		rootPort = IORegisterForSystemPower(&rootPort, &notificationPort, powerCallback, &notifier);
		if (! rootPort) {
			fprintf(stderr, "IORegisterForSystemPower failed\n");
			return -1;
		}
		CFRunLoopAddSource(gRunLoop, IONotificationPortGetRunLoopSource(notificationPort), kCFRunLoopDefaultMode);		
		
		// Iterate once to get already-present devices and arm the notification    
		DeviceAdded(NULL, gAddedIter);	
		
		// Start the run loop. Now we'll receive notifications.
		if(gVerbose)
			printf("Starting run loop.\n\n");
		CFRunLoopRun();
        
		// We should never get here
		fprintf(stderr, "Unexpectedly back from CFRunLoopRun()!\n");
		return -1;
	}
	else {
		// Check for CM6206 once
		return ActivateDevices();
	}
}

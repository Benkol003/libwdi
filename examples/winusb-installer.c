#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ShlObj_core.h>
#include "getopt/getopt.h"
#include "libwdi.h"
#if defined(_PREFAST_)
/* Disable "Banned API Usage:" errors when using WDK's OACR/Prefast */
#pragma warning(disable:28719)
/* Disable "Consider using 'GetTickCount64' instead of 'GetTickCount'" when using WDK's OACR/Prefast */
#pragma warning(disable:28159)
#define _CRT_SECURE_NO_WARNINGS
#endif

#define oprintf(...) do {if (!opt_silent) printf(__VA_ARGS__);} while(0)

#define DESC "FTDI interface for WinUSB/OpenOCD"
//default to those forFTDI FT2232H-56Q
#define VID 0x0403
#define PID 0x6010
#define INF_NAME "usb_device.inf"
#define DEFAULT_DIR "usb_driver"
#define MI 0
//device VID PID and MI, will be in devinst id, but are still required by libwdi and im too lazy to parse/check atm
//dont use the device instance path of the COM port that starts with FTDI\\... as wont work

void usage(void)
{
	printf("Usage: \n");
	printf("Required Arguements:\n");
	printf("-v, --vid <id>             set the vendor ID (VID, use 0x prefix for hex)\n");
	printf("-p, --pid <id>             set the product ID (PID, use 0x prefix for hex)\n");
	printf("-i, --iid <id>             set the interface ID (MI)\n");

	printf("Optional Arguments:\n");
	printf("-n, --name <name>          set the device name\n");
	printf("-f, --inf <name>           set the inf name\n");
	printf("-e, --extract_dir <dir>    set the extraction directory\n");
	printf("-x, --extract              extract files only (don't install)\n");
	printf("-s, --silent               silent mode\n");
	printf("-l, --log                  set log level (0=debug, 4=none)\n");
	printf("-h, --help                 display usage\n");
	printf("\n");
}

//see libwdi.h WDI_.. for error codes
int __cdecl main(int argc, char** argv)
{
	static struct wdi_device_info* ldev, dev = { NULL, VID, PID, TRUE, MI, DESC, NULL, NULL, NULL };
	static struct wdi_options_create_list ocl = { 0 };
	ocl.list_all = TRUE; ocl.list_hubs = FALSE; ocl.trim_whitespaces = TRUE;
	static struct wdi_options_prepare_driver opd = { 0 }; opd.driver_type = WDI_WINUSB;
	static struct wdi_options_install_driver oid = { 0 };
	static struct wdi_options_install_cert oic = { 0 };
	static int opt_silent = 0, opt_extract = 0, log_level = WDI_LOG_LEVEL_WARNING;
	static BOOL matching_device_found;
	int c, r;
	char* inf_name = INF_NAME;
	char* ext_dir = DEFAULT_DIR;
	char* cert_name = NULL;

	static struct option long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"inf", required_argument, 0, 'f'},
		{"vid", required_argument, 0, 'v'},
		{"pid", required_argument, 0, 'p'},
		{"iid", required_argument, 0, 'i'},
		{"extract_dir", required_argument, 0, 'e'},
		{"extract", no_argument, 0, 'x'},
		{"silent", no_argument, 0, 's'},
		{"log", required_argument, 0, 'l'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	while (1)
	{
		c = getopt_long(argc, argv, "n:f:v:p:i:e:x:s:l:h", long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case 'n':
			dev.desc = optarg;
		case 'e':
			ext_dir = optarg;
			break;
		case 'f':
			inf_name = optarg;
			break;
		case 'h':
			usage();
			exit(0);
			break;
		case 'i':
			dev.is_composite = TRUE;
			dev.mi = (unsigned char)strtol(optarg, NULL, 0);
			break;
		case 'l':
			log_level = (int)strtol(optarg, NULL, 0);
			break;
		case 'p':
			dev.pid = (unsigned short)strtol(optarg, NULL, 0);
			break;
		case 's':
			opt_silent = 1;
			log_level = WDI_LOG_LEVEL_NONE;
			break;
		case 'v':
			dev.vid = (unsigned short)strtol(optarg, NULL, 0);
			break;
		case 'x':
			opt_extract = 1;
			break;
		default:
			usage();
			exit(0);
		}
	}

	wdi_set_log_level(log_level);

	if (!IsUserAnAdmin()) {
		oprintf("must run as an admin to be able to install libwdi certificate & WinUSB driver, exiting...\n");
		return WDI_ERROR_NEEDS_ADMIN;
	}

	oprintf("Extracting driver files...\n");
	r = wdi_prepare_driver(&dev, ext_dir, inf_name, &opd);
	oprintf("  %s\n", wdi_strerror(r));
	if ((r != WDI_SUCCESS) || (opt_extract))
		return r;
	

	
	//call installer without matching to any existing device
	r = wdi_install_driver(&dev, ext_dir, inf_name, &oid);
	oprintf("  %s\n", wdi_strerror(r));
	return r;
}


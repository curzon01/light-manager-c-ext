/*
 ============================================================================
 Name        : lightmanager.c
 Author      : zwiebelchen <lars.cebu@gmail.com>
 Modified    : Norbert Richter <mail@norbert-richter.info>
 Version     : 2.03.0021
 Copyright   : GPL
 Description : Access and control your jbmedia Light Manager Pro(+) from Linux
               based on source from light-manager-c written by original author
               with some enhancments.
               Open source published on
               http://code.google.com/p/light-manager-c-ext/
 ============================================================================
 */

/*
	Revision history of lightmanager.c

	Legende:
	+ New/Add
	- Bugfix
	* Change


	1.02.0004
			First release using extensions

	1.02.0005
	        - TCP command handling revised (did not worked on windows telnet TCP sockets)
	        * -h parameter (FS20 housecode) syntax must now be a FS20 code (e.g. -h 14213444)
	        * SET CLOCK parameter changed to have the same format as for Linux date -s MMDDhhmm[[CC]YY][.ss]
	        + multiple cmds on command line -c parameter (e.g. -c "GET CLOCK; GET TEMP")

	1.02.0006
			+ Parameter -d implemented: Run as a real daemon
			* Outputs are now going to syslog

	1.02.0007
			+ WAIT command implemented

	1.02.0008
			* Output default stdout, optional to syslog using cmd parameter -s

	1.02.0009
			+ multiple commands also possible on TCP connection command line (as it still works for -c parameter)
			+ New command GET/SET HOUSECODE (get/set FS20 housecode on command line)

	1.02.0010
			* FS20 command parameter changed

	1.02.0011
			+ Added parameter -a (listen for address)
			* additonal log program exit errors to stderr (even output is set to syslog)
			- Segmentation fault on ubs_connect when no device is connected
			- Segmentation fault on TCP command help output

	1.02.0012
			+ Added pidfile

	1.02.0013
			- Handle broken pipe signal
			- Handle client disconnect correctly (resulted in 100% CPU load)

	1.02.0014
			- FS20 address 1111 not accepted

	2.00.0015
			+ Web Server functionality added:
			  Usage: http://<server>/cmd=<command>[&<command>[&...]]
			         where <command> are a valid command (see command 'help')
			         multiple commands can be used delimited by a ampersand char &
			         e.g.
			         http://<serverip>/cmd=get time&get temp
			         reads the current clock and temperature
			+ Command VERSION added

	2.00.0016
			- GET TIME command sometimes return UTC instead of local time
			* Be more verbose (helpfull for multiple commands)
			+ New command VERBOSE/QUIET (default is: be VERBOSE)

	2.01.0017
			- Running as daemon: Sometimes not responsible anymore, solved

	2.02.0018
			+ InterTechno dim commands added (for more info see command "help")

	2.02.0019
			* some add. comments within source code

	2.03.0021
			+ SET CLOCK|TIME new command parameter AUTO (or AUTOCORRECTION)
			  for possible hour daylight saving time handling

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <netdb.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <libusb-1.0/libusb.h>


/* ======================================================================== */
/* Defines */
/* ======================================================================== */

/* Program name and version */
#define VERSION				"2.3"
#define BUILD				"0021"
#define PROGNAME			"Linux Lightmanager"

/* Some macros */
#define exit_if(expr) \
if(expr) { \
  debug(LOG_ERR, "%s: %d: %s: Error - %s", __FILE__, __LINE__, __PRETTY_FUNCTION__, strerror(errno)); \
  exit(1); \
}
#define return_if(expr, retvalue) \
if(expr) { \
  debug(LOG_DEBUG, "%s: %d: %s: Error - %s", __FILE__, __LINE__, __PRETTY_FUNCTION__, strerror(errno)); \
  return(retvalue); \
}


#define LM_VENDOR_ID		0x16c0		/* jbmedia Light-Manager (Pro) USB vendor */
#define LM_PRODUCT_ID		0x0a32		/* jbmedia Light-Manager (Pro) USB product ID */

#define USB_MAX_RETRY		5			/* max number of retries on usb error */
#define USB_TIMEOUT			250			/* timeout in ms for usb transfer */
#define USB_WAIT_ON_ERROR	250			/* delay between unsuccessful usb retries */

#define INPUT_BUFFER_MAXLEN	1024		/* TCP commmand string buffer size */
#define MSG_BUFFER_MAXLEN	2048		/* TCP return message string buffer size */

#define CMD_DELIMITER		",;&"		/* Command line command delimiter */
#define MAX_CMDS			500			/* Max number of commands per command line */
#define TOKEN_DELIMITER 	" ,;\t\v\f" /* Command line token delimiter */


/* program parameter defaults */
#define DEF_DAEMON		false
#define DEF_DEBUG		false
#define DEF_SYSLOG		false
#define DEF_PORT		3456
#define DEF_HOUSECODE	0x0000
#define DEF_PIDFILE		"/var/run/lightmanager.pid"


/* Several output flags for handle_input() and subfunctions */
#define HANDLE_INPUT_NOOK	1	// SET if the additional successful "OK" at the end of a command will be suppressed
#define HANDLE_INPUT_HTML	2	// SET if output should be in HTML format


/* ======================================================================== */
/* Global vars */
/* ======================================================================== */
/* program parameter variables */
bool fDaemon;
bool fDebug;
bool fsyslog;
unsigned int port;
unsigned long s_addr;
unsigned int housecode;
char pidfile[512];

/* TCP */
fd_set socks;

/* Resources */
pthread_mutex_t mutex_socks = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_usb   = PTHREAD_MUTEX_INITIALIZER;

libusb_device_handle *dev_handle;
libusb_context *usbContext;



/* ======================================================================== */
/* Prototypes */
/* ======================================================================== */
/* Non-ANSI stdlib functions */
int  stricmp(const char *s1, const char *s2);
int  strnicmp(const char *s1, const char *s2, size_t n);
char *stristr(const char *str1, const char *str2);
char *str_replace(const char *string, const char *substr, const char *replacement);
char *itoa(int value, char* result, int base);
char *ltrim(char *const s);
char *rtrim(char *const s);
char *trim(char *const s);

/* FS20 specific  */
int  fs20toi(char *fs20, char **endptr);
const char *itofs20(char *buf, int code, char *separator);

/* USB Functions */
int  usb_connect(void);
int  usb_release(void);
int  usb_send(libusb_device_handle* dev_handle, unsigned char* device_data, bool fexpectdata);
int  set_time(libusb_device_handle* dev_handle, struct tm *timeinfo);
time_t get_time(libusb_device_handle* dev_handle);

/* Helper Functions */
void debug(int priority, const char *format, ...);
FILE *openfile(const char* filename, const char* mode);
void closefile(FILE	*filehandle);
void createpidfile(const char *pidfile, pid_t pid);
void removepidfile(const char *pidfile);
void cleanup(int sig);
void endfunc(int sig);
int  write_to_client(int socket_handle, int flags, const char *format, ...);
void client_cmd_help(int socket_handle, int flags);
int  cmdcompare(const char * cs, const char * ct);
char from_hex(char ch);
char *url_decode(char *str);
void request_header(int socket_handle, int response, const char *responsetext);
void html_header(int socket_handle, const char *title);
void html_footer(int socket_handle);
int  handle_input(char* input, libusb_device_handle* dev_handle, int socket_handle, int flags);

/* TCP socket thread functions */
int  tcp_server_init(int port);
int  tcp_server_connect(int listen_sock, struct sockaddr_in *psock);
int  recbuffer(int s, void *buf, size_t len, int flags);
void tcp_server_handle_client_end(int rc, int client_fd);
void *tcp_server_handle_client(void *arg);

/* Program helper functions */
void prog_version(void);
void copyright(void);
void usage(void);


/* ======================================================================== */
/* Non-ANSI stdlib functions */
/* ======================================================================== */
int stricmp(const char *s1, const char *s2)
{
  char f, l;

  do {
    f = ((*s1 <= 'Z') && (*s1 >= 'A')) ? *s1 + 'a' - 'A' : *s1;
    l = ((*s2 <= 'Z') && (*s2 >= 'A')) ? *s2 + 'a' - 'A' : *s2;
    s1++;
    s2++;
  } while ((f) && (f == l));

  return (int) (f - l);
}

int strnicmp(const char *s1, const char *s2, size_t n)
{
  int f, l;

  do {
    if (((f = (unsigned char)(*(s1++))) >= 'A') && (f <= 'Z')) f -= 'A' - 'a';
    if (((l = (unsigned char)(*(s2++))) >= 'A') && (l <= 'Z')) l -= 'A' - 'a';
  } while (--n && f && (f == l));

  return f - l;
}


char *stristr(const char *str1, const char *str2) {
	char *cp = (char *) str1;
	char *s1, *s2;

	if (!*str2) {
		return (char *) str1;
	}

	while (*cp) {
		s1 = cp;
		s2 = (char *) str2;

		while ( *s1 && *s2 && !(toupper(*s1) - toupper(*s2)) ) {
			s1++;
			s2++;
		}
		if (!*s2) {
			return cp;
		}
		cp++;
	}

	return NULL;
}

/*
 * Create a new string with [substr] being replaced by [replacement] in [string]
 * Returns the new string, or NULL if out of memory.
 * The caller is responsible for free the new string memory
 */
char *str_replace ( const char *string, const char *substr, const char *replacement )
{
	char *tok = NULL;
	char *newstr = NULL;
	char *oldstr = NULL;
	char *head = NULL;

	/* if either substr or replacement is NULL, simply duplicate string */
	if ( substr == NULL || replacement == NULL ) {
		return strdup (string);
	}

	newstr = strdup (string);
	head = newstr;
	while ( (tok = strstr ( head, substr ))) {
		oldstr = newstr;
		newstr = malloc ( strlen ( oldstr ) - strlen ( substr ) + strlen ( replacement ) + 1 );

		if ( newstr == NULL ){
			free (oldstr);
			return NULL;
		}
		memcpy ( newstr, oldstr, tok - oldstr );
		memcpy ( newstr + (tok - oldstr), replacement, strlen ( replacement ) );
		memcpy ( newstr + (tok - oldstr) + strlen( replacement ), tok + strlen ( substr ), strlen ( oldstr ) - strlen ( substr ) - ( tok - oldstr ) );
		memset ( newstr + strlen ( oldstr ) - strlen ( substr ) + strlen ( replacement ) , 0, 1 );

		head = newstr + (tok - oldstr) + strlen( replacement );
		free (oldstr);
	}
	return newstr;
}


/** * C++ version 0.4 char* style "itoa":
	* Written by Lukás Chmela
	* Released under GPLv3.
	*/
char * itoa(int value, char* result, int base)
{
	/* check that the base if valid */
	if (base < 2 || base > 36) { *result = '\0'; return result; }

	char* ptr = result, *ptr1 = result, tmp_char;
	int tmp_value;

	do {
		tmp_value = value;
		value /= base;
		*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
	} while ( value );

	/* Apply negative sign */
	if (tmp_value < 0) *ptr++ = '-';
	*ptr-- = '\0';
	while(ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr--= *ptr1;
		*ptr1++ = tmp_char;
	}
	return result;
}


/* Remove leading whitespaces */
char *ltrim(char *const s)
{
	size_t len;
	char *cur;

	if(s && *s) {
		len = strlen(s);
		cur = s;
		while(*cur && isspace(*cur)) {
			++cur, --len;
		}

		if(s != cur) {
			memmove(s, cur, len + 1);
		}
	}

	return s;
}

/* Remove trailing whitespaces */
char *rtrim(char *const s)
{
	size_t len;
	char *cur;

	if(s && *s) {
		len = strlen(s);
		cur = s + len - 1;

		while(cur != s && isspace(*cur)) {
			--cur, --len;
		}

		cur[isspace(*cur) ? 0 : 1] = '\0';
	}

	return s;
}

/* Remove leading and trailing whitespaces */
char *trim(char *const s)
{
	rtrim(s);
	ltrim(s);

	return s;
}
/* ======================================================================== */
/* FS20 specific  */
/* ======================================================================== */

/* convert FS20 code to int
   FS20 code format: xx.yy.... or xxyy...
   where xx and yy are number of addresscodes and subaddresses
   in the format 11..44
   returns: FS20 code as integer or -1 on error
   */
int fs20toi(char *fs20, char **endptr)
{
	int res = 0;

	/* length of string must be even */
	if ( strlen(fs20)%2 != 0 ) {
		return -1;
	}

	while( *fs20 && !isspace(*fs20) ) {
		int tmp;
		res <<= 4;
		tmp  = ((*fs20++ - '0')-1) * 4;
		tmp += ((*fs20++ - '0')-1);
		res += tmp;
	}
	if( endptr != NULL ){
		*endptr = fs20;
	}
	return res;
}


/* convert integer value to FS20 code
   FS20 code format: xxyy....
   where xx and yy are number of addresscodes and subaddresses
   in the format 11..44
   If separator is given (not NULL and not character is not '\0')
   it will be used each two digits
*/
const char *itofs20(char *buf, int code, char *separator)
{
	int strpos = 0;
	int shift = (code>0xff)?12:12;

	while( shift>=0 ) {
		if( ((code>>shift) & 0x0f) < 4 ) {
			/* Add leading 0 */
			buf[strpos++]='0';
			itoa(((code>>shift) & 0x0f), &buf[strpos++], 4);
		}
		else {
			itoa(((code>>shift) & 0x0f), &buf[strpos++], 4);
			strpos++;
		}
		if( separator!=NULL && *separator != '\0' ) {
			buf[strpos++]=(*separator - 1);
		}
		shift -= 4;
	}
	if( separator!=NULL && *separator != '\0' ) {
		buf[--strpos]='\0';
	}
	strpos = 0;
	while( buf[strpos] ) {
		buf[strpos++]++;
	}
	return buf;
}


/* ======================================================================== */
/* USB Functions */
/* ======================================================================== */

/* Connects to a jbmedia Light Manager Pro(+) */
int usb_connect(void)
{
	int rc;

	/* USB connection */
	pthread_mutex_lock(&mutex_usb);
	usbContext = NULL;
	debug(LOG_DEBUG, "try to init libusb");
	rc = libusb_init(&usbContext);
	if (rc < 0) {
		debug(LOG_ERR, "libusb init error %i", rc);
		pthread_mutex_unlock(&mutex_usb);
		return EXIT_FAILURE;
	}
	debug(LOG_DEBUG, "libusb initialized");

	dev_handle = libusb_open_device_with_vid_pid(usbContext, LM_VENDOR_ID, LM_PRODUCT_ID); /* VendorID and ProductID in decimal */
	if (dev_handle == NULL ) {
		debug(LOG_DEBUG, "Cannot open USB device (vendor 0x%04x, product 0x%04x)", LM_VENDOR_ID, LM_PRODUCT_ID);
		libusb_exit(usbContext);
		pthread_mutex_unlock(&mutex_usb);
		return EXIT_FAILURE;
	}
	if (libusb_kernel_driver_active(dev_handle, 0) == 1) {
		debug(LOG_DEBUG, "Kernel driver active");
		if (libusb_detach_kernel_driver(dev_handle, 0) == 0) {
			debug(LOG_DEBUG, "Kernel driver detached!");
		} else {
			debug(LOG_DEBUG, "Kernel driver not detached!");
		}
	} else {
		debug(LOG_DEBUG, "Kernel driver not active");
	}

	rc = libusb_claim_interface(dev_handle, 0);
	if (rc < 0) {
		debug(LOG_ERR, "Error: Cannot claim interface\n");
		libusb_close(dev_handle);
		libusb_exit(usbContext);
		pthread_mutex_unlock(&mutex_usb);
		return EXIT_FAILURE;
	}
	pthread_mutex_unlock(&mutex_usb);
	return EXIT_SUCCESS;
}


/* Release connection to a jbmedia Light Manager Pro(+) */
int usb_release(void)
{
	int rc;

	pthread_mutex_lock(&mutex_usb);
	rc = libusb_release_interface(dev_handle, 0);
	if (rc != 0) {
		debug(LOG_ERR, "Cannot release interface\n");
		pthread_mutex_unlock(&mutex_usb);
		return EXIT_FAILURE;
	}
	libusb_close(dev_handle);
	libusb_exit(usbContext);
	pthread_mutex_unlock(&mutex_usb);
	return EXIT_SUCCESS;
}

/* Send raw data to jbmedia Light Manager Pro(+) */
int usb_send(libusb_device_handle* dev_handle, unsigned char* device_data, bool fexpectdata)
{
	int retry;
	int actual;
	int ret;
	int err = EXIT_SUCCESS;

	pthread_mutex_lock(&mutex_usb);
	retry = USB_MAX_RETRY;
	ret = EXIT_FAILURE;
	while( ret!=0 && retry>0 ) {
		debug(LOG_DEBUG, "usb_send(0x01) (%02x %02x %02x %02x %02x %02x %02x %02x)", device_data[0], device_data[1], device_data[2], device_data[3], device_data[4], device_data[5], device_data[6], device_data[7] );
		ret = libusb_interrupt_transfer(dev_handle, (0x01 | LIBUSB_ENDPOINT_OUT), device_data, 8, &actual, USB_TIMEOUT);
		debug(LOG_DEBUG, "usb_send(0x01) transfered: %d, returns %d (%02x %02x %02x %02x %02x %02x %02x %02x)", actual, ret, device_data[0], device_data[1], device_data[2], device_data[3], device_data[4], device_data[5], device_data[6], device_data[7] );
		retry--;
		if( ret!=0 && retry>0 ) {
			usleep( USB_WAIT_ON_ERROR*1000L );
		}
	}
	if( ret!=0 && retry==0 ) {
		err = ret;
	}

	if( fexpectdata ) {
		retry = USB_MAX_RETRY;
		ret = EXIT_FAILURE;
		while( ret!=0 && retry>0 ) {
			debug(LOG_DEBUG, "usb_send(0x82) (%02x %02x %02x %02x %02x %02x %02x %02x)", device_data[0], device_data[1], device_data[2], device_data[3], device_data[4], device_data[5], device_data[6], device_data[7] );
			ret = libusb_interrupt_transfer(dev_handle, (0x82 | LIBUSB_ENDPOINT_IN), device_data, 8, &actual, USB_TIMEOUT);
			debug(LOG_DEBUG, "usb_send(0x82) transfered: %d, returns %d (%02x %02x %02x %02x %02x %02x %02x %02x)", actual, ret, device_data[0], device_data[1], device_data[2], device_data[3], device_data[4], device_data[5], device_data[6], device_data[7] );
			retry--;
			if( ret!=0 && retry>0 ) {
				usleep( USB_WAIT_ON_ERROR*1000L );
			}
		}
		if( ret!=0 && retry==0 ) {
			err = ret;
		}
	}

	pthread_mutex_unlock(&mutex_usb);

	return err;
}

/* Set jbmedia Light Manager Pro(+) time to value within struct 'timeinfo' */
int set_time(libusb_device_handle* dev_handle, struct tm *timeinfo)
{
	int i;
	static char usbcmd[8];

	memset(usbcmd, 0, sizeof(usbcmd));

	usbcmd[0] = 0x08;
	usbcmd[1] = timeinfo->tm_sec;
	usbcmd[2] = timeinfo->tm_min;
	usbcmd[3] = timeinfo->tm_hour;
	usbcmd[4] = timeinfo->tm_mday;
	usbcmd[5] = timeinfo->tm_mon+1;
	usbcmd[6] = (timeinfo->tm_wday==0)?7:timeinfo->tm_wday;
	usbcmd[7] = timeinfo->tm_year-100;
	debug(LOG_DEBUG, "Device time set to %02d-%02d-%02d %02d:%02d:%02d", usbcmd[7], usbcmd[5], usbcmd[4], usbcmd[3], usbcmd[2], usbcmd[1]);

	for(i=1; i<8;i++) {
		usbcmd[i] = ((usbcmd[i]/10)*0x10) + (usbcmd[i]%10);
	}
	if( usb_send(dev_handle, (unsigned char *)usbcmd, false) != EXIT_SUCCESS ) {
		return 0;
	}

	memset(usbcmd, 0, sizeof(usbcmd));
	usbcmd[2] = 0x0d;
	if( usb_send(dev_handle, (unsigned char *)usbcmd, false) != EXIT_SUCCESS ) {
		return 0;
	}

	memset(usbcmd, 0, sizeof(usbcmd));
	usbcmd[0] = 0x06;
	usbcmd[1] = 0x02;
	usbcmd[2] = 0x01;
	usbcmd[3] = 0x02;
	if( usb_send(dev_handle, (unsigned char *)usbcmd, false) != EXIT_SUCCESS ) {
		return 0;
	}
}

/* Get jbmedia Light Manager Pro(+) time, returns time_t on success otherwise -1 */
time_t get_time(libusb_device_handle* dev_handle)
{
	static char usbcmd[8];
	struct tm timeinfo;
  	time_t now;
  	struct tm * currenttime;

	memset(usbcmd, 0, sizeof(usbcmd));
	usbcmd[0] = 0x09;
	if( usb_send(dev_handle, (unsigned char *)usbcmd, true) != EXIT_SUCCESS ) {
		return -1;
	}
	time(&now);
	currenttime = localtime(&now);
	memcpy(&timeinfo, currenttime, sizeof(timeinfo));

	/* ss mm hh dd MM ww yy 00 */
	timeinfo.tm_sec  = usbcmd[0];
	timeinfo.tm_min  = usbcmd[1];
	timeinfo.tm_hour = usbcmd[2];
	timeinfo.tm_mday = usbcmd[3];
	timeinfo.tm_mon  = usbcmd[4]-1;
	timeinfo.tm_year = usbcmd[6] + 100;

	debug(LOG_DEBUG, "Device timestamp returned %02d-%02d-%02d %02d:%02d:%02d", usbcmd[6], usbcmd[4], usbcmd[3], usbcmd[2], usbcmd[1], usbcmd[0]);
	return mktime(&timeinfo);
}


/* ======================================================================== */
/* Helper Functions */
/* ======================================================================== */

void debug(int priority, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	if( priority == LOG_DEBUG ) {
		if( fDebug ) {
			if( fsyslog ) {
				vsyslog(priority, format, args);
			}
			else {
				vfprintf(stdout, format, args);
				fputs("\n", stdout);
			}
		}
	}
	else {
		if( fsyslog ) {
			vsyslog(priority, format, args);
			/* Additonal output errors to stderr */
			if( priority == LOG_ERR ) {
				vfprintf(stderr, format, args);
				fputs("\n", stdout);
			}
		}
		else {
			vfprintf(stdout, format, args);
			fputs("\n", stdout);
		}
	}
	va_end(args);
}

FILE *openfile(const char* filename, const char* mode)
{
	FILE *filehandle;

	if (filehandle = fopen(filename, "r")) {
		fclose(filehandle);
	}
	filehandle = fopen ( filename, mode );
	return filehandle;
}

void closefile(FILE	*filehandle)
{
	if( filehandle==NULL )
		return;

	fclose ( filehandle	);
}

void createpidfile(const char *pidfile, pid_t pid)
{
	FILE *fpidfile = openfile(pidfile,"w");
	if( fpidfile!= NULL ) {
		fprintf(fpidfile, "%d\n", pid);
		closefile(fpidfile);
	}
}

void removepidfile(const char *pidfile)
{
	remove(pidfile);
}

void cleanup(int sig)
{
	const char *reason;

	switch (sig ) {
		case SIGINT:
			reason = "SIGINT";
			break;
		case SIGKILL:
			reason = "SIGKILL";
			break;
		case SIGTERM:
			reason = "SIGTERM";
			break;
		default:
			reason = "unknown";
			break;
	}
	removepidfile(pidfile);
	if( fDaemon ) {
		debug(LOG_INFO, "Terminate program %s v%s (build %s) - %s", PROGNAME, VERSION, BUILD, reason);
	}
}

void endfunc(int sig)
{
	if( (sig == SIGINT) ||
		(sig == SIGKILL) ||
		(sig == SIGTERM) )
	{
		cleanup(sig);
		exit (0);
	}
	return;
}
void dummyfunc(int sig)
{
}

int write_to_client(int socket_handle, int flags, const char *format, ...)
{
	va_list args;
	char msg[MSG_BUFFER_MAXLEN];
	char *sendmsg;
	int rc=0;

	va_start (args, format);
	vsprintf (msg, format, args);
	if( socket_handle != 0 ) {
		pthread_mutex_lock(&mutex_socks);
		if( flags & HANDLE_INPUT_HTML ) {
			if( (sendmsg = str_replace(msg, "\r\n", "<br />\r\n")) != NULL ) {
				rc = send(socket_handle, sendmsg, strlen(sendmsg), 0);
				free(sendmsg);
			}
		}
		else {
			rc = send(socket_handle, msg, strlen(msg), 0);
		}
		pthread_mutex_unlock(&mutex_socks);
	}
	else {
		fputs(msg, stdout);
	}
	va_end (args);

	return rc;
}

void client_cmd_help(int socket_handle, int flags)
{
	write_to_client(socket_handle, flags,
					 	"\r\n"
					 	"%s v%s (build %s) help\r\n"
						"\r\n"
					, PROGNAME, VERSION, BUILD);
	write_to_client(socket_handle, flags & ~HANDLE_INPUT_HTML,
						"%s"
						"Light Manager commands\r\n"
						"    GET CLOCK|TIME    Read the current device date and time\r\n"
						"    GET HOUSECODE     Read the current FS20 housecode\r\n"
						"    GET TEMP          Read the current device temperature sensor\r\n"
						"    SET HOUSECODE adr Set the FS20 housecode where\r\n"
						"                        adr  FS20 housecode (11111111-44444444)\r\n"
						"    SET CLOCK|TIME [time|AUTO]\r\n"
						"                      Set the device clock to system time or to <time>\r\n"
						"                      where time format is MMDDhhmm[[CC]YY][.ss]\r\n"
						"                      Use AUTO to avoid device automatic correction.\r\n"
						"\r\n"
						,(flags & HANDLE_INPUT_HTML)?"<pre>":"");
	write_to_client(socket_handle, flags & ~HANDLE_INPUT_HTML,
						"Device commands\r\n"
						"    FS20 addr cmd     Send a FS20 command where\r\n"
						"                        adr  FS20 address using the format ggss (1111-4444)\r\n"
						"                        cmd  one of the following command:\r\n"
						"                             ON|UP|OPEN      Switches ON or open a jalousie\r\n"
						"                             OFF|DOWN|CLOSE  Switches OFF or close a jalousie\r\n"
						"                             +|BRIGHT        regulate dimmer one step up\r\n"
						"                             +|DARK          regulate dimmer one step down\r\n"
						"                             <dim>           is a absoulte or percentage dim\r\n"
						"                                             value:\r\n"
						"                                             for absolute dim use 0 (min=off)\r\n"
						"                                             to 16 (max)\r\n"
						"                                             for percentage dim use 0% (off) to\r\n"
						"                                             100% (max)\r\n"
						,(flags & HANDLE_INPUT_HTML)?"<pre>":"");
	write_to_client(socket_handle, flags & ~HANDLE_INPUT_HTML,
						"    IT code addr cmd    Send an InterTechno command where\r\n"
						"                        code InterTechno housecode (A-P)\r\n"
						"                        addr InterTechno channel (1-16)\r\n"
						"                        cmd  one of the following command\r\n"
						"                             ON|UP|OPEN      Switches ON or open a jalousie\r\n"
						"                             OFF|DOWN|CLOSE  Switches OFF or close a jalousie\r\n"
						"                             +|BRIGHT        regulate dimmer one step up\r\n"
						"                             +|DARK          regulate dimmer one step down\r\n"
						"                             <dim>           is a absoulte or percentage dim\r\n"
						"                                             value:\r\n"
						"                                             for absolute dim use 0 (min=off)\r\n"
						"                                             to 248 (max)\r\n"
						"                                             for percentage dim use 0% (off) to\r\n"
						"                                             100% (max)\r\n"
						,(flags & HANDLE_INPUT_HTML)?"<pre>":"");
	write_to_client(socket_handle, flags & ~HANDLE_INPUT_HTML,
						"    UNIROLL addr cmd  Send an Uniroll command where\r\n"
						"                        adr  Uniroll jalousie number (1-100)\r\n"
						"                        cmd  Command UP|+|DOWN|-|STOP\r\n"
						"    SCENE scn         Activate scene <scn> (1-254)\r\n"
						"\r\n"
						);
	write_to_client(socket_handle, flags & ~HANDLE_INPUT_HTML,
						"System commands\r\n"
						"    ? or HELP         Prints this help\r\n"
						"    VERSION           Prints program name and version\r\n"
						"    VERBOSE           Be verbose (command and result output)\r\n"
						"    QUIET             Be quiet (no command and result output)\r\n"
						"    EXIT              Disconnect and exit server programm\r\n"
						"    QUIT              Disconnect\r\n"
						"    WAIT ms           Wait for <ms> milliseconds\r\n"
						"%s"
						,(flags & HANDLE_INPUT_HTML)?"</pre>":"");
}


int cmdcompare(const char * cs, const char * ct)
{
	/* return strnicmp(cs, ct, strlen(ct)); */
	return stricmp(cs, ct);
}

/* Converts a hex character to its integer value */
char from_hex(char ch) {
	return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Returns a url-decoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_decode(char *str) {
	char *pstr = str;
	char *buf = malloc(strlen(str) + 1);
	char *pbuf = buf;

	if( buf == NULL ) {
		return buf;
	}
	while (*pstr) {
		if (*pstr == '%') {
			if (pstr[1] && pstr[2]) {
				*pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
				pstr += 2;
			}
		}
		else if (*pstr == '+') {
			*pbuf++ = ' ';
		}
		else {
			*pbuf++ = *pstr;
		}
		pstr++;
	}
	*pbuf = '\0';

	return buf;
}

/* Writes a html request header to client using <socket_handle> */
void request_header(int socket_handle, int response, const char *responsetext)
{
  	time_t now;
  	struct tm * currenttime;
	char buffer[50];

    time(&now);
    currenttime = gmtime(&now);
    strftime (buffer,80,"%a %b %d %X %Y GMT",currenttime);

	write_to_client(socket_handle, 0,
		"HTTP/1.1 %d %s\r\n"
		"Date: %s\r\n"
		"Server: %s WEB %s (build %s)\r\n"
		"Last-Modified: %s\r\n"
		"Content-Language: %s\r\n"
		"Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\r\n"
		"Pragma: no-cache\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html\r\n"
		"\r\n"
		,response, responsetext
		,buffer
		,PROGNAME, VERSION, BUILD
		,buffer
		,"en");
}

/* Writes a html header to client using <socket_handle> */
void html_header(int socket_handle, const char *title)
{
	write_to_client(socket_handle, 0,
		"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
		"       \"http://www.w3.org/TR/html4/loose.dtd\">\r\n"
		"<html>\r\n"
		"<head>\r\n"
		"<title>%s</title>\r\n"
		"</head>\r\n"
		"<body>\r\n"
		,title);
}

/* Writes a html footer to client using <socket_handle> */
void html_footer(int socket_handle)
{
		write_to_client(socket_handle, 0,
			"</body>\r\n"
			"</html>\r\n"
			);
}

char *seterror(const char *format, ...)
{
	va_list args;
	char *errormsg;

	errormsg = malloc(strlen(format)+1024);
	if ( errormsg != NULL ) {
		va_start (args, format);
		vsprintf (errormsg, format, args);
		va_end (args);
	}

	return errormsg;
}

/* 	handle command input either via TCP socket or by a given string.
	if socket_handle is 0, then results will be given via stdout
	otherwise it wilkl be sent back via TCP to the socket client
	returns:
		 0: successful, normal
		-1: successful, client want to disconnect
		-2: successful, client want to disconnect and quit the server
		-3: sucesssful http request
*/
int handle_input(char* input, libusb_device_handle* dev_handle, int socket_handle, int flags)
{

	static char usbcmd[8];
	char cmd_delimiter[] = CMD_DELIMITER;
	char *cmds[MAX_CMDS];

	char tok_delimiter[] = TOKEN_DELIMITER;
	int i;
	char *ptr;
	bool fcmdok;
	bool quiet = false;

	debug(LOG_DEBUG, "Handle Input '%s'", input);
	if( stristr(input,"GET")==input && stristr(input,"HTTP/1.")!=NULL ) {
		char *newinput;

		*stristr(input,"HTTP/1.") = '\0';
		input = stristr(input,"/");
		if( input!=NULL ) {
			input = trim(input);
			debug(LOG_DEBUG, "Handle HTTP request '%s'", input);
			if( stristr(input,"/cmd=") ) {
				input = stristr(input,"/cmd=")+5;
				if( (ptr = url_decode(input)) ) {
					request_header(socket_handle, 200, "OK");
					html_header(socket_handle, "Lightmanager");
					handle_input(ptr, dev_handle, socket_handle, HANDLE_INPUT_HTML);
					html_footer(socket_handle);
					free(ptr);
					return -3;
				}
			}
		}
		request_header(socket_handle, 400, "Bad Request");
		html_header(socket_handle, "Error 400 - Bad Request");
		write_to_client(socket_handle, HANDLE_INPUT_HTML,
			"<h1>Error 400 - Bad Request</h1>\r\n"
			"The request cannot be fulfilled due to bad syntax.\r\n"
			"\r\n"
			"Usage&colon; <pre>http&colon;//&lt;server&gt;/cmd=<span style=\"color:blue;\">command</span>[&amp;<span style=\"color:blue;\">command</span>[...]]</pre>\r\n"
			"\r\n"
			"For possible commands see help below\r\n"
			"<pre>\r\n"
			);
		client_cmd_help(socket_handle, 0);
		write_to_client(socket_handle, 0,"</pre>\r\n");
		html_footer(socket_handle);
		return -3;
	}


	debug(LOG_DEBUG, "Handle input '%s'", input);

	i = 0;
	cmds[i] = strtok(input, cmd_delimiter);
	while( i<MAX_CMDS && cmds[i]!=NULL ) {
		cmds[++i] = strtok(NULL, cmd_delimiter);
	}
	i = 0;
	while( i<MAX_CMDS && cmds[i]!=NULL ) {
		char *command = cmds[i++];
		char *cmdexec;
		char *errormsg;

		debug(LOG_DEBUG, "Handle cmd '%s'", command);

		fcmdok = true;
		cmdexec = strdup(command);
		errormsg = NULL;

		memset(usbcmd, 0, sizeof(usbcmd));

		ptr = strtok(command, tok_delimiter);

		if( ptr != NULL ) {
			if (cmdcompare(ptr, "HELP") == 0 || cmdcompare(ptr, "H") == 0 || cmdcompare(ptr, "?") == 0) {
				client_cmd_help(socket_handle, flags);
			}
			else if (cmdcompare(ptr, "VERSION") == 0) {
				write_to_client(socket_handle, flags, "%s v%s (build %s)\r\n", PROGNAME, VERSION, BUILD);
			}
			else if (cmdcompare(ptr, "VERBOSE") == 0) {
				quiet = false;
			}
			else if (cmdcompare(ptr, "QUIET") == 0) {
				quiet = true;
			}
			/* FS20 devices */
			else if (cmdcompare(ptr, "FS20") == 0) {
				char *cp;
				int addr;
				int cmd = -1;

				/* next token: addr */
		 		ptr = strtok(NULL, tok_delimiter);
		 		if( ptr!=NULL ) {
					int addr = fs20toi(ptr, &cp);
					if ( addr >= 0 ) {
						/* next token: cmd */
				 		ptr = strtok(NULL, tok_delimiter);
				 		if( ptr!=NULL ) {
							if (cmdcompare(ptr, "ON") == 0 || cmdcompare(ptr, "UP") == 0  || cmdcompare(ptr, "OPEN") == 0) {
								cmd = 0x11;
							} else if (cmdcompare(ptr, "OFF") == 0 || cmdcompare(ptr, "DOWN") == 0  || cmdcompare(ptr, "CLOSE") == 0) {
								cmd = 0x00;
							} else if (cmdcompare(ptr, "TOGGLE") == 0) {
								cmd = 0x12;
							} else if (cmdcompare(ptr, "BRIGHT") == 0 || cmdcompare(ptr, "+") == 0 ) {
								cmd = 0x13;
							} else if (cmdcompare(ptr, "DARK") == 0 || cmdcompare(ptr, "-") == 0 ) {
								cmd = 0x14;
							}
							/* dimming case */
							else {
								errno = 0;
								int dim_value = strtol(ptr, NULL, 10);
								if( *(ptr+strlen(ptr)-1)=='\%' ) {
									dim_value = (16 * dim_value) / 100;
								}
								if (errno != 0 || dim_value < 0 || dim_value > 16) {
									cmd = -2;
									errormsg = seterror("Wrong dim level (must be within 0-16 or 0\%-100\%)");
									fcmdok = false;
								}
								else {
									cmd = 0x01 * dim_value;
								}
							}
							if (cmd >= 0) {
								usbcmd[0] = 0x01;
								usbcmd[1] = (unsigned char) (housecode >> 8);   /* Housecode high byte */
								usbcmd[2] = (unsigned char) (housecode & 0xff); /* Housecode low byte */
								usbcmd[3] = addr;
								usbcmd[4] = cmd;
								usbcmd[6] = 0x03;
								if( usb_send(dev_handle, (unsigned char *)usbcmd, false) != EXIT_SUCCESS ) {
									errormsg = seterror("USB communication error");
									fcmdok = false;
								}
							}
							else if (cmd == -1 ) {
								errormsg = seterror("unknown <cmd> parameter '%s'", ptr);
								fcmdok = false;
							}
						}
						else {
							errormsg = seterror("missing <cmd> parameter");
							fcmdok = false;
						}
					}
					else {
						errormsg = seterror("%s: wrong <addr> parameter", ptr);
						fcmdok = false;
					}
				}
				else {
					errormsg = seterror("missing <addr> parameter");
					fcmdok = false;
				}
		 	}
			/* Uniroll devices */
			else if (cmdcompare(ptr, "UNI") == 0) {
				int addr;
				int cmd = -1;

				/* next token: addr */
		 		ptr = strtok(NULL, tok_delimiter);
		 		if( ptr!=NULL ) {
					errno = 0;
					int addr = strtol(ptr, NULL, 10);
					if (errno == 0 && addr >=1 && addr <= 16) {
						/* next token: cmd */
				 		ptr = strtok(NULL, tok_delimiter);
				 		if( ptr!=NULL ) {
							if (cmdcompare(ptr, "STOP") == 0) {
								cmd = 0x02;
							} else if (cmdcompare(ptr, "UP") == 0 || cmdcompare(ptr, "+") == 0 ) {
								cmd = 0x01;
							} else if (cmdcompare(ptr, "DOWN") == 0 || cmdcompare(ptr, "-") == 0 ) {
								cmd = 0x04;
							}
							if (cmd >= 0) {
								/* 15 jj 74 cc 00 00 00 00 */
								usbcmd[0] = 0x15;
								usbcmd[1] = addr-1;
								usbcmd[2] = 0x74;
								usbcmd[3] = cmd;
								if( usb_send(dev_handle, (unsigned char *)usbcmd, false) != EXIT_SUCCESS ) {
									errormsg = seterror("USB communication error");
									fcmdok = false;
								}
							}
							else {
								errormsg = seterror("wrong <cmd> parameter '%s'", ptr);
								fcmdok = false;
							}
						}
						else {
							errormsg = seterror("missing <cmd> parameter");
							fcmdok = false;
						}
					}
					else {
						errormsg = seterror("%s: wrong <addr> parameter", ptr);
						fcmdok = false;
					}
				}
				else {
					errormsg = seterror("missing <addr> parameter");
					fcmdok = false;
				}
		 	}
			/* InterTechno devices */
			else if (cmdcompare(ptr, "IT") == 0 || cmdcompare(ptr, "InterTechno") == 0) {
				int code;
				int addr;
				int cmd = -1;

				/* next token: code */
		 		ptr = strtok(NULL, tok_delimiter);
		 		if( ptr!=NULL ) {
		 			if( toupper(*ptr)>='A' && toupper(*ptr)<='Z' ) {
		 				code = toupper(*ptr) - 'A';
						/* next token: addr */
				 		ptr = strtok(NULL, tok_delimiter);
				 		if( ptr!=NULL ) {
							errno = 0;
							int addr = strtol(ptr, NULL, 10);
							if (errno == 0 && addr >=1 && addr <= 16) {
								/* next token: cmd */
						 		ptr = strtok(NULL, tok_delimiter);
						 		if( ptr!=NULL ) {
						 			int maincmd = 0x06; /*	0x06 default for all commands except dim
						 									0x05 for dim, then cmd is the dim level (0-250) */
									if (cmdcompare(ptr, "ON") == 0 || cmdcompare(ptr, "UP") == 0  || cmdcompare(ptr, "OPEN") == 0) {
										cmd = 0x01;
									} else if (cmdcompare(ptr, "OFF") == 0 || cmdcompare(ptr, "DOWN") == 0  || cmdcompare(ptr, "CLOSE") == 0) {
										cmd = 0x00;
									} else if (cmdcompare(ptr, "TOGGLE") == 0 ) {
										cmd = 0x02;
									} else if (cmdcompare(ptr, "BRIGHT") == 0 || cmdcompare(ptr, "+") == 0 ) {
										cmd = 0x05;
									} else if (cmdcompare(ptr, "DARK") == 0 || cmdcompare(ptr, "-") == 0 ) {
										cmd = 0x06;
									}
									/* dimming case */
									else {
										errno = 0;
										maincmd = 0x05;
										int dim_value = strtol(ptr, NULL, 10);
										if( *(ptr+strlen(ptr)-1)=='\%' ) {
											dim_value = (248 * dim_value) / 100;
										}
										if (errno != 0 || dim_value < 0 || dim_value > 248) {
											cmd = -2;
											errormsg = seterror("Wrong dim level (must be within 0-248 or 0\%-100\%)");
											fcmdok = false;
										}
										else {
											cmd = 0x01 * dim_value;
										}
									}
									if (cmd >= 0) {
										usbcmd[0] = 0x05;
										usbcmd[1] = code * 0x10 + (addr - 1);
										usbcmd[2] = cmd;
										usbcmd[3] = maincmd;
										usbcmd[4] = 0x01;
										if( usb_send(dev_handle, (unsigned char *)usbcmd, false) != EXIT_SUCCESS ) {
											errormsg = seterror("USB communication error");
											fcmdok = false;
										}
									}
									else {
										errormsg = seterror("wrong <cmd> parameter '%s'", ptr);
										fcmdok = false;
									}
								}
								else {
									errormsg = seterror("missing <cmd> parameter");
									fcmdok = false;
								}
							}
							else {
								errormsg = seterror("%s: <addr> parameter out of range (must be within 1 to 16)", ptr);
								fcmdok = false;
							}
						}
						else {
							errormsg = seterror("missing <addr> parameter");
							fcmdok = false;
						}
					}
					else {
						errormsg = seterror("<code> parameter out of range (must be within 'A' to 'P')");
						fcmdok = false;
					}
				}
				else {
					errormsg = seterror("missing <code> parameter");
					fcmdok = false;
				}
		 	}
		 	/* Scene commands */
			else if (cmdcompare(ptr, "SCENE") == 0) {
				long int scene;

		 		ptr = strtok(NULL, tok_delimiter);
				if( ptr != NULL ) {
					scene = strtol(ptr, NULL, 10);
					if( scene >= 1 && scene<=254 ) {
						usbcmd[0] = 0x0f;
						usbcmd[1] = 0x01 * scene;
						if( usb_send(dev_handle, (unsigned char *)usbcmd, false) != EXIT_SUCCESS ) {
							errormsg = seterror("USB communication error");
							fcmdok = false;
						}
					}
					else {
						errormsg = seterror("parameter <s> out of range (must be within range 1-254)");
						fcmdok = false;
					}
				}
				else {
					errormsg = seterror("missing parameter");
					fcmdok = false;
				}
		 	}
		 	/* Get commands */
			else if (cmdcompare(ptr, "GET") == 0) {
				/* next token GET device */
		 		ptr = strtok(NULL, tok_delimiter);
		 		if( ptr!=NULL ) {
					if (cmdcompare(ptr, "CLOCK") == 0 ||
						cmdcompare(ptr, "TIME") == 0) {
						struct tm * currenttime;
						time_t devtime;

						devtime = get_time(dev_handle);
						if( devtime == -1 ) {
							errormsg = seterror("USB communication error");
							fcmdok = false;
						}
						else {
							currenttime = localtime(&devtime);
							write_to_client(socket_handle, flags, "%s\r\n", asctime(currenttime) );
						}
					} else if ( cmdcompare(ptr, "TEMP") == 0 || cmdcompare(ptr, "TEMPERATURE") == 0 ) {
						usbcmd[0] = 0x0c;
						if( usb_send(dev_handle, (unsigned char *)usbcmd, true) != EXIT_SUCCESS ) {
							errormsg = seterror("USB communication error");
							fcmdok = false;
						}
						else if( usbcmd[0]==0xfd ) {
							write_to_client(socket_handle, flags, "%.1f%s\r\n", (float)usbcmd[1]/2, (flags & HANDLE_INPUT_HTML)?" &deg;C":"");
						}
					} else if (cmdcompare(ptr, "HOUSECODE") == 0 ) {
						char buf[64];
						write_to_client(socket_handle, flags, "%s\r\n", itofs20(buf, housecode, NULL));
					}
					else {
						errormsg = seterror("unknown parameter '%s'", ptr);
						fcmdok = false;
					}
				}
				else {
					errormsg = seterror("missing parameter");
					fcmdok = false;
				}
		 	}
		 	/* Set commands */
			else if (cmdcompare(ptr, "SET") == 0) {
		 		ptr = strtok(NULL, tok_delimiter);
		 		/* next token SET device */
		 		if( ptr!=NULL ) {
					if (cmdcompare(ptr, "CLOCK") == 0 ||
						cmdcompare(ptr, "TIME") == 0) {
					  	time_t now;
					  	struct tm * currenttime;
						struct tm timeinfo;

				        time(&now);
				        currenttime = localtime(&now);
				        memcpy(&timeinfo, currenttime, sizeof(timeinfo));

				        /* next token new time (optional) */
				 		ptr = strtok(NULL, tok_delimiter);
				 		if( ptr!=NULL ) {
							switch( strlen(ptr) ) {
								case 8:		/* MMDDhhmm */
							        strptime(ptr, "%m%d%H%M", &timeinfo);
									break;
								case 10:	/* MMDDhhmmYY */
							        strptime(ptr, "%m%d%H%M%y", &timeinfo);
									break;
								case 11:	/* MMDDhhmm.ss */
							        strptime(ptr, "%m%d%H%M.%S", &timeinfo);
									break;
								case 12:	/* MMDDhhmmCCYY */
							        strptime(ptr, "%m%d%H%M%Y", &timeinfo);
									break;
								case 13:	/* MMDDhhmmYY.ss */
							        strptime(ptr, "%m%d%H%M%y.%S", &timeinfo);
									break;
								case 15:	/* MMDDhhmmCCYY.ss */
							        strptime(ptr, "%m%d%H%M%Y.%S", &timeinfo);
									break;
								default:
									if ( (strlen(ptr)==4 && cmdcompare(ptr, "AUTO") == 0) ||
										 (strlen(ptr)==14 && cmdcompare(ptr, "AUTOCORRECTION") == 0) ) {

										/* First check if some hour transition is done by device */
										timeinfo.tm_sec = 0;
							 			if( set_time(dev_handle, &timeinfo) != 0 ) {
											errormsg = seterror("USB communication error");
											fcmdok = false;
										}
										else {
											/* Read back time set */
											time_t devtime;
											devtime = get_time(dev_handle);
											if( devtime == -1 ) {
												errormsg = seterror("USB communication error");
												fcmdok = false;
											}
											else {
												int diff;
												/* Compare hour of time set with hour of time returned */
												currenttime = localtime(&devtime);
												diff = (timeinfo.tm_hour-currenttime->tm_hour);
												debug(LOG_DEBUG, "Device timestamp hour diff: %d", diff );
										        time(&now);
										        currenttime = localtime(&now);
												if( diff != 0 ) {
											        currenttime->tm_hour += diff;
													debug(LOG_DEBUG, "Hour corrected to %02d", currenttime->tm_hour);
												}
										        memcpy(&timeinfo, currenttime, sizeof(timeinfo));
											}
										}
									}
									else {
										errormsg = seterror("wrong paramater, use time format 'MMDDhhmm[[CC]YY][.ss]' or keyword 'AUTO'");
										fcmdok = false;
									}
									break;
							}
				 		}
				 		if( fcmdok == true ) {
				 			if( set_time(dev_handle, &timeinfo) != 0 ) {
								errormsg = seterror("USB communication error");
								fcmdok = false;
							}
				 		}
				 	}
					else if (cmdcompare(ptr, "HOUSECODE") == 0 ) {
				        /* next token new housecode */
				 		ptr = strtok(NULL, tok_delimiter);
				 		if( ptr!=NULL ) {
				 			int newhc = fs20toi(ptr, NULL);
				 			if ( newhc>= 0 ) {
				 				housecode = newhc;
				 			}
				 			else {
				 				errormsg = seterror("wrong paramater '%s'", ptr);
				 				fcmdok = false;
				 			}
						}
						else {
							errormsg = seterror("missing paramater");
							fcmdok = false;
						}
					}
					else {
						errormsg = seterror("unknown parameter '%s'", ptr);
						fcmdok = false;
					}
				}
				else {
					errormsg = seterror("missing parameter");
					fcmdok = false;
				}
			}
		 	/* Control commands */
			else if (cmdcompare(ptr, "WAIT") == 0) {
				long int ms;

		 		ptr = strtok(NULL, tok_delimiter);
				if( ptr != NULL ) {
					ms = strtol(ptr, NULL, 10);
					usleep(ms*1000L);
				}
				else {
					errormsg = seterror("missing parameter");
					fcmdok = false;
				}
		 	}
			else if (cmdcompare(ptr, "QUIT") == 0 || cmdcompare(ptr, "Q") == 0) {
				debug(LOG_DEBUG, "Client QUIT requested");
				return -1; //exit
			}
			else if (cmdcompare(ptr, "EXIT") == 0 || cmdcompare(ptr, "E") == 0) {
				debug(LOG_DEBUG, "Client EXIT requested");
				return -2; //end
			}
			else {
				errormsg = seterror("unknown command '%s'", ptr);
				fcmdok = false;
			}
		}

		/* Output executed command */
		if( !quiet && (flags & HANDLE_INPUT_NOOK)==0 ) {
			/* Output status */
			write_to_client(socket_handle, flags, "%s: %s%s\r\n", (cmdexec != NULL)?cmdexec:"<unknown>", (fcmdok)?"OK":"ERROR - ", (fcmdok)?"":((errormsg != NULL)?errormsg:"<unknown>") );
		}
		if( cmdexec != NULL ) {
			free(cmdexec);
			cmdexec = NULL;
		}
		if( errormsg != NULL ) {
			free(errormsg);
			errormsg = NULL;
		}
	}

	return 0;
}


/* ======================================================================== */
/* TCP socket thread functions */
/* ======================================================================== */

int tcp_server_init(int port)
/* Server (listen) open port - only once
 * in port: TCP server port number
 * return: Socket filedescriptor
 */
{
	int listen_fd;
	int ret;
	struct sockaddr_in sock;
	int yes = 1;

	listen_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	exit_if(listen_fd < 0);

	/* prevent "Error Address already in use" error */
	ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	exit_if(ret < 0);

	memset((char *) &sock, 0, sizeof(sock));
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = s_addr;
	sock.sin_port = htons(port);

	debug(LOG_DEBUG, "Server bind socket");
	ret = bind(listen_fd, (struct sockaddr *) &sock, sizeof(sock));
	exit_if(ret != 0);

	debug(LOG_DEBUG, "Server listening");
	ret = listen(listen_fd, 5);
	exit_if(ret < 0);

	debug(LOG_INFO, "Server now listen on port %d", port);

	return listen_fd;
}


int tcp_server_connect(int listen_sock, struct sockaddr_in *psock)
/* Client TCP connection - for each client
 * in listen_sock: Socket main filedescriptor to get client connected
 * return: Client socket filedescriptor or error
 */
{
	int fd;
	struct sockaddr_in sock;
	socklen_t socklen;

	socklen = sizeof(sock);
	fd = accept(listen_sock, (struct sockaddr *) &sock, &socklen);
	return_if(fd < 0, -1);

	if( psock != NULL ) {
		memcpy(psock, &sock, socklen);
	}
	return fd;
}

int recbuffer(int s, void *buf, size_t len, int flags)
{
	int rc;
	int slen;
	char *str;

	memset(buf, 0, len);
	str = (char *)buf;
	slen = 0;
	debug(LOG_DEBUG, "recbuffer(%d, %p, %d, %d) called", s, buf, len, flags);
	while( (rc=recv(s, str, len, flags)) > 0) {
		slen += rc;
		if( rc>0 && *(str+rc-1)=='\r' || *(str+rc-1)=='\n' ) {
			debug(LOG_DEBUG, "recbuffer() returning due to cr/lf: rc=%d", rc);
			return slen;
		}
		str += rc;
		// usleep( 50*1000L );
	}
	debug(LOG_DEBUG, "recbuffer() returning: rc=%d", rc);
	return rc;
}

void tcp_server_handle_client_end(int rc, int client_fd)
{
	debug(LOG_DEBUG, "Disconnect from client (handle %d)", client_fd);
	/* End of TCP Connection */
	pthread_mutex_lock(&mutex_socks);
	FD_CLR(client_fd, &socks);      /* remove dead client_fd */
	pthread_mutex_unlock(&mutex_socks);
	close(client_fd);
	if( rc == -2 ) {
		rc = usb_release();
		exit(rc);
	}
}

void *tcp_server_handle_client(void *arg)
/* Connected client thread
 * Handles input from clients
 * in arg: Client socket filedescriptor
 */
{
	int client_fd;
	char buf[INPUT_BUFFER_MAXLEN];
	int buflen;
	int rc;
	int wfd;

	client_fd = (int)arg;

	debug(LOG_DEBUG, "tcp_server_handle_client() thread started with client_fd = %d", client_fd);
	while(true) {
		memset(buf, 0, sizeof(buf));
		rc = recbuffer(client_fd, buf, sizeof(buf), 0);
		if ( rc <= 0 ) {
			debug(LOG_DEBUG, "tcp_server_handle_client() thread will be end due to rc = %d", rc);
			tcp_server_handle_client_end(rc, client_fd);
			pthread_exit(NULL);
		}
		else {
			rc = handle_input(trim(buf), dev_handle, client_fd, 0);
			if ( rc < 0 ) {
				if( rc > -3 ) {
					write_to_client(client_fd, 0, "bye\r\n");
				}
				tcp_server_handle_client_end(rc, client_fd);
				pthread_exit(NULL);
			}
			else {
				if( write_to_client(client_fd, 0, ">")<0 ) {
					pthread_mutex_lock(&mutex_socks);
					FD_CLR(client_fd, &socks);      /* remove dead client_fd */
					pthread_mutex_unlock(&mutex_socks);
					close(client_fd);
					pthread_exit(NULL);
				}
			}
		}
	}
	return NULL;
}




/* ======================================================================== */
/* Program helper functions */
/* ======================================================================== */

void prog_version(void)
{
	printf("%s v%s (build %s)\n", PROGNAME, VERSION, BUILD);
}

void copyright(void)
{
	puts(	"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
			"This is free software: you are free to change and redistribute it.\n"
			"There is NO WARRANTY, to the extent permitted by law.\n"
			"\n"
			"written by zwiebelchen <lars.cebu@gmail.com>\n"
			"modified by Norbert Richter <mail@norbert-richter.info>\n"
			"\n");
}

void usage(void)
{
	char buf[12];

	printf("\nUsage: lightmanager [OPTION]\n");
	printf("\n");
	printf("Options are:\n");
	printf("    -a addr       Listen on TCP <addr> for command client (default all available)\n");
	printf("    -c cmd        Execute command <cmd> and exit (separate commands by ';' or ',')\n");
	printf("    -d            Start as daemon (default %s)\n", DEF_DAEMON?"yes":"no");
	printf("    -f pidfile    PID file name and location (default %s)\n", DEF_PIDFILE);
	printf("    -g            Debug mode (default %s)\n", DEF_DEBUG?"enabled":"disabled");
	printf("    -h housecode  Use <housecode> for sending FS20 data (default %s)\n", itofs20(buf, DEF_HOUSECODE, NULL));
	printf("    -p port       Listen on TCP <port> for command client (default %d)\n", DEF_PORT);
	printf("    -s            Redirect output to syslog instead of stdout (default)\n");
	printf("    -?            Prints this help and exit\n");
	printf("    -v            Prints version and exit\n");
}


int main(int argc, char * argv[]) {
	int listen_fd;
	int rc = 0;
	pid_t pid, sid;
	char cmdexec[MSG_BUFFER_MAXLEN];

	memset(cmdexec, 0, sizeof(cmdexec));
	fDaemon = DEF_DAEMON;
	fDebug = DEF_DEBUG;
	fsyslog = DEF_SYSLOG;
	port = DEF_PORT;
	s_addr = htonl(INADDR_ANY);
	housecode = DEF_HOUSECODE;
	strncpy(pidfile, DEF_PIDFILE, sizeof(pidfile));

	while (true)
	{
		int result = getopt(argc, argv, "a:c:dgh:p:sv?");
		if (result == -1) {
			break; /* end of list */
		}
		switch (result)
		{
			case ':': /* missing argument of a parameter */
				debug(LOG_ERR, "missing argument\n");
				return EXIT_FAILURE;
				break;
			case 'a':
				s_addr = inet_addr(optarg);
				debug(LOG_DEBUG, "Listen on addreess %s", optarg);
				break;
			case 'c':
				if( fDaemon ) {
					debug(LOG_WARNING, "Starting as daemon with parameter -c is not possible, disable daemon flag");
					fDaemon = false;
				}
				debug(LOG_DEBUG, "Execute command(s) '%s'", optarg);
				strncpy(cmdexec, optarg, sizeof(cmdexec));
				break;
			case 'd':
				if( *cmdexec ) {
					debug(LOG_WARNING, "Starting as daemon with parameter -c is not possible, disable daemon flag");
				}
				else {
					fDaemon = true;
				}
				break;
			case 'f':
				memset(pidfile, '\0', sizeof(pidfile));
				strncpy(pidfile, optarg, sizeof(pidfile));
				debug(LOG_DEBUG, "pidfile %s", pidfile);
				break;
			case 'g':
				fDebug = true;
				debug(LOG_DEBUG, "%s v%s (build %s)", PROGNAME, VERSION, BUILD);
				debug(LOG_DEBUG, "Debug enabled");
				break;
			case 'h':
				{
					char buf[64];
					housecode = fs20toi(optarg, NULL);
					debug(LOG_DEBUG, "Using housecode %s (%0dd, 0x%04x, FS20=%s)", optarg, housecode, housecode, itofs20(buf, housecode, NULL));
				}
				break;
			case 'p':
				port = strtol(optarg, NULL, 10);
				debug(LOG_DEBUG, "Using TCP port %d for listening", port);
				break;
			case 's':
				fsyslog = true;
				debug(LOG_DEBUG, "Output to syslog");
				break;
			case '?': /* unknown parameter */
				prog_version();
				usage();
				return EXIT_SUCCESS;
			case 'v':
				prog_version();
				copyright();
				return EXIT_SUCCESS;
			default: /* unknown */
				break;
		}
	}
	while (optind < argc)
	{
		debug(LOG_WARNING, "Unknown parameter <%s>", argv[optind++]);
	}


	/* Starting as daemon if requested */
	if( fDaemon ) {
		debug(LOG_INFO, "Starting %s v%s (build %s) as daemon", PROGNAME, VERSION, BUILD);
		/* Fork off the parent process */
		pid = fork();
		exit_if(pid < 0);
		// If we got a good PID, then we can exit the parent process.
		if (pid > 0) {
			exit(EXIT_SUCCESS);
		}
		/* Change the file mode mask */
		umask(0);

		/* Create a new SID for the child process */
		sid = setsid();
		exit_if(sid < 0);
		pid = sid;
		/* Close out the standard file descriptors */
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}
	else {
		pid=getpid();
	}
	signal(SIGINT,endfunc);
	signal(SIGKILL,endfunc);
	signal(SIGTERM,endfunc);
	signal(SIGPIPE,dummyfunc);

	createpidfile(pidfile, pid);

	rc = usb_connect();
	if( rc == EXIT_SUCCESS ) {

		/* If command line cmd is given, execute cmd and exit */
		if( *cmdexec ) {
			rc = handle_input(trim(cmdexec), dev_handle, 0, HANDLE_INPUT_NOOK);
		}
		/* otherwise start TCP listing */
		else {
			/* open main TCP listening socket */
			listen_fd = tcp_server_init(port);
			debug(LOG_DEBUG, "tcp_server_init(%d) returns %d", port, listen_fd);
			if( listen_fd >= 0 ) {
				FD_ZERO(&socks);

				/* main loop */
				while (true) {
					struct sockaddr_in sock;
					int client_fd;
					void *arg;

					/* Check TCP server listen port (client connect) */
					client_fd = tcp_server_connect(listen_fd, &sock);
					debug(LOG_DEBUG, "tcp_server_connect((%d,...) returns %d", listen_fd, client_fd);
					if (client_fd >= 0) {
						pthread_t thread_id;
						pthread_attr_t attr;

						debug(LOG_DEBUG, "Client connected from %s (handle=%d)", inet_ntoa(sock.sin_addr), client_fd);
						pthread_mutex_lock(&mutex_socks);
						FD_SET(client_fd, &socks);
						pthread_mutex_unlock(&mutex_socks);

						/* start thread for client command handling */
						pthread_attr_init(&attr);
						/* we need to created detached threads (PTHREAD_CREATE_DETACHED),
						   so its thread ID and other resources can be reused as soon as the thread terminates. */
						pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
						arg = (void *)client_fd;
						int ret = pthread_create(&thread_id, &attr, tcp_server_handle_client, arg);
						debug(LOG_DEBUG, "client thread %sstarted (thread_id=%ul)", ret==0?"":"not ", thread_id);
						pthread_attr_destroy(&attr);
					}
				}
			}
			rc = usb_release();
		}
	}

	debug(LOG_DEBUG, "main - SIGTERM");
	cleanup(SIGTERM);
	return rc;
}

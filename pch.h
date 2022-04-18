#ifndef PCH_H
#define PCH_H

#define PROGRAM_NAME "sr71"
#include "config.h"

#define _XOPEN_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

// Terminals
#include <sys/ioctl.h>
#include <termios.h>

// Networking
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

// TLS
#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
//#include <openssl/err.h>

#include "util.h"
#include "utf8.h"

#endif

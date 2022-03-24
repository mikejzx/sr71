#ifndef PCH_H
#define PCH_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

#endif

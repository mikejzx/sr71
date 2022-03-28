#ifndef PCH_H
#define PCH_H

// (working title)
#define PROGRAM_NAME "vigem"
//#define PROGRAM_DATA_DIR "~/.local/share/" PROGRAM_NAME "/"
#define PROGRAM_DATA_DIR "."
#define TOFU_FILE_PATH PROGRAM_DATA_DIR "/trusted_hosts"
#define CACHE_PATH PROGRAM_DATA_DIR "/cache"
#define CACHE_PATH_GEMINI CACHE_PATH "/gemini"
#define CACHE_PATH_GOPHER CACHE_PATH "/gopher"

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
#include <time.h>
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

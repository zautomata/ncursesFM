#ifndef FM
#define FM

#ifdef SYSTEMD_PRESENT
#include "mount.h"
#include "install_package.h"
#endif

#include "search.h"
#include "archiver.h"
#include "worker_thread.h"

#ifdef LIBCUPS_PRESENT
#include "print.h"
#endif

#include <sys/wait.h>
#include <sys/time.h>

#ifdef LIBX11_PRESENT
#include <X11/Xlib.h>
#endif

#define FAST_BROWSE_THRESHOLD 500000
#define MILLION 1000000
#define BIG_FILE_THRESHOLD 5000000

#define NUM(a) (sizeof(a) / sizeof(*a))

void change_dir(const char *str);
void switch_hidden(void);
int is_ext(const char *filename, const char *ext[], int size);
void manage_file(const char *str, float size);
void fast_file_operations(const int index);
int remove_file(void);
void manage_space_press(const char *str);
int paste_file(void);
int move_file(void);
void change_tab(void);
void fast_browse(int c);

#endif
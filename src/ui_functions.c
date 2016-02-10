#include "../inc/ui_functions.h"

static void info_win_init(void);
static void generate_list(int win);
static int sizesort(const struct dirent **d1, const struct dirent **d2);
static int last_mod_sort(const struct dirent **d1, const struct dirent **d2);
static int typesort(const struct dirent **d1, const struct dirent **d2);
static void list_everything(int win, int old_dim, int end);
static void print_arrow(int win, int y);
static void print_border_and_title(int win);
static int is_hidden(const struct dirent *current_file);
static void initialize_tab_cwd(int win);
static void scroll_helper_func(int x, int direction);
static void colored_folders(int win, const char *name);
static void helper_print(void);
static void create_helper_win(void);
static void remove_helper_win(void);
static void show_stat(int init, int end, int win);
static void erase_stat(void);
static void info_print(const char *str, int i);
static void info_refresh(int fd);
static void inotify_refresh(int win);
static void resize_helper_win(void);
static void resize_fm_win(void);
static void check_selected(const char *str, int win, int line);
static void update_sysinfo(void);

/*
 * Struct that holds informations
 * about a fm tab
 */
struct scrstr {
    WINDOW *fm;
    int width;
    int delta;
    int stat_active;
    char tot_size[30];
};

/*
 * struct written to the pipe2 (O_DIRECT).
 */
struct info_msg {
    char *msg;
    uint8_t line;
};

static struct scrstr mywin[MAX_TABS];
static WINDOW *helper_win, *info_win;
static int dim, sorting_index;
static int (*const sorting_func[])(const struct dirent **d1, const struct dirent **d2) = {
    alphasort, sizesort, last_mod_sort, typesort
};

/*
 * Initializes screen, colors etc etc.
 */
void screen_init(void) {
    setlocale(LC_ALL, "");
    initscr();
    start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_CYAN, COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    init_pair(5, COLOR_RED, COLOR_BLACK);
    noecho();
    curs_set(0);
    raw();
    nodelay(stdscr, TRUE);
    notimeout(stdscr, TRUE);
    dim = LINES - INFO_HEIGHT;
    if (config.starting_helper) {
        create_helper_win();
        config.starting_helper = 0;
    }
    info_win_init();
    cont = 1;
    new_tab(cont - 1);
}

/*
 * Initializes info_win with proper strings for every line.
 */
static void info_win_init(void) {
    info_win = subwin(stdscr, INFO_HEIGHT, COLS, LINES - INFO_HEIGHT, 0);
    keypad(info_win, TRUE);
    nodelay(info_win, TRUE);
    notimeout(info_win, TRUE);
    for (int i = 0; i < INFO_HEIGHT - 1; i++) {     /* -1 because i won't print anything on last line (SYSINFO line) */
        mvwprintw(info_win, i, 1, "%s", info_win_str[i]);
    }
    timer_func();
}
/*
 * Clear any existing window, and close info_pipe
 */
void screen_end(void) {
    if (stdscr) {
        for (int i = 0; i < cont; i++) {
            delete_tab(i);
        }
        delwin(info_win);
        /*
         * needed: this way any print_info won't do anything:
         * to avoid a print_info from a worker thread while we're leaving
         * or after we left the program.
         */
        info_win = NULL;
        if (helper_win) {
            delwin(helper_win);
        }
        delwin(stdscr);
        endwin();
    }
}

/*
 * Creates a list of strings from current win path's files and print them to screen (list_everything)
 * If program cannot allocate memory, it will leave.
 */
static void generate_list(int win) {
    struct dirent **files;

    ps[win].number_of_files = scandir(ps[win].my_cwd, &files, is_hidden, sorting_func[sorting_index]);
    free(ps[win].nl);
    if (!(ps[win].nl = calloc(ps[win].number_of_files, PATH_MAX))) {
        quit = MEM_ERR_QUIT;
        ERROR("could not malloc. Leaving.");
    }
    str_ptr[win] = ps[win].nl;
    for (int i = 0; i < ps[win].number_of_files; i++) {
        if (!quit) {
            sprintf(ps[win].nl[i], "%s/%s", ps[win].my_cwd, files[i]->d_name);
        }
        free(files[i]);
    }
    free(files);
    if (!quit) {
        reset_win(win);
    }
}

/*
 * Callback function to scandir: list files by size.
 */
static int sizesort(const struct dirent **d1, const struct dirent **d2) {
    struct stat stat1, stat2;
    float result;

    stat((*d1)->d_name, &stat1);
    stat((*d2)->d_name, &stat2);
    result = stat1.st_size - stat2.st_size;
    return (result > 0) ? -1 : 1;
}

/*
 * Callback function to scandir: list files by last modified.
 */
static int last_mod_sort(const struct dirent **d1, const struct dirent **d2) {
    struct stat stat1, stat2;

    stat((*d1)->d_name, &stat1);
    stat((*d2)->d_name, &stat2);
    return (stat2.st_mtime - stat1.st_mtime);
}

/*
 * Callback function to scandir: list files by type.
 */
static int typesort(const struct dirent **d1, const struct dirent **d2) {
    int ret;

    if ((*d1)->d_type == (*d2)->d_type) {
        return alphasort(d1, d2);
    }
    if ((*d1)->d_type == DT_DIR) {
        ret = -1;
    } else if (((*d1)->d_type == DT_REG) && ((*d2)->d_type == DT_LNK)) {
        ret = -1;
    } else {
        ret = 1;
    }
    return ret;
}

/*
 * Clear tab, reset every var, if stat_active was idle, turn it on,
 * then call list_everything.
 */
void reset_win(int win)
{
    wclear(mywin[win].fm);
    mywin[win].delta = 0;
    ps[win].curr_pos = 0;
    if (!special_mode[win] && mywin[win].stat_active) {
        memset(mywin[win].tot_size, 0, strlen(mywin[win].tot_size));
        mywin[win].stat_active = STATS_ON;
    }
    list_everything(win, 0, 0);
}

/*
 * Prints to window 'win' "end" strings, startig from old_dim.
 * If end == 0, it means it needs to print every string until the end of available rows,
 * If stat_active == STATS_ON for 'win', and 'win' is not in special_mode, 
 * it prints stats about size and permissions for every file.
 */
static void list_everything(int win, int old_dim, int end) {
    char *str;
    int width = mywin[win].width - 5;

    if (end == 0) {
        end = dim - 2;
    }
    if (!special_mode[win]) {
        width -= STAT_LENGTH;
    }
    wattron(mywin[win].fm, A_BOLD);
    for (int i = old_dim; (i < ps[win].number_of_files) && (i  < old_dim + end); i++) {
        if (special_mode[win]) {
            str = *(str_ptr[win] + i);
        } else {
            check_selected(*(str_ptr[win] + i), win, i);
            str = strrchr(*(str_ptr[win] + i), '/') + 1;
        }
        colored_folders(win, *(str_ptr[win] + i));
        mvwprintw(mywin[win].fm, 1 + i - mywin[win].delta, 4, "%.*s", width, str);
        wattroff(mywin[win].fm, COLOR_PAIR);
    }
    print_arrow(win, 1 + ps[win].curr_pos - mywin[win].delta);
    if (!special_mode[win] && mywin[win].stat_active == STATS_ON) {
        show_stat(old_dim, end, win);
    }
    print_border_and_title(win);
}

static void print_arrow(int win, int y) {
    if (active == win) {
        wattron(mywin[win].fm, A_BOLD);
        if (fast_browse_mode[win]) {
            wattron(mywin[win].fm, COLOR_PAIR(FAST_BROWSE_COL));
        } else {
            wattron(mywin[win].fm, COLOR_PAIR(ACTIVE_COL));
        }
    }
    mvwprintw(mywin[win].fm, y, 1, "->");
    wattroff(mywin[win].fm, COLOR_PAIR);
    wattroff(mywin[win].fm, A_BOLD);
}

/*
 * Helper function that prints borders and title of 'win'.
 * to the right border's corner.
 */
static void print_border_and_title(int win) {
    wborder(mywin[win].fm, '|', '|', '-', '-', '+', '+', '+', '+');
    mvwprintw(mywin[win].fm, 0, 0, "%.*s", mywin[win].width - 1, ps[win].title);
    if (special_mode[win]) {
        mvwprintw(mywin[win].fm, 0, mywin[win].width - strlen(special_mode_title), special_mode_title);
    } else {
        mvwprintw(mywin[win].fm, 0, mywin[win].width - strlen(mywin[win].tot_size), mywin[win].tot_size);
    }
    wrefresh(mywin[win].fm);
}

/*
 * Helper function passed to scandir (in generate_list() )
 * Will return false for '.', and for every file starting with '.' (except for '..') if !show_hidden
 */
static int is_hidden(const struct dirent *current_file) {
    if (current_file->d_name[0] == '.') {
        if ((strlen(current_file->d_name) == 1) || ((!config.show_hidden) && current_file->d_name[1] != '.')) {
            return (FALSE);
        }
    }
    return (TRUE);
}

/*
 * Creates a new tab with right attributes.
 * Then calls initialize_tab_cwd().
 */
void new_tab(int win) {
    mywin[win].width = COLS / cont + win * (COLS % cont);
    mywin[win].fm = newwin(dim, mywin[win].width, 0, (COLS * win) / cont);
    keypad(mywin[win].fm, TRUE);
    scrollok(mywin[win].fm, TRUE);
    idlok(mywin[win].fm, TRUE);
    notimeout(mywin[win].fm, TRUE);
    nodelay(mywin[win].fm, TRUE);
    initialize_tab_cwd(win);
}

/*
 * Helper functions called in main.c before creating/after deleting second tab.
 */
void change_first_tab_size(void) {
    wclear(mywin[0].fm);
    mywin[0].width = COLS / cont;
    wresize(mywin[0].fm, dim, mywin[0].width);
    if (!special_mode[0] && mywin[0].stat_active) {
        mywin[0].stat_active = STATS_ON;
    }
    list_everything(0, mywin[0].delta, 0);
}

/*
 * Helper function for new_tab().
 * Calculates new tab's cwd and saves new tab's title.
 * Add an inotify_watcher on the new tab's cwd.
 * Then refreshes UI.
 */
static void initialize_tab_cwd(int win) {
    if (strlen(config.starting_dir)) {
        if ((cont == 1) || (config.second_tab_starting_dir)) {
            strcpy(ps[win].my_cwd, config.starting_dir);
        }
    }
    if (!strlen(ps[win].my_cwd)) {
        getcwd(ps[win].my_cwd, PATH_MAX);
    }
    if (change_dir(ps[win].my_cwd, win) == -1) {
        quit = GENERIC_ERR_QUIT;
        ERROR("could not scan current dir. Leaving.");
    }
}

/*
 * Removes a tab, reset its attributes,
 * frees its list of files and removes its inotify watcher.
 */
void delete_tab(int win) {
    delwin(mywin[win].fm);
    mywin[win].fm = NULL;
    memset(ps[win].my_cwd, 0, sizeof(ps[win].my_cwd));
    memset(mywin[win].tot_size, 0, strlen(mywin[win].tot_size));
    mywin[win].stat_active = STATS_OFF;
    free(ps[win].nl);
    inotify_rm_watch(inot[win].fd, inot[win].wd);
    ps[win].nl = NULL;
}

void scroll_down(void) {
    if (ps[active].curr_pos < ps[active].number_of_files - 1) {
        ps[active].curr_pos++;
        if (ps[active].curr_pos - (dim - 2) == mywin[active].delta) {
            scroll_helper_func(dim - 2, 1);
            if (!special_mode[active] && mywin[active].stat_active == STATS_IDLE) {
                mywin[active].stat_active = STATS_ON;
            }
            list_everything(active, ps[active].curr_pos, 1);
        } else {
            mvwprintw(mywin[active].fm, ps[active].curr_pos - mywin[active].delta, 1, "  ");
            print_arrow(active, ps[active].curr_pos - mywin[active].delta + 1);
            wrefresh(mywin[active].fm);
        }
    }
}

void scroll_up(void) {
    if (ps[active].curr_pos > 0) {
        ps[active].curr_pos--;
        if (ps[active].curr_pos < mywin[active].delta) {
            scroll_helper_func(1, -1);
            if (!special_mode[active] && mywin[active].stat_active == STATS_IDLE) {
                mywin[active].stat_active = STATS_ON;
            }
            list_everything(active, mywin[active].delta, 1);
        } else {
            mvwprintw(mywin[active].fm, ps[active].curr_pos - mywin[active].delta + 2, 1, "  ");
            print_arrow(active, ps[active].curr_pos - mywin[active].delta + 1);
            wrefresh(mywin[active].fm);
        }
    }
}

static void scroll_helper_func(int x, int direction) {
    mvwprintw(mywin[active].fm, x, 1, "  ");
    wborder(mywin[active].fm, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ');
    wscrl(mywin[active].fm, direction);
    mywin[active].delta += direction;
}

/*
 * Follows ls color scheme to color files/folders.
 * In search mode, it highlights paths inside archives in yellow.
 * In device mode, everything is printed in yellow.
 */
static void colored_folders(int win, const char *name) {
    struct stat file_stat;

    if (lstat(name, &file_stat) == 0) {
        if (S_ISDIR(file_stat.st_mode)) {
            wattron(mywin[win].fm, COLOR_PAIR(1));
        } else if (S_ISLNK(file_stat.st_mode)) {
            wattron(mywin[win].fm, COLOR_PAIR(3));
        } else if ((S_ISREG(file_stat.st_mode)) && (file_stat.st_mode & S_IXUSR)) {
            wattron(mywin[win].fm, COLOR_PAIR(2));
        }
    } else {
        wattron(mywin[win].fm, COLOR_PAIR(4));
    }
}

void trigger_show_helper_message(void) {
    if (!helper_win) {
        if (LINES >= HELPER_HEIGHT + INFO_HEIGHT + 3) {
            create_helper_win();
        } else {
            print_info("Window too small. Enlarge it.", ERR_LINE);
        }
    } else {
        remove_helper_win();
    }
}

/*
 * Changes "dim" global var;
 * if current position in the folder was > dim - 3 (where dim goes from 0 to dim - 1, and -2 is because of helper_win borders),
 * change it to dim - 3 + ps[i].delta.
 * Then create helper_win and print its strings.
 */
static void create_helper_win(void) {
    dim -= HELPER_HEIGHT;
    for (int i = 0; i < cont; i++) {
        wresize(mywin[i].fm, dim, mywin[i].width);
        print_border_and_title(i);
        if (ps[i].curr_pos > dim - 3 + mywin[i].delta) {
            ps[i].curr_pos = dim - 3 + mywin[i].delta;
            print_arrow(i, dim - 3 + 1);
        }
        wrefresh(mywin[i].fm);
    }
    helper_win = newwin(HELPER_HEIGHT, COLS, dim, 0);
    wclear(helper_win);
    helper_print();
}

/*
 * Remove helper_win, removes old bottom border of every fm win then resizes it.
 * Finally prints last HELPER_HEIGHT lines for each fm win.
 */
static void remove_helper_win(void) {
    wclear(helper_win);
    delwin(helper_win);
    helper_win = NULL;
    dim += HELPER_HEIGHT;
    for (int i = 0; i < cont; i++) {
        mvwhline(mywin[i].fm, dim - 1 - HELPER_HEIGHT, 0, ' ', COLS);
        wresize(mywin[i].fm, dim, mywin[i].width);
        if (!special_mode[i] && mywin[i].stat_active == STATS_IDLE) {
            mywin[i].stat_active = STATS_ON;
        }
        list_everything(i, dim - 2 - HELPER_HEIGHT + mywin[i].delta, HELPER_HEIGHT);
    }
}

static void helper_print(void) {
    const char *title = "Press 'l' to trigger helper";
    int len = (COLS - strlen(title)) / 2;

    wborder(helper_win, '|', '|', '-', '-', '+', '+', '+', '+');
    for (int i = 0; i < HELPER_HEIGHT - 2; i++) {
        mvwprintw(helper_win, i + 1, 0, "| * %.*s", COLS - 5, helper_string[i]);
    }
    wattron(helper_win, A_BOLD);
    mvwprintw(helper_win, 0, len, title);
    wattroff(helper_win, A_BOLD);
    wrefresh(helper_win);
}

/*
 * init: from where to print stats.
 * end: how many files' stats we need to print. (0 means all)
 * win: window where we need to print.
 * Prints size and perms for each of the files of the win between init and init + end.
 * Plus, calculates full folder size if mywin[win].tot_size is empty (it is emptied in generate_list,
 * so it will be empty only when a full redraw of the win is needed).
 */
static void show_stat(int init, int end, int win) {
    int check = strlen(mywin[win].tot_size);
    const int perm_bit[9] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};
    const char perm_sign[3] = {'r', 'w', 'x'};
    char str[30];
    float total_size = 0;
    struct stat file_stat;
    int perm_col = mywin[win].width - PERM_LENGTH;
    int size_col = mywin[win].width - STAT_LENGTH;

    check %= check - 1; // "check" should be 0 or 1 (strlen(tot_size) will never be 1, so i can safely divide for check - 1)
    for (int i = check * init; i < ps[win].number_of_files; i++) {
        stat(ps[win].nl[i], &file_stat);
        if (!check) {
            total_size += file_stat.st_size;
        }
        if ((i >= init) && (i < init + end)) {
            change_unit(file_stat.st_size, str);
            char x[30];
            sprintf(x, "%s\t", str);
            mvwprintw(mywin[win].fm, i + 1 - mywin[win].delta, size_col, "%s", str);
            wmove(mywin[win].fm, i + 1 - mywin[win].delta, perm_col);
            for (int j = 0; j < 9; j++) {
                wprintw(mywin[win].fm, (file_stat.st_mode & perm_bit[j]) ? "%c" : "-", perm_sign[j % 3]);
            }
            if ((i == init + end - 1) && (check)) {
                break;
            }
        }
    }
    if (!check) {
        change_unit(total_size, str);
        sprintf(mywin[win].tot_size, "Total size: %s", str);
    }
    mywin[win].stat_active = STATS_IDLE;
}

/*
 * Helper function used in show_stat: received a size,
 * it changes the unit from Kb to Mb to Gb if size > 1024(previous unit)
 */
void change_unit(float size, char *str) {
    char *unit[3] = {"KB", "MB", "GB"};
    int i = 0;

    size /= 1024;
    while ((size > 1024) && (i < 3)) {
        size /= 1024;
        i++;
    }
    sprintf(str, "%.2f%s", size, unit[i]);
}

/*
 * Called when "s" is pressed, updates stat_active, then locks ui_mutex
 * and shows stats or erase stats.
 * Then release the mutex.
 */
void trigger_stats(void) {
    mywin[active].stat_active = !mywin[active].stat_active;
    if (mywin[active].stat_active) {
        show_stat(mywin[active].delta, dim - 2, active);
    } else {
        erase_stat();
    }
    print_border_and_title(active);
}

/*
 * Move to STAT_COL and clear to eol.
 * It deletes mywin[active].tot_size too.
 */
static void erase_stat(void) {
    for (int i = 0; (i < ps[active].number_of_files) && (i < dim - 2); i++) {
        wmove(mywin[active].fm, i + 1, mywin[active].width - STAT_LENGTH);
        wclrtoeol(mywin[active].fm);
    }
    memset(mywin[active].tot_size, 0, strlen(mywin[active].tot_size));
}

/*
 * Clears i line to the end, then prints str string.
 * Then performs some checks about some "sticky messages"
 * (eg: "Pasting..." while a thread is pasting a file)
 */
static void info_print(const char *str, int i) {
    char st[100] = {0};
    int len = 1 + strlen(info_win_str[i]);

    wmove(info_win, i, len);
    wclrtoeol(info_win);
    mvwprintw(info_win, i, len, "%.*s", COLS - len, str);
    if (i == INFO_LINE) {
        if (selected) {
            strcpy(st, selected_mess);
        }
        if (thread_h) {
            sprintf(st + strlen(st), "[%d/%d] %s", thread_h->num, num_of_jobs, thread_job_mesg[thread_h->type]);
        }
        mvwprintw(info_win, INFO_LINE, COLS - strlen(st), st);
    } else if ((i == ERR_LINE) && (sv.searching)) {
        mvwprintw(info_win, ERR_LINE, COLS - strlen(searching_mess[sv.searching - 1]), searching_mess[sv.searching - 1]);
    }
    wrefresh(info_win);
}

/*
 * IF it is not called during quit:
 * it needs to malloc COLS - len bytes as they're printable chars on the screen.
 * we need malloc because window can be resized (ie: COLS is not a constant)
 * then writes on the pipe the address of its heap-allocated struct info_msg.
 */
void print_info(const char *str, int line) {
    if (info_win) {
        struct info_msg *info;
        int len = 1 + strlen(info_win_str[line]);
    
        if (!(info = malloc(sizeof(struct info_msg)))) {
            goto error;
        }
        if (!(info->msg = malloc(sizeof(char) * (COLS - len)))) {
            goto error;
        }
        strncpy(info->msg, str, COLS - len);
        info->line = line;
        size_t r = write(info_fd[1], &info, sizeof(struct info_msg *));
        if (r <= 0) {
            free(info->msg);
            free(info);
            WARN("a message could not be written.");
        }
    }
    return;
    
error:
    quit = MEM_ERR_QUIT;
    ERROR("could not malloc.");
}

void print_and_warn(const char *err, int line) {
    print_info(err, line);
    WARN(err);
}

/*
 * Given a str, a char input[d], and a char c (that is default value if enter is pressed, if dim == 1),
 * asks the user "str" and saves in input the user response.
 * I needed to replace wgetnstr function with my own wgetch cycle
 * because otherwise that would prevent KEY_RESIZE event to be managed.
 * Plus this way i can reprint the question (str) and the current answer (input) after the resize.
 */
void ask_user(const char *str, char *input, int d, char c) {
    int s, len, i = 0;
    
    print_info(str, ASK_LINE);
    do {
        s = main_poll(info_win);
        if (s == KEY_RESIZE) {
            resize_win();
            char resize_str[200];
            sprintf(resize_str, "%s%s", str, input);
            print_info(resize_str, ASK_LINE);
        } else if (s == 10) { // enter to exit
            break;
        } else if (s != ERR) {
            len = strlen(str) + strlen(info_win_str[ASK_LINE]) + i;
            if ((s == 127) && (i)) {    // backspace!
                input[--i] = '\0';
                mvwdelch(info_win, ASK_LINE, len);
            } else if (isprint(s)) {
                if (d == 1) {
                    *input = tolower(s);
                } else {
                    sprintf(input + i, "%c", s);
                }
                i++;
                mvwaddch(info_win, ASK_LINE, len + 1, s);
            }
        }
        wrefresh(info_win);
    } while ((i < d) && (!quit));
    if (i == 0) {
        input[0] = c;
    }
    print_info("", ASK_LINE);
}

/*
 * call ppoll; it is interruptable from SIGINT and SIGTERM signals.
 * It will poll for getch, timerfd, inotify, pipe and bus events.
 * If occurred event is not a getch event, return recursively main_poll
 * (ie: we don't need to come back to the cycle that called main_poll, because no buttons has been pressed)
 * else return getch value (it will be ERR if a signal has been received, as ppoll gets interrupted but
 * getch is in notimeout mode)
 * If ppoll return -1 it means: window has been resized or we received a signal (sigint/sigterm).
 * Either case, just return wgetch.
 */
int main_poll(WINDOW *win) {
    int ret = ERR - 1;
    uint64_t t;
    /*
     * resize event returns -EPERM error with ppoll (-1)
     * see here: http://keyvanfatehi.com/2011/08/03/asynchronous-c-programs-an-event-loop-and-ncurses/.
     * plus, this is needed when a signal is caught (sigint/sigterm)
     */
    int r = ppoll(main_p, nfds, NULL, &main_mask);
    if (r == -1) {
        if (!win) {
            ret = wgetch(mywin[active].fm);
        } else {
            ret = wgetch(win);
        }
    } else {
        /* check if we received more than 1 event at the same time */
        for (int i = 0; i < nfds && r > 0; i++) {
            if(main_p[i].revents & POLLIN) {
                switch (i) {
                case GETCH_IX:
                /* we received an user input */
                    if (!win) {
                        ret = wgetch(mywin[active].fm);
                    } else {
                        ret = wgetch(win);
                    }
                    break;
                case TIMER_IX:
                /* we received a timer expiration signal on timerfd */
                    read(main_p[i].fd, &t, 8);
                    timer_func();
                    break;
                case INOTIFY_IX1:
                case INOTIFY_IX2:
                /* we received an event from inotify */
                    inotify_refresh(i - INOTIFY_IX1);
                    break;
                case INFO_IX:
                /* we received an event from pipe to print a info msg */
                    info_refresh(main_p[i].fd);
                    break;
#ifdef SYSTEMD_PRESENT
                case DEVMON_IX:
                /* we received a bus event */
                    devices_bus_process();
                    break;
#endif
                }
                r--;
            }
        }
    }
    /*
     * if ret == ERR - 1, it means we did not receive a getch event.
     * it would be useless to return to main_cycle, because there would be
     * a switch case, a stat call, and other checks completely useless in this case.
     * Return directly main_poll to resume waiting on fds.
     */
    if (ret == ERR - 1) {
        return main_poll(win);
    }
    return ret;
}

/*
 * Reads from info_pipe the address of the struct previously
 * allocated on heap by print_info function(),
 * and prints it to info_win.
 * Then free all the resources.
 */
static void info_refresh(int fd) {
    struct info_msg *info;
    
    read(fd, &info, sizeof(struct info_msg *));
    info_print(info->msg, info->line);
    free(info->msg);
    free(info);
}

/*
 * thanks: http://stackoverflow.com/questions/13351172/inotify-file-in-c
 */
static void inotify_refresh(int win) {
    size_t len, i = 0;
    char buffer[BUF_LEN];
    
    len = read(inot[win].fd, buffer, BUF_LEN);
    while (i < len) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        /* ignore events for hidden files if config.show_hidden is false */
        if ((event->len) && ((event->name[0] != '.') || (config.show_hidden))) {
            if ((event->mask & IN_CREATE) || (event->mask & IN_DELETE) || event->mask & IN_MOVE) {
                tab_refresh(win);
            } else if (event->mask & IN_MODIFY || event->mask & IN_ATTRIB) {
                if (!special_mode[win] && mywin[win].stat_active) {
                    memset(mywin[win].tot_size, 0, strlen(mywin[win].tot_size));
                    show_stat(mywin[win].delta, dim - 2, win);
                    print_border_and_title(win);
                }
            }
        }
        i += EVENT_SIZE + event->len;
    }
}

/*
 * Refreshes win UI if win is not in special_mode 
 * (searching, bookmarks or device mode)
 */
void tab_refresh(int win) {
    if (!special_mode[win]) {
        generate_list(win);
    }
}

/*
 * Used to refresh special_mode windows.
 */
void update_special_mode(int num,  int win, char (*str)[PATH_MAX + 1]) {
    if (str) {
        /* Do not reset win if a device/bookmark has been added. Just print next line */
        int check = num - ps[win].number_of_files;
        ps[win].number_of_files = num;
        str_ptr[win] = str;
        if (check < 0) {
            reset_win(win);
        } else {
            list_everything(win, num - 1, 1);
        }
    } else {
        /* Only used in device_monitor: change mounted status event */
        list_everything(win, 0, 0);
    }
}

/*
 * Used when switching to special_mode.
 */
void show_special_tab(int num, char (*str)[PATH_MAX + 1], const char *title) {
    ps[active].number_of_files = num;
    str_ptr[active] = str;
    special_mode[active] = 1;
    strcpy(ps[active].title, title);
    reset_win(active);
}

/*
 * Removes info win;
 * resizes every fm win, and moves it in the new position.
 * If helper_win != NULL, resizes it too, and moves it in the new position.
 * Fixes new current_position of every fm,
 * Then recreates info win.
 * finally prints again any "sticky" message (eg: "searching"/"pasting...")
 */
void resize_win(void) {
    wclear(info_win);
    delwin(info_win);
    dim = LINES - INFO_HEIGHT;
    if (helper_win) {
        resize_helper_win();
    }
    resize_fm_win();
    info_win_init();
    print_info("", INFO_LINE);
}

/*
 * Just clear helper_win and resize it.
 * Then move it to the new position and print helper strings.
 */
static void resize_helper_win(void) {
    wclear(helper_win);
    dim -= HELPER_HEIGHT;
    wresize(helper_win, HELPER_HEIGHT, COLS);
    mvwin(helper_win, dim, 0);
    helper_print();
}

/*
 * Clear every fm win, resize it and move it to its new position.
 * Fix new win's delta with new available LINES (ie: dim).
 * Then list_everything, being careful if stat_active is ON.
 */
static void resize_fm_win(void) {
    for (int i = 0; i < cont; i++) {
        wclear(mywin[i].fm);
        mywin[i].width = COLS / cont + i * (COLS % cont);
        wresize(mywin[i].fm, dim, mywin[i].width);
        mvwin(mywin[i].fm, 0, (COLS * i) / cont);
        if (ps[i].curr_pos > dim - 3) {
            mywin[i].delta = ps[i].curr_pos - (dim - 3);
        } else {
            mywin[i].delta = 0;
        }
        if (!special_mode[i] && mywin[i].stat_active == STATS_IDLE) {
            mywin[i].stat_active = STATS_ON;
        }
        list_everything(i, mywin[i].delta, 0);
    }
}

void change_sort(void) {
    sorting_index = (sorting_index + 1) % NUM(sorting_func);
    print_info(sorting_str[sorting_index], INFO_LINE);
    for (int i = 0; i < cont; i++) {
        tab_refresh(i);
    }
}

/*
 * Called in manage_space_press() (fm_functions.c)
 * It checks for each fm win, if they're on the same cwd, then checks
 * if the file to be highlighted is visible inside "win" -> useful only if win is not the active one.
 * Then prints c char before current filename and refreshes win.
 */
void highlight_selected(int line, const char c) {
    for (int i = 0; i < cont; i++) {
        if (!special_mode[i]) {
            if ((i == active) || ((!strcmp(ps[i].my_cwd, ps[active].my_cwd))
            && (line - mywin[i].delta > 0) && (line - mywin[i].delta < dim - 2))) {
                wattron(mywin[i].fm, A_BOLD);
                mvwprintw(mywin[i].fm, 1 + line - mywin[i].delta, SEL_COL, "%c", c);
                wattroff(mywin[i].fm, A_BOLD);
                wrefresh(mywin[i].fm);
            }
        }
    }
}

static void check_selected(const char *str, int win, int line) {
    file_list *tmp = selected;

    while (tmp) {
        if (!strcmp(tmp->name, str)) {
            mvwprintw(mywin[win].fm, 1 + line - mywin[win].delta, SEL_COL, "*");
            break;
        }
        tmp = tmp->next;
    }
}

void erase_selected_highlight(void) {
    for (int j = 0; j < cont; j++) {
        for (int i = 0; (i < dim - 2) && (i < ps[j].number_of_files); i++) {
            mvwprintw(mywin[j].fm, 1 + i, SEL_COL, " ");
        }
        wrefresh(mywin[j].fm);
    }
}

void update_arrows(void) {
    print_arrow(!active, 1 + ps[!active].curr_pos - mywin[!active].delta);
    print_arrow(active, 1 + ps[active].curr_pos - mywin[active].delta);
    wrefresh(mywin[!active].fm);
    wrefresh(mywin[active].fm);
}

void switch_fast_browse_mode(void) {
    if (!fast_browse_mode[active]) {
        fast_browse_mode[active] = 1;
        print_info("Fast browse mode enabled for active tab.", INFO_LINE);
    } else {
        fast_browse_mode[active] = 0;
        print_info("Fast browse mode disabled for active tab.", INFO_LINE);
    }
    print_arrow(active, 1 + ps[active].curr_pos - mywin[active].delta);
    wrefresh(mywin[active].fm);
}

void update_time(void) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char date[30], time[10];

    wmove(info_win, SYSTEM_INFO_LINE, 1);
    wclrtoeol(info_win);
    update_sysinfo();
    sprintf(date, "%d-%d-%d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    sprintf(time, "%02d:%02d", tm.tm_hour, tm.tm_min);
    mvwprintw(info_win, SYSTEM_INFO_LINE, 1, "Date: %s, %s", date, time);
}

static void update_sysinfo(void) {
    const long minute = 60;
    const long hour = minute * 60;
    const long day = hour * 24;
    const double megabyte = 1024 * 1024;
    char sys_str[100];
    int len;
    struct sysinfo si;
    
    sysinfo(&si);
    sprintf(sys_str, "up: %ldd, %ldh, %02ldm, ", si.uptime / day, (si.uptime % day) / hour, 
                                                    (si.uptime % hour) / minute);
    len = strlen(sys_str);
    sprintf(sys_str + len, "loads: %.2f, %.2f, %.2f, ", si.loads[0] / (float)(1 << SI_LOAD_SHIFT),
                                                        si.loads[1] / (float)(1 << SI_LOAD_SHIFT),
                                                        si.loads[2] / (float)(1 << SI_LOAD_SHIFT));
    float used_ram = (si.totalram - si.freeram) / megabyte;
    len = strlen(sys_str);
    sprintf(sys_str + len, "procs: %d, ram usage: %.1fMb/%.1fMb", si.procs, used_ram, si.totalram / megabyte);
    len = strlen(sys_str);
    mvwprintw(info_win, SYSTEM_INFO_LINE, (COLS - len) / 2, "%.*s", COLS, sys_str);
}

void update_batt(int online, int perc[], int num_of_batt, char name[][10]) {
    const char *ac_online = "On AC", *fail = "No power supply info available.";
    char batt_str[20];
    int len = 0;
    
    switch (online) {
    case -1:
        /* built without libudev support. No info available. */
        mvwprintw(info_win, SYSTEM_INFO_LINE, COLS - strlen(fail), fail);
        break;
    case 1:
        /* ac connected */
        mvwprintw(info_win, SYSTEM_INFO_LINE, COLS - strlen(ac_online), ac_online);
        break;
    case 0:
        /* on battery */
        for (int i = 0; i < num_of_batt; i++) {
            sprintf(batt_str, "%s: ", name[i]);
            if (perc[i] != -1) {
                sprintf(batt_str + strlen(batt_str), "%d%%%%", perc[i]);
            } else {
                sprintf(batt_str + strlen(batt_str), "no info.");
            }
            len += strlen(batt_str) - 1;    /* -1 to delete a space derived from %%%% */
            if (perc[i] != -1 && perc[i] <= config.bat_low_level) {
                wattron(info_win, COLOR_PAIR(5));
            }
            mvwprintw(info_win, SYSTEM_INFO_LINE, COLS - len, batt_str);
            wattroff(info_win, COLOR_PAIR);
            len++;  /* if there's another bat, at least divide the two batteries by 1 space */
        }
        break;
    }
    wrefresh(info_win);
}
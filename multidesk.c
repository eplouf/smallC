/* multidesk.c
 *
 * a try to do a multi desktop manager.
 *
 * Big points are:
 *  - managing a way to create/manage many desktops
 *  - place a general hook for switching from one to another
 * once all of that is done, it launch a cmd.exe or go.exe
 *
 * copyright 2014 kilian 'eldre8' hart @afturgurluk.org
 */

/* CreateDesktop API!?!?!
 * but my own method by making HIDE or SHOW each window is much more
 * powerful, because I can then launch K!Tv in Overlay mode for example!
 */

/* TODO
 *
 *  - unstick a window
 *  - move a window to another desktop
 *  - could list the windows, opens a list of them
 *  - place a keyboard hook to replace the lame shell
 *  - integrate the runthem.c to launch applications at start
 *  - provides the shell functions
 *  - rearrange window in desktop
 *  - reload configuration of sticky windows from a config file (like '*PuTTy*')
 *  - place a clock on the cmd.exe title ?
 *  - unlimited windows (currently limited to 255)
 */

#include <windows.h>
#include <winuser.h>
#include <process.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")

#define MAX_PROC 255
#define BSIZE 255
#define OK 1
#define FAIL 0
#define DESKTOP_ALL DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW | DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL | DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP | DESKTOP_WRITEOBJECTS
#define WINSTA_ALL WINSTA_ACCESSCLIPBOARD | WINSTA_ACCESSGLOBALATOMS | WINSTA_CREATEDESKTOP | WINSTA_ENUMDESKTOPS | WINSTA_ENUMERATE | WINSTA_EXITWINDOWS | WINSTA_READATTRIBUTES | WINSTA_READSCREEN | WINSTA_WRITEATTRIBUTES

// if desk == -1 , then the window is sticky!
struct List {
  HWND PID;
    signed int desk;
};

int currentdesk=0;
struct List DeskList[MAX_PROC];     // will be enaugh for 255 shown app! it seems good...
int appindl=0;          // count of DeskList

int i;

void displayerr() {
    char *buffer;

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&buffer, 0, NULL);
    MessageBox(NULL, buffer, "MultiDesk", MB_OK);
    LocalFree(buffer);
}

void DisplayTitle(int code, HWND handle) {
    int i;
    char buffer[BSIZE+12];  // possible overflow via code! :)

    i=sprintf(buffer, "%d\n%08X - ", code, handle);
    GetWindowText(handle, &buffer[i], BSIZE-1);
    MessageBox(NULL, buffer, "MultiDesk", MB_OK);
}

int execute(char *prog) {
    int i;
    struct _STARTUPINFOA startup;
    struct _PROCESS_INFORMATION procinfo;

  memset((void*)&startup, 0, sizeof(struct _STARTUPINFOA));
    startup.cb=sizeof(struct _STARTUPINFOA);
  startup.dwFlags=STARTF_USESHOWWINDOW;
  startup.wShowWindow=SW_SHOWDEFAULT;
    i=CreateProcess(NULL, prog, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &procinfo);
    return i;
}

/* grab a window, to do some action */
HWND GrabWindow() {
    HWND handle;
    POINT point;
    HCURSOR ccurr, cnew;
    char buffer[BSIZE];
    int i;

    ccurr=GetCursor();
    LoadCursor(NULL, IDC_CROSS);
    MessageBox(NULL, "Hit enter, when ready", "multidesk", MB_OK);
    if (GetCursorPos(&point)<1)
        displayerr(0, "GetCursorPos");
    handle=WindowFromPoint(point);
    if (handle==NULL)
        displayerr(0, "WindowFromPoint");
    if (IsWindow(handle)==0)
        MessageBox(NULL, "not a window!", "multidesk", MB_OK);
    // move to top window
    while (GetParent(handle)!=0) {
        handle=GetParent(handle);
    }
    SetCursor(ccurr);
    i=sprintf(buffer, "0x%08X\n", handle);
    GetWindowText(handle, &buffer[i], BSIZE-i);
    MessageBox(NULL, buffer, "multidesk", MB_OK);

    return handle;
}

/* store the PID into the struct,
 * and check if it is already present in the case it's sticky */
int WindowInDesk(HWND handle) {
    int i;

    // check if it already exist
    for (i=0;i<appindl;i++)
        if (DeskList[i].PID==handle) {
            if (DeskList[i].desk==-1)
                return FALSE;   // sticky window
            if (DeskList[i].desk==currentdesk)
                return TRUE;     // deja stoque d'un ancien switch, on hide
            MessageBox(NULL, "fenetre.desk different!", "MultiDesk", MB_OK);
            return FALSE;    // il y a comme un ptit soucis
        }
    // handle n'etait pas dans la liste, sans doute une nouvelle fenetre
    DeskList[appindl].PID=handle;
    DeskList[appindl].desk=currentdesk;
    appindl++;
    return TRUE;
}

/* the same but with a EnumWindows proc */
int EnumWindowsProc(HWND handle, LPARAM lParam) {
    if (IsWindowVisible(handle)!=lParam)
        if (WindowInDesk(handle)==TRUE)
            ShowWindow(handle, lParam);
    return TRUE;
}

/* test if the handle is still alive */
int CheckHandle(HWND handle) {
    // get the status of the handle
    if (GetWindowThreadProcessId(handle, NULL)<1)
        return !OK;
    // ok if the handle is really alive
    return OK;
}

/* rearrange the windows list */
int RemoveWindows() {
    int i, j, count;

    count=appindl;
    for (i=0;i<appindl;i++) {
        if ((DeskList[i].PID==0) && (DeskList[i].desk==0)) {
            DeskList[i].PID=DeskList[i+1].PID;
            DeskList[i].desk=DeskList[i+1].desk;
            DeskList[i+1].PID=0;
            DeskList[i+1].desk=0;
            count--;
        }
    }
    appindl=count;

    return OK;
}

/* will take windows in the desktop num that
 * are in the DeskList,
 * and will SHOW each of them.
 */
int ActiveDesk(int num) {
    int i, j=0;

    currentdesk=num;
    // just scroll the PID list and activate the window that belongs to the desk
    for (i=0;i<=appindl;i++) {
        if (DeskList[i].desk==currentdesk) {
            // if the window was destroyed while we were away
            // or simply closed
            if (CheckHandle(DeskList[i].PID)!=OK) {
                // mark that dead
                DeskList[i].PID=0;
                DeskList[i].desk=0;
            } else {
                ShowWindow(DeskList[i].PID, SW_SHOW);
            }
        }
    }
    RemoveWindows();

    return OK;
}

/* will HIDE each window currently in SHOW mode,
 * and place it in the DeskList if not in before.
 */
int DesactiveDesk() {
    EnumWindows((void*)&EnumWindowsProc, SW_HIDE);

    return OK;
}

void Stick() {
    int i;
    HWND that;

    that=GrabWindow();
    if (that!=NULL) {
        for (i=0;i<appindl;i++)
             if (DeskList[i].PID==that) {
                 DeskList[i].desk=-1;
                 return;
             }
        // not found
        DeskList[appindl].PID=that;
        DeskList[appindl].desk=-1;
        appindl++;
    }
}

void UnStick() {
    int i;
    HWND that;

    that=GrabWindow();
    if (that!=NULL)
        for (i=0;i<appindl;i++)
             if (DeskList[i].PID==that) {
                 DeskList[i].desk=currentdesk;
                 continue;
             }
}

int main(int argc, char **argv) {
  HWND me, that;
  int i, param;
    char *buffer, buf[BSIZE];

    // some pre-init
    appindl=0;
    currentdesk=1;

    buffer=(char*)malloc(255);
    while (1) {
        printf("So? ");
        gets(buffer);
        switch (*buffer) {
            case 'e':
                displayerr();
                break;
            case 'c':
                execute(&buffer[2]);
                break;
            case 'i':
                Stick();
                break;
            case 'I':
                UnStick();
                break;
            case 's':
                // switch between desktops
                //ActualizeWinInDesk();
                if ((currentdesk==atoi(buffer+1))
                  || (atoi(buffer+1)==0))
                    break;
                DesactiveDesk();
                ActiveDesk(atoi(buffer+1));
                break;
      case 'l':
        for (i=0;i<appindl;i++) {
                    GetWindowText(DeskList[i].PID, buf, BSIZE);
          printf("0x%08X - %d - %s\n", DeskList[i].PID,
                        DeskList[i].desk, buf);
                }
        break;
      case 'a':
        sscanf(buffer+2, "%x %u", &that, &param);
        ShowWindow(that, param);
        break;
            case 'm':
                that=GrabWindow();
                param=atoi(buffer+1);
                ShowWindow(that, SW_HIDE);
                for (i=0;i<appindl;i++)
                    if (DeskList[i].PID==that) {
                        DeskList[i].desk=param;
                        continue;
                    }
                break;
            case 'q':
                free(buffer);
                exit(1);
            case '?':
                printf("e\t\tdisplay error\n");
                printf("c <cmd>\t\texecute a command\n");
                printf("i\t\tignore a window, make sticky it\n");
                printf("I\t\tunstick a window\n");
                printf("s <n>\t\tswitch to desk n\n");
                printf("l\t\tlist the DeskList struct\n");
                printf("a <handle> <bool>\tshow a window\n");
                printf("m <desk>\tmove a window to some desk\n");
                printf("q\t\tquit!\n");
                break;
            default:
                *buffer=0;
        }
    }
    return 0;
}


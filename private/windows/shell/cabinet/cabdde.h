//---------------------------------------------------------------------------
// Handle dde conversations.
//---------------------------------------------------------------------------

void  InitialiseDDE(void);
void  UnInitialiseDDE(void);
void  DDE_AddShellServices(void);
void  DDE_RemoveShellServices(void); 
DWORD GetDDEAppFlagsFromWindow(HWND hwnd);

#define DDECONV_NONE                                    0x00000000
#define DDECONV_NO_UNC                                  0x00000001
#define DDECONV_FORCED_CONNECTION                       0x00000002
#define DDECONV_REPEAT_ACKS                             0x00000004
#define DDECONV_FAIL_CONNECTS                           0x00000008
#define DDECONV_MAP_MEDIA_RECORDER                      0x00000010
#define DDECONV_NULL_FOR_STARTUP                        0x00000020
#define DDECONV_ALLOW_INVALID_CL                        0x00000040
#define DDECONV_EXPLORER_SERVICE_AND_TOPIC              0x00000080
#define DDECONV_USING_SENDMSG                           0x00000100
#define DDECONV_NO_INIT                                 0x00000200

/* Copyright (C) 1996-2001 by Salvador E. Tropea (SET),
   see copyrigh file for details */
//#define DEBUG
#include <ceditint.h>
#define Uses_stdio
#define Uses_stdlib
#define Uses_AllocLocal
#define Uses_string
#define Uses_unistd
#define Uses_ctype
#define Uses_limits

#define Uses_MsgBox
#define Uses_fpstream
#define Uses_TDialog
#define Uses_SOStack
#define Uses_TDialog
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TStringCollection

#define Uses_TSInputLine
#define Uses_TSLabel
#define Uses_TSHzLabel
#define Uses_TSLabelCheck
#define Uses_TSLabelRadio
#define Uses_TSButton
#define Uses_TSSortedListBox
#define Uses_TSVeGroup

// First include creates the dependencies
#include <easydia1.h>
#include <ceditor.h>
// Second request the headers
#include <easydiag.h>

#define Uses_SETAppDialogs
#define Uses_SETAppHelper
#define Uses_SETAppVarious
#define Uses_SETAppConst
#include <setapp.h>

#include <edmsg.h>
#include <dyncat.h>
#include <rhutils.h>
#include <splinman.h>
#include <intermp3.h>
#include <runprog.h>
#include <loadcle.h>

#ifdef TVOS_UNIX
#include <sys/wait.h>
#include <signal.h>
#endif

const int maxCommand=256;

static char     Command[maxCommand]="\x0";
static uint32   Options=0;
static char    *CurrentParser=0;
static char    *ErrorFile=0;
static char    *incCompiler;
static int      IndexCLE;
static int      incGoBack, incLines=20;
static pid_t    PidChild=0;
static char     ParsingErrors=0;
static char     PendingCleanUp=0;
static SOStack *StackPath;
static char    *RedirInputFile=0;

const char *Running=__("Running %s");
const char *BackEd=__("Back in the editor");
const char *FromPrg=__("From program:");
// Values for Options
const unsigned opUseOSScreen=1,opNeverFork=2,opAlwaysBkgd=4,opJumpFirstError=8;

void ConfigureRunCommand(void)
{
 if (!Command[0])
    strcpy(Command,"make");

 #pragma pack(1)
 struct
 {
  char   ComAux[maxCommand]  __attribute__((packed));
  uint32 Options             __attribute__((packed));
  uint32 OpsScroll           __attribute__((packed));
  char   Lines[4]            __attribute__((packed));
  TListBoxRec tl             __attribute__((packed));
 } box;
 #pragma pack()
 strcpy(box.ComAux,Command);
 box.Options=Options & (~edsmScrollMask);
 box.OpsScroll=(Options & edsmScrollMask)/edsmScrollShifter;
 sprintf(box.Lines,"%d",incLines);

 TStringCollection *list=CLEGetList();
 box.tl.items=list;
 int validList=box.tl.items && list->getCount()!=0;
 if (validList)
   {
    if (!CurrentParser)
       CurrentParser=newStr("GNU");
    if (!list->search((void *)CurrentParser,box.tl.selection))
       box.tl.selection=0;
   }

 TSViewCol *col=new TSViewCol(new TDialog(TRect(1,1,1,1),_("Command to run")));
 // ADEFJKLNPSVU
 TSLabel *progInput=new TSLabel(_("~E~nter the program name"),
         new TSInputLine(maxCommand,40));
 TSLabel *options=TSLabelCheck(__("~O~ptions"),
         __("~U~se OS screen to run the program"),
         __("~D~on't try to run in background"),
         __("~A~lways parse in background"),
         __("~J~ump to the first error"),0);
 TSLabel *opsscroll=TSLabelRadio(__("Message window ~s~croll"),
         __("E~v~er"),__("~N~ever"),__("Only if not ~f~ocused"),0);
 TSHzLabel *linesInput=new TSHzLabel(_("~L~ines per pass"),new TSInputLine(4));
 TSVeGroup *grp=MakeVeGroup(0,progInput,options,opsscroll,linesInput,0);
 grp->makeSameW();
 col->insert(2,1,grp);

 if (validList)
   {
    TSLabel *compilers=new TSLabel(_("Errors ~p~arser"),
                                   new TSSortedListBox(28,grp->h-1,tsslbVertical));
    col->insert(xTSRightOf,yTSUp,compilers,grp);
   }

 EasyInsertOKCancel(col);

 TDialog *d=col->doIt();
 delete col;
 d->options|=ofCentered;
 d->helpCtx=cmeConfRunCommand;

 if (execDialog(d,&box)!=cmCancel)
   {
    strcpy(Command,box.ComAux);
    Options=box.Options | (box.OpsScroll*edsmScrollShifter);
    incLines=atoi(box.Lines);
    if (validList)
      {
       delete[] CurrentParser;
       CurrentParser=newStr((char *)box.tl.items->at(box.tl.selection));
      }
   }
}

static
char *ParseFun(char *buf, FileInfo &fI, char *&fileName)
{
 char *endOfName,*endOfLine=0;
 int offset=0;
 int IsLineNumber=0;

 // Look for file name and line number
 // It fails if: The file is absolute and starts with a number
 if (ucisalpha(buf[0]) && buf[1]==':' && (!ucisdigit(buf[2])))
    offset=2;
 endOfName=strchr(buf+offset,':');
 if (endOfName)
    endOfLine=strchr(endOfName+1,':');
 /* Check if the line number is real */
 if (endOfLine && ucisdigit(endOfName[1]))
   {
    char *s;
    for (s=endOfName+2; *s!=':' && ucisdigit(*s); s++);
    IsLineNumber=*s==':';
    if (IsLineNumber)
      {// Check if the error is empty ...
       for (s=endOfLine+1; *s && ucisspace(*s); s++);
       if (!*s) // in this case invalidate it
          endOfName=0;
      }
   }
 if (!endOfName || !endOfLine || !IsLineNumber)
   {
    char *s=strstr(buf,"ntering dir");
    if (!s) s=strstr(buf,_("ntering dir"));
    if (s)
      {
       // The people that makes make if funny:
       s=strchr(s,'`');
       if (s)
         {
          char *e=strrchr(s,'\'');
          if (e)
            {
             char ActualPath[PATH_MAX];
             char v=*e;
             *e=0;
             strcpy(ActualPath,s+1);
             *e=v;
             strcat(ActualPath,"/");
             StackPath->addStr(ActualPath);
            }
         }
      }
    else
      {
       s=strstr(buf,"eaving dir");
       if (!s) s=strstr(buf,_("eaving dir"));
       if (s)
          StackPath->DestroyTop();
      }
    fI.Line=-1;
    fileName=0;
    return strdup(buf);
   }

 char *actPath=StackPath->GetStrOf(StackPath->GetTopHandle());

 char *ret;
 fI.len=strlen(endOfLine+1);
 fI.offset=endOfLine-buf+1;
 ret=strdup(buf);
 char bFile[PATH_MAX];
 if (offset || buf[0]=='/' || buf[0]=='\\')
   { // Absolute path
    *endOfName=0;
    strcpy(bFile,buf);
   }
 else
   { // Relative path
    // Put the actual directory
    strcpy(bFile,actPath);
    // Now the name
    *endOfName=0;
    strcat(bFile,buf);
   }
 // Fix it to avoid things like /dir/../dir/file
 CLY_fexpand(bFile);
 fileName=strdup(bFile);

 *endOfLine=0;
 fI.Line=atoi(endOfName+1);
 fI.Column=1;

 return ret;
}

static
char *ParseFunCLE(char *buf, FileInfo &fI, char *&fileName)
{
 fI.Line=-1;
 fileName=0;
 if (!CLEValues[IndexCLE].Pattern)
    return strdup(buf);

 int hits,len=strlen(buf);
 char fName[PATH_MAX];
 if ((hits=CLEDoSearch(buf,len,CLEValues[IndexCLE].Pattern))==0)
   { // No luck with the error pattern see others
    if (CLEValues[IndexCLE].EnterDirPat &&
        CLEValues[IndexCLE].EnterDirDir!=0xFF &&
        CLEDoSearch(buf,len,CLEValues[IndexCLE].EnterDirPat)!=0)
      { // Entering in a directory
       CLEGetMatch(CLEValues[IndexCLE].EnterDirDir,fName,PATH_MAX);
       StackPath->addStr(fName);
      }
    else
      if (CLEValues[IndexCLE].LeaveDir &&
          CLEDoSearch(buf,len,CLEValues[IndexCLE].LeaveDir)!=0)
        {
         StackPath->DestroyTop();
        }
    return strdup(buf);
   }
 // Ok, we have a match

 char *actPath=StackPath->GetStrOf(StackPath->GetTopHandle());

 CLEGetMatch(CLEValues[IndexCLE].File,fName,PATH_MAX);
 if (fName[1]==':' || fName[0]=='/' || fName[0]=='\\')
   { // Absolute path
    CLY_fexpand(fName);
    fileName=strdup(fName);
   }
 else
   { // Relative path
    char bFile[PATH_MAX];
    strcpy(bFile,actPath);
    int l=strlen(bFile);
    if (l && actPath[l-1]!='/' && actPath[l-1]!='\\')
       strcat(bFile,"/");
    strcat(bFile,fName);
    CLY_fexpand(bFile);
    fileName=strdup(bFile);
   }

 CLEGetMatch(CLEValues[IndexCLE].Line,fName,PATH_MAX);
 fI.Line=atoi(fName);
 fI.Column=1;
 CLEGetMatch(CLEValues[IndexCLE].Description,fI.offset,fI.len);

 //CLEGetMatch(CLEValues[IndexCLE].Description,fName,PATH_MAX);
 return strdup(buf);
}

static
char *nullStr()
{
 char *s;
 s=new char[1];
 *s=0;
 return s;
}

static
void RemoveErrorFile()
{
 unlink(ErrorFile);
 DeleteArray(ErrorFile);
 ErrorFile=0;
}

/**[txh]********************************************************************

  Description:
  Returns the content of the file where the last call to RunExternalProgram
stored the redirected data. @x{RunExternalProgram}. You must pass the
maximun allowed len and the function will return the real len.

  Return:
  A new allocated string even on error. If error ocurred the new string
have 0 lenght.

***************************************************************************/

char *RunExternalProgramGetFile(int &len)
{
 char *s;
 long lMax=len;

 len=0;
 if (!ErrorFile)
    return nullStr();
 FILE *f=fopen(ErrorFile,"rb");
 if (!f)
    s=nullStr();
 else
   {
    long lFile;
    fseek(f,0,SEEK_END);
    lFile=ftell(f);
    fseek(f,0,SEEK_SET);
    if (lFile>lMax)
       lFile=lMax;
   
    s=new char[lFile+1];
    if (fread(s,lFile,1,f)!=1)
      {
       delete s;
       s=nullStr();
      }
    else
       len=(int)lFile;
    s[lFile]=0; // Make it ASCIIZ

    fclose(f);
   }

 RemoveErrorFile();
 return s;
}

/**[txh]********************************************************************

  Description:
  Deletes the file where the redirection is stored.

***************************************************************************/

void RunExternalProgramKillFile(void)
{
 if (!ErrorFile)
    return;
 RemoveErrorFile();
}

/**[txh]********************************************************************

  Description:
  Returns the pid of owr child. If no child is running it returns 0.
  
***************************************************************************/

pid_t RunExternalProgramGetChildPid()
{
 return PidChild;
}

/**[txh]********************************************************************

  Description:
  Returns True if we are running a command or parsir their ouptup and hence
we can run another.
  
***************************************************************************/

Boolean RunExternalProgramRunning()
{
 return PidChild || ParsingErrors ? True : False;
}

/**[txh]********************************************************************

  Description:
  If we still collecting information from a child process it says the user
we can't run another process.
  
  Return:
  True if we are free to run another process.
  
***************************************************************************/

Boolean RunExternalProgramNotRunning()
{
 if (RunExternalProgramRunning())
   {
    messageBox(_("We are running a child process. Please wait or stop it."),
               mfError | mfOKButton);
    return False;
   }
 return True;
}

static
void IncCleanUp()
{
 uint32 scrlOps=Options & edsmScrollMask;
 if (!PendingCleanUp)
   {
    TView::disableCommand(cmeStopChild);
    DumpFileToMessageEnd();
    RemoveErrorFile();
    delete StackPath;
    StackPath=0;
    DeleteArray(incCompiler);
   }
 // Check if the desktop is executing a modal dialog
 TView *p=TApplication::deskTop->current;
 if (p && (p->state & sfModal))
   {// Yes, wait.
    if (!PendingCleanUp)
      {
       PendingCleanUp=1;
       EdShowMessage(_("Waiting ..."),scrlOps | edsmDontSelect);
      }
    return;
   }
 // Ok, we can go on
 ParsingErrors=0;
 EdShowMessage(_(BackEd),scrlOps);
 if (incGoBack)
   {
    if (Options & opJumpFirstError)
       EdJumpToFirstError();
    else
      if (scrlOps==edsmEverScroll)
         EdJumpToMessage(0);
   }
 ReLoadModifEditors();
}

/**[txh]********************************************************************

  Description:
  Incrementally parses the errors. Just one error will be parsed and only if
available. Should be used when RunExternalProgram didn't parse the file
and we won't to do it in "background", specially if our child is running in
parallel.
  It should be called from the TApplication::idle() member.
  
***************************************************************************/

void RunExternalProgramIncParse()
{
 if (!PidChild && !ParsingErrors)
    return;
 if (PidChild && !ParsingErrors)
   {// First call
    ParsingErrors=1;
    if (PidChild==-1)
       PidChild=0;
    incGoBack=0;
    IndexCLE=CLEGetIndexOfLoad(incCompiler);
    DumpFileToMessageInit(ErrorFile,FromPrg,Options & edsmScrollMask,
                          IndexCLE<0 ? ParseFun : ParseFunCLE);
   }

 if (PidChild)
   {
    // See if our child still alive
    #ifdef TVOS_UNIX
    int status;
    // If waitpid returns the number of our child means it communicated
    // to as a termination status
    if (waitpid(PidChild,&status,WNOHANG)==PidChild)
       PidChild=0;
    #else
    PidChild=0;
    #endif
   }

 int Finished=DumpFileToMessageParseMore(incLines,&incGoBack);

 if (Finished && !PidChild)
   {// Finished
    IncCleanUp();
   }
}

/**[txh]********************************************************************

  Description:
  Kills a running child and stops the "background" message parsing.
  
***************************************************************************/

void RunExternalProgramStopChild()
{
 if (!PidChild && !ParsingErrors)
    return;
 if (PidChild)
   {
    #ifdef TVOS_UNIX
    // See if our child still alive
    int status;
    dbprintf("runprog: killing: %d\n",PidChild);
    // 0 means the child doesn't communicate a termination status
    if (waitpid(PidChild,&status,WNOHANG)==0)
      {
       dbprintf("runprog: sending SIGTERM\n");
       // Kill the group (session in fact)
       kill(-PidChild,SIGTERM);
       // Wait a little bit to let the OS really kill it
       usleep(100000);
       if (waitpid(PidChild,&status,WNOHANG)==0)
         {// It doesn't say is finished, ok force it to die
          dbprintf("runprog: sending SIGKILL\n");
          kill(-PidChild,SIGKILL);
          waitpid(PidChild,&status,0);
         }
      }
    #endif
    PidChild=0;
   }
 EdShowMessage(_("Process interrupted"));
 IncCleanUp();
}

/**[txh]********************************************************************

  Description:
  Saves the indicated data to a temporal file. It can be used to redirect
the input of a program when calling RunExternalProgram with the repRedirIn
option. Use RunExternalProgramRemoveInRedir to release the used resources.
@x{RunExternalProgramRemoveInRedir}.
  
***************************************************************************/

void RunExternalProgramSetInRedir(const char *buffer, unsigned len)
{
 free(RedirInputFile);
 RedirInputFile=unique_name("in",0);
 FILE *f=fopen(RedirInputFile,"wb");
 if (f)
   {
    fwrite(buffer,len,1,f);
    fclose(f);
   }
}

/**[txh]********************************************************************

  Description:
  Releases all the resourses allocated by RunExternalProgramSetInRedir.
@x{RunExternalProgramSetInRedir}.
  
***************************************************************************/

void RunExternalProgramRemoveInRedir()
{
 if (RedirInputFile)
   {
    unlink(RedirInputFile);
    free(RedirInputFile);
    RedirInputFile=0;
   }
}

/**[txh]********************************************************************

  Description:
  Runs an external program passed as argument. The stderr and stdout are
redirected and the result is showed in the message window. If you don't pass
an argument the program specified by the configuration dialog is used.@*
  Valid flags are:@*
  repDontShowDialog: if the argument is null the dialog that tells about
the configuration is not showed.@*
  repDontShowAsMessage: the redirected information isn't showed in the
message box, instead the file is available calling
RunExternalProgramGetFile.@*
  repRestoreScreen: Restore the screen after running the program.@*
  repDontFork: Don't try to multitask.@*

***************************************************************************/

void RunExternalProgram(char *Program, unsigned flags, char *compiler)
{
 // If for some reason we still running a command don't run another
 if (PidChild || ParsingErrors) return;

 char *s=Command;
 if (Program)
    s=Program;
 if (!compiler)
    compiler=CurrentParser;

 if (!s || *s==0)
   {
    if ((flags & repDontShowDialog)==0)
       messageBox(_("You must configure it first in the Options submenu"),mfError | mfOKButton);
    return;
   }

 SaveAllEditors();
 char *err=open_stderr_out();

 if ((flags & repDontShowAsMessage)==0)
   {
    int l=strlen(s)+strlen(_(Running));
    AllocLocalStr(b,l);
    sprintf(b,_(Running),s);
    EdShowMessage(b,True);
   }

 StackPath=new SOStack();
 char b[PATH_MAX];
 //getwd(b); Declared as dangerous by glibc 2, reported by ld ?!
 getcwd(b,PATH_MAX);
 strcat(b,"/");
 StackPath->addStr(b);

 // Redirect the input if needed
 strcpy(b,s);
 if ((flags & repRedirIn) && RedirInputFile && strlen(RedirInputFile)+
     strlen(s)+4<PATH_MAX)
   {
    strcat(b," < ");
    strcat(b,RedirInputFile);
   }

 int saveScreen=(Options & opUseOSScreen) || (flags & repRestoreScreen);
 if (saveScreen)
    FullSuspendScreen();

 MP3Suspend;
 TV_System(b,saveScreen || (flags & repDontFork) || (Options & opNeverFork) ?
           0 : &PidChild);
 MP3Resume;

 if (saveScreen)
    FullResumeScreen();

 close_stderr_out();
 if (!PidChild && (flags & repDontShowAsMessage)==0)
   {
    if (Options & opAlwaysBkgd)
      {
       PidChild=-1;
       ErrorFile=newStr(err);
      }
    else
      {
       int goBack=0;
       uint32 scrlOps=Options & edsmScrollMask;
       TProgram::deskTop->lock();
       IndexCLE=CLEGetIndexOfLoad(compiler);
       goBack=DumpFileToMessage(err,FromPrg,scrlOps,
                                IndexCLE<0 ? ParseFun : ParseFunCLE);
       SpLinesUpdate();
       ErrorFile=0;
       EdShowMessage(_(BackEd),scrlOps);
       if (goBack)
         {
          if (Options & opJumpFirstError)
             EdJumpToFirstError();
          else
            if (scrlOps==edsmEverScroll)
               EdJumpToMessage(0);
         }
       TProgram::deskTop->unlock();
      }
   }
 else
    ErrorFile=newStr(err);

 if (PidChild)
   {
    incCompiler=newStr(compiler);
    TView::enableCommand(cmeStopChild);
    dbprintf("runprog: pid child: %d\n",PidChild);
   }
 else
   {
    delete StackPath;
    StackPath=0;
    ReLoadModifEditors();
   }
}

void SaveRunCommand(fpstream &s)
{
 if (Command[0])
   {
    s << (char)4;
    s.writeString(Command);
    s << Options << incLines;
    s.writeString(CurrentParser);
    return;
   }
 s << (char)0;
}

void LoadRunCommand(fpstream &s)
{
 char version;

 s >> version;
 if (version)
   {
    s.readString(Command,maxCommand);
    if (version>=2)
       s >> Options;
    if (version>=4)
       s >> incLines;
    if (version>=3)
       CurrentParser=s.readString();
   }
 if (!CurrentParser)
    CurrentParser=newStr("GNU");
}

void RunExternalProgramFreeMemory()
{
 if (CurrentParser)
    delete[] CurrentParser;
}

/******************************************************************************/
/*                                                                            */
/*                    X r d X r o o t d C o n f i g . c c                     */
/*                                                                            */
/* (c) 2010 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*                DE-AC02-76-SFO0515 with the Deprtment of Energy             */
/*                                                                            */
/* This file is part of the XRootD software suite.                            */
/*                                                                            */
/* XRootD is free software: you can redistribute it and/or modify it under    */
/* the terms of the GNU Lesser General Public License as published by the     */
/* Free Software Foundation, either version 3 of the License, or (at your     */
/* option) any later version.                                                 */
/*                                                                            */
/* XRootD is distributed in the hope that it will be useful, but WITHOUT      */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public       */
/* License for more details.                                                  */
/*                                                                            */
/* You should have received a copy of the GNU Lesser General Public License   */
/* along with XRootD in a file called COPYING.LESSER (LGPL license) and file  */
/* COPYING (GPL license).  If not, see <http://www.gnu.org/licenses/>.        */
/*                                                                            */
/* The copyright holder's institutional names and contributor's names may not */
/* be used to endorse or promote products derived from this software without  */
/* specific prior written permission of the institution or contributor.       */
/******************************************************************************/
 
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <string>
#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#ifdef __solaris__
#include <sys/isa_defs.h>
#endif

#include <limits>

#include "XrdVersion.hh"

#include "XProtocol/XProtocol.hh"

#include "XrdSfs/XrdSfsFlags.hh"
#include "XrdSfs/XrdSfsInterface.hh"
#include "XrdNet/XrdNetOpts.hh"
#include "XrdNet/XrdNetSocket.hh"
#include "XrdOuc/XrdOuca2x.hh"
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdOuc/XrdOucProg.hh"
#include "XrdOuc/XrdOucReqID.hh"
#include "XrdOuc/XrdOucString.hh"
#include "XrdOuc/XrdOucStream.hh"
#include "XrdOuc/XrdOucTrace.hh"
#include "XrdOuc/XrdOucUtils.hh"
#include "XrdSec/XrdSecLoadSecurity.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdSys/XrdSysHeaders.hh"
#include "XrdSys/XrdSysLogger.hh"

#include "XrdXrootd/XrdXrootdAdmin.hh"
#include "XrdXrootd/XrdXrootdAio.hh"
#include "XrdXrootd/XrdXrootdCallBack.hh"
#include "XrdXrootd/XrdXrootdFile.hh"
#include "XrdXrootd/XrdXrootdFileLock.hh"
#include "XrdXrootd/XrdXrootdFileLock1.hh"
#include "XrdXrootd/XrdXrootdGSReal.hh"
#include "XrdXrootd/XrdXrootdJob.hh"
#include "XrdXrootd/XrdXrootdMonitor.hh"
#include "XrdXrootd/XrdXrootdPrepare.hh"
#include "XrdXrootd/XrdXrootdProtocol.hh"
#include "XrdXrootd/XrdXrootdStats.hh"
#include "XrdXrootd/XrdXrootdTrace.hh"
#include "XrdXrootd/XrdXrootdTransit.hh"
#include "XrdXrootd/XrdXrootdXPath.hh"

#include "Xrd/XrdBuffer.hh"
#include "Xrd/XrdInet.hh"

/******************************************************************************/
/*         P r o t o c o l   C o m m a n d   L i n e   O p t i o n s          */
/******************************************************************************/
  
/* This is the XRootd server. The syntax is:

   xrootd [options]

   options: [<xopt>] [-r] [-t] [-y] [path]

Where:
   xopt   are xrd specified options that are screened out.

   -r     This is a redirecting server.

   -t     This server is a redirection target.

   -y     This server is a proxy server.

    path  Export path. Any number of paths may be specified.
          By default, only '/tmp' is exported.

*/
/******************************************************************************/
/*                               G l o b a l s                                */
/******************************************************************************/

                XrdOucString      *XrdXrootdCF;

extern          XrdOucTrace       *XrdXrootdTrace;

                XrdXrootdPrepare  *XrdXrootdPrepQ;

                const char        *XrdXrootdInstance;

                int                XrdXrootdPort;

extern XrdSfsFileSystem *XrdXrootdloadFileSystem(XrdSysError *,
                                                 XrdSfsFileSystem *,
                                                 const char *,
                                                 const char *, XrdOucEnv *);
extern XrdSfsFileSystem *XrdSfsGetDefaultFileSystem
                         (XrdSfsFileSystem *nativeFS,
                          XrdSysLogger     *Logger,
                          const char       *configFn,
                          XrdOucEnv        *EnvInfo);

/******************************************************************************/
/*                         L o c a l   S t a t i c s                          */
/******************************************************************************/

namespace
{
char                    *digParm  = 0;
char                    *FSLib[2] = {0,0};
std::vector<std::string> FSLPath;
char                    *gpfLib  = 0;// Normally zero for default
char                    *gpfParm = 0;
char                    *SecLib;
}
  
/******************************************************************************/
/*                             C o n f i g u r e                              */
/******************************************************************************/
  
int XrdXrootdProtocol::Configure(char *parms, XrdProtocol_Config *pi)
{
/*
  Function: Establish configuration at load time.

  Input:    None.

  Output:   0 upon success or !0 otherwise.
*/

   extern XrdSfsFileSystem *XrdDigGetFS
                            (XrdSfsFileSystem *nativeFS,
                             XrdSysLogger     *Logger,
                             const char       *configFn,
                             const char       *theParms);
   extern int optind, opterr;

   XrdOucEnv xrootdEnv;
   XrdXrootdXPath *xp;
   char *adminp, *rdf, *bP, *tmp, c, buff[1024];
   int i, n, deper = 0;

// Copy out the special info we want to use at top level
//
   eDest.logger(pi->eDest->logger());
   XrdXrootdTrace = new XrdOucTrace(&eDest);
   SI           = new XrdXrootdStats(pi->Stats);
   Sched        = pi->Sched;
   BPool        = pi->BPool;
   hailWait     = pi->hailWait;
   readWait     = pi->readWait;
   Port         = pi->Port;
   myInst       = pi->myInst;
   Window       = pi->WSize;
   tlsPort      = pi->tlsPort;
   tlsCtx       = pi->tlsCtx;
   XrdXrootdCF  = pi->totalCF;

// Record globally accessible values
//
   XrdXrootdInstance = pi->myInst;
   XrdXrootdPort     = pi->Port;

// Set the callback object static areas now!
//
   XrdXrootdCallBack::setVals(&eDest, SI, Sched, Port);

// Prohibit this program from executing as superuser
//
   if (geteuid() == 0)
      {eDest.Emsg("Config", "Security reasons prohibit xrootd running as "
                  "superuser; xrootd is terminating.");
       _exit(8);
      }

// Process any command line options
//
   opterr = 0; optind = 1;
   if (pi->argc > 1 && '-' == *(pi->argv[1]))
      while ((c=getopt(pi->argc,pi->argv,"mrst")) && ((unsigned char)c != 0xff))
     { switch(c)
       {
       case 'r': deper = 1;
                 XrdOucEnv::Export("XRDREDIRECT", "R");
                 break;
       case 'm': XrdOucEnv::Export("XRDREDIRECT", "R");
                 break;
       case 't': deper = 1;
                 XrdOucEnv::Export("XRDRETARGET", "1");
                 break;
       case 's': XrdOucEnv::Export("XRDRETARGET", "1");
                 break;
       case 'y': XrdOucEnv::Export("XRDREDPROXY", "1");
                 break;
       default:  eDest.Say("Config warning: ignoring invalid option '",pi->argv[optind-1],"'.");
       }
     }

// Check for deprecated options
//
   if (deper) eDest.Say("Config warning: '-r -t' are deprecated; use '-m -s' instead.");

// Pick up exported paths
//
   for ( ; optind < pi->argc; optind++) xexpdo(pi->argv[optind]);

// Pre-initialize some i/o values. Note that we now set maximum readv element
// transfer size to the buffer size (before it was a reasonable 256K).
//
   if (!(as_miniosz = as_segsize/2)) as_miniosz = as_segsize;
   n = (pi->theEnv ? pi->theEnv->GetInt("MaxBuffSize") : 0);
   maxTransz = maxBuffsz = (n ? n : BPool->MaxSize());
   memset(Route, 0, sizeof(Route));

// Now process and configuration parameters
//
   rdf = (parms && *parms ? parms : pi->ConfigFN);
   if (rdf && Config(rdf)) return 0;
   if (pi->DebugON) XrdXrootdTrace->What = TRACE_ALL;

// Check if we are exporting a generic object name
//
   if (XPList.Opts() & XROOTDXP_NOSLASH)
      {eDest.Say("Config exporting ", XPList.Path(n)); n += 2;}
      else n = 0;

// Check if we are exporting anything
//
   if (!(xp = XPList.Next()) && !n)
      {XPList.Insert("/tmp"); n = 8;
       eDest.Say("Config warning: only '/tmp' will be exported.");
      } else {
       while(xp) {eDest.Say("Config exporting ", xp->Path(i));
                  n += i+2; xp = xp->Next();
                 }
      }

// Export the exports
//
   bP = tmp = (char *)malloc(n);
   if (XPList.Opts() & XROOTDXP_NOSLASH)
      {strcpy(bP, XPList.Path(i)); bP += i, *bP++ = ' ';}
   xp = XPList.Next();
   while(xp) {strcpy(bP, xp->Path(i)); bP += i; *bP++ = ' '; xp = xp->Next();}
   *(bP-1) = '\0';
   XrdOucEnv::Export("XRDEXPORTS", tmp); free(tmp);

// Initialize the security system if this is wanted
//
   if (!ConfigSecurity(xrootdEnv, pi->ConfigFN)) return 0;

// Set up the network for self-identification and display it
//
   pi->NetTCP->netIF.Port(Port);
   pi->NetTCP->netIF.Display("Config ");

// Establish our specific environment that will be passed along
//
   xrootdEnv.PutPtr("XrdInet*", (void *)(pi->NetTCP));
   xrootdEnv.PutPtr("XrdNetIF*", (void *)(&(pi->NetTCP->netIF)));
   xrootdEnv.PutPtr("XrdScheduler*", Sched);

// Copy over the xrd environment which contains plugin argv's
//
   if (pi->theEnv) xrootdEnv.PutPtr("xrdEnv*", pi->theEnv);

// Initialize monitoring (it won't do anything if it wasn't enabled)
//
   if (!XrdXrootdMonitor::Init(Sched, &eDest, pi->myName, pi->myProg,
                               myInst, Port)) return 0;

// Config g-stream objects, as needed. This needs to be done before we
// load any plugins but after we initialize monitoring.
//
   ConfigGStream(xrootdEnv);

// Get the filesystem to be used and its features
//
   if (!ConfigFS(xrootdEnv, pi->ConfigFN)) return 0;
   fsFeatures = osFS->Features();

// Check if the file system includes a custom prepare handler as this will
// affect how we handle prepare requests.
//
   if (fsFeatures & XrdSfs::hasPRP2 || xrootdEnv.Get("XRD_PrepHandler"))
      PrepareAlt = true;

// Check if the diglib should be loaded. We only support the builtin one. In
// the future we will have to change this code to be like the above.
//
   if (digParm)
      {TRACE(DEBUG, "Loading dig filesystem builtin");
       digFS = XrdDigGetFS(osFS, eDest.logger(), pi->ConfigFN, digParm);
       if (!digFS) eDest.Emsg("Config","Unable to load digFS; "
                                       "remote debugging disabled!");
      }

// Check if we are going to be processing checksums locally
//
   if (JobCKT && JobLCL)
      {XrdOucErrInfo myError("Config");
       XrdOucTList *tP = JobCKTLST;
       do {if (osFS->chksum(XrdSfsFileSystem::csSize,tP->text,0,myError))
              {eDest.Emsg("Config",tP->text,"checksum is not natively supported.");
               return 0;
              }
           tP->ival[1] = myError.getErrInfo();
           tP = tP->next;
          } while(tP);
      }

// Initialiaze for AIO
//
        if (getenv("XRDXROOTD_NOAIO")) as_noaio = 1;
   else if (!as_noaio) XrdXrootdAioReq::Init(as_segsize, as_maxperreq, as_maxpersrv);
   else eDest.Say("Config warning: asynchronous I/O has been disabled!");

// Create the file lock manager
//
   Locker = (XrdXrootdFileLock *)new XrdXrootdFileLock1();
   XrdXrootdFile::Init(Locker, &eDest, as_nosf == 0);
   if (as_nosf) eDest.Say("Config warning: sendfile I/O has been disabled!");

// Schedule protocol object cleanup (also advise the transit protocol)
//
   ProtStack.Set(pi->Sched, XrdXrootdTrace, TRACE_MEM);
   n = (pi->ConnMax/3 ? pi->ConnMax/3 : 30);
   ProtStack.Set(n, 60*60);
   XrdXrootdTransit::Init(pi->Sched, n, 60*60);

// Initialize the request ID generation object
//
   PrepID = new XrdOucReqID(pi->urAddr, (int)Port);

// Initialize for prepare processing
//
   XrdXrootdPrepQ = new XrdXrootdPrepare(&eDest, pi->Sched, PrepareAlt);
   sprintf(buff, "%%s://%s:%d/&L=%%d&U=%%s", pi->myName, pi->Port);
   Notify = strdup(buff);

// Set the redirect flag if we are a pure redirector
//
   int tlsFlags = myRole & kXR_tlsAny;
   myRole = kXR_isServer; myRolf = kXR_DataServer;
   if ((rdf = getenv("XRDREDIRECT"))
   && (!strcmp(rdf, "R") || !strcmp(rdf, "M")))
      {isRedir = *rdf;
       myRole = kXR_isManager; myRolf = kXR_LBalServer;
       if (!strcmp(rdf, "M"))  myRole |=kXR_attrMeta;
      } 
   myRole |= tlsFlags;
   if (getenv("XRDREDPROXY"))  myRole |=kXR_attrProxy;

// Check if we are redirecting anything
//
   if ((xp = RPList.Next()))
      {int k;
       char buff[2048], puff[1024];
       do {k = xp->Opts();
           if (Route[k].Host[0] == Route[k].Host[1]
           &&  Route[k].Port[0] == Route[k].Port[1]) *puff = 0;
              else sprintf(puff, "%%%s:%d", Route[k].Host[1], Route[k].Port[1]);
           sprintf(buff," to %s:%d%s",Route[k].Host[0],Route[k].Port[0],puff);
           eDest.Say("Config redirect static ", xp->Path(), buff);
           xp = xp->Next();
          } while(xp);
      }

   if ((xp = RQList.Next()))
      {int k;
       const char *cgi1, *cgi2;
       char buff[2048], puff[1024], xCgi[RD_Num] = {0};
       if (isRedir) {cgi1 = "+"; cgi2 = getenv("XRDCMSCLUSTERID");}
          else      {cgi1 = "";  cgi2 = pi->myName;}
       myCNlen = snprintf(buff, sizeof(buff), "%s%s", cgi1, cgi2);
       myCName = strdup(buff);
       do {k = xp->Opts();
           if (Route[k].Host[0] == Route[k].Host[1]
           &&  Route[k].Port[0] == Route[k].Port[1]) *puff = 0;
              else sprintf(puff, "%%%s:%d", Route[k].Host[1], Route[k].Port[1]);
           sprintf(buff," to %s:%d%s",Route[k].Host[0],Route[k].Port[0],puff);
           eDest.Say("Config redirect enoent ", xp->Path(), buff);
           if (!xCgi[k] && cgi2)
              {bool isdup = Route[k].Host[0] == Route[k].Host[1]
                         && Route[k].Port[0] == Route[k].Port[1];
               for (i = 0; i < 2; i++)
                   {n = snprintf(buff,sizeof(buff), "%s?tried=%s%s",
                                 Route[k].Host[i], cgi1, cgi2);
                    free(Route[k].Host[i]); Route[k].Host[i] = strdup(buff);
                    Route[k].RDSz[i] = n;
                    if (isdup) {Route[k].Host[1] = Route[k].Host[0];
                                Route[k].RDSz[1] = n; break;
                               }
                   }
              }
           xCgi[k] = 1;
           xp = xp->Next();
          } while(xp);
      }

// Add all jobs that we can run to the admin object
//
   if (JobCKS) XrdXrootdAdmin::addJob("chksum", JobCKS);

// Establish the path to be used for admin functions. We will loose this
// storage upon an error but we don't care because we'll just exit.
//
   adminp = XrdOucUtils::genPath(pi->AdmPath, 0, ".xrootd");

// Setup the admin path (used in all roles).
//
   if (!(AdminSock = XrdNetSocket::Create(&eDest, adminp, "admin", pi->AdmMode))
   ||  !XrdXrootdAdmin::Init(&eDest, AdminSock)) return 0;

// Setup pid file
//
   PidFile();

// Indicate whether or not we support extened attributes
//
  {XrdOucEnv     myEnv;
   XrdOucErrInfo eInfo("", &myEnv);
   char buff[128];
   if (osFS->FAttr(0, eInfo, 0) == SFS_OK)
      {usxMaxNsz = myEnv.GetInt("usxMaxNsz");
       if (usxMaxNsz < 0) usxMaxNsz = 0;
       usxMaxVsz = myEnv.GetInt("usxMaxVsz");
       if (usxMaxVsz < 0) usxMaxVsz = 0;
       snprintf(buff, sizeof(buff), "%d %d", usxMaxNsz, usxMaxVsz);
       usxParms  = strdup(buff);
      } else {
       usxMaxNsz = 0;
       usxMaxVsz = 0;
       usxParms  = strdup("0 0");
      }
  }

// Finally, check if we really need to be in bypass mode if it is set
//
   if (OD_Bypass)
      {const char *penv = getenv("XRDXROOTD_PROXY");
       if (!penv || *penv != '=')
          {OD_Bypass = false;
           eDest.Say("Config warning: 'fsoverload bypass' ignored; "
                                     "not a forwarding proxy.");
          }
      }

// Add any additional features
//
   if (fsFeatures & XrdSfs::hasPOSC) myRole |= kXR_supposc;
   if (fsFeatures & XrdSfs::hasPGRW) myRole |= kXR_suppgrw;
   if (fsFeatures & XrdSfs::hasGPF)  myRole |= kXR_supgpf;
   if (fsFeatures & XrdSfs::hasGPFA && myRole & kXR_supgpf)
      myRole |= kXR_anongpf;

// Finally note whether or not we have TLS enabled
//
   if (tlsCtx) myRole |= kXR_haveTLS;

// Return success
//
   free(adminp);
   return 1;
}

/******************************************************************************/
/*                                C o n f i g                                 */
/******************************************************************************/
  
#define TS_Xeq(x,m) (!strcmp(x,var)) GoNo = m(Config)

int XrdXrootdProtocol::Config(const char *ConfigFN)
{
   XrdOucEnv myEnv;
   XrdOucStream Config(&eDest, getenv("XRDINSTANCE"), &myEnv, "=====> ");
   char *var;
   int cfgFD, GoNo, NoGo = 0, ismine;

   // Open and attach the config file
   //
   if ((cfgFD = open(ConfigFN, O_RDONLY, 0)) < 0)
       return eDest.Emsg("Config", errno, "open config file", ConfigFN);
   Config.Attach(cfgFD);

   // Indicate what we are about to do in the capture stream
   //
   static const char *cvec[] = { "*** xroot protocol config:", 0 };
   Config.Capture(cvec);

   // Process items
   //
   while((var = Config.GetMyFirstWord()))
        {     if ((ismine = !strncmp("xrootd.", var, 7)) && var[7]) var += 7;
         else if ((ismine = !strcmp("all.export", var)))    var += 4;
         else if ((ismine = !strcmp("all.pidpath",var)))    var += 4;
         else if ((ismine = !strcmp("all.seclib", var)))    var += 4;

         if (ismine)
            {     if TS_Xeq("async",         xasync);
             else if TS_Xeq("chksum",        xcksum);
             else if TS_Xeq("diglib",        xdig);
             else if TS_Xeq("export",        xexp);
             else if TS_Xeq("fslib",         xfsl);
             else if TS_Xeq("fsoverload",    xfso);
             else if TS_Xeq("gpflib",        xgpf);
             else if TS_Xeq("log",           xlog);
             else if TS_Xeq("monitor",       xmon);
             else if TS_Xeq("pidpath",       xpidf);
             else if TS_Xeq("prep",          xprep);
             else if TS_Xeq("redirect",      xred);
             else if TS_Xeq("seclib",        xsecl);
             else if TS_Xeq("tls",           xtls);
             else if TS_Xeq("trace",         xtrace);
             else if TS_Xeq("limit",         xlimit);
             else {eDest.Say("Config warning: ignoring unknown directive '",var,"'.");
                   Config.Echo();
                   continue;
                  }
             if (GoNo) {Config.Echo(); NoGo = 1;}
            }
        }

// Add our config to our environment and return
//
   return NoGo;
}

/******************************************************************************/
/*                     P r i v a t e   F u n c t i o n s                      */
/******************************************************************************/
/******************************************************************************/
/*                               P i d F i l e                                */
/******************************************************************************/
  
void XrdXrootdProtocol::PidFile()
{
    int rc, xfd;
    char buff[32], pidFN[1200];
    char *ppath=XrdOucUtils::genPath(pidPath,XrdOucUtils::InstName(-1));
    const char *xop = 0;

    if ((rc = XrdOucUtils::makePath(ppath,XrdOucUtils::pathMode)))
       {xop = "create"; errno = rc;}
       else {snprintf(pidFN, sizeof(pidFN), "%s/xrootd.pid", ppath);

            if ((xfd = open(pidFN, O_WRONLY|O_CREAT|O_TRUNC,0644)) < 0)
               xop = "open";
               else {if (write(xfd,buff,snprintf(buff,sizeof(buff),"%d",
                         static_cast<int>(getpid()))) < 0) xop = "write";
                     close(xfd);
                    }
            }

    free(ppath);
    if (xop) eDest.Emsg("Config", errno, xop, pidFN);
}

/******************************************************************************/
/*                              C o n f i g F S                               */
/******************************************************************************/

bool XrdXrootdProtocol::ConfigFS(XrdOucEnv &xEnv, const char *cfn)
{
   const char *fsLoc;
   int n;

// Get the filesystem to be used
//
   if (FSLib[0])
      {TRACE(DEBUG, "Loading base filesystem library " <<FSLib[0]);
       osFS = XrdXrootdloadFileSystem(&eDest, 0, FSLib[0], cfn, &xEnv);
       fsLoc = FSLib[0];
      } else {
       osFS = XrdSfsGetDefaultFileSystem(0, eDest.logger(), cfn, &xEnv);
       fsLoc = "default";
      }

// Make sure we have loaded something
//
   if (!osFS)
      {eDest.Emsg("Config", "Unable to load base file system using", fsLoc);
       return false;
      }
   if (FSLib[0]) osFS->EnvInfo(&xEnv);

// If there is an old style wrapper, load it now.
//
   if (FSLib[1] && !ConfigFS(FSLib[1], xEnv, cfn)) return false;

// Run through any other pushdowns
//
   if ((n = FSLPath.size()))
      for (int i = 0; i < n; i++)
          {if (!ConfigFS(FSLPath[i].c_str(), xEnv, cfn)) return false;}

// Inform the statistics object which filesystem to use
//
   SI->setFS(osFS);

// All done here
//
   return true;
}

/******************************************************************************/

bool XrdXrootdProtocol::ConfigFS(const char *path, XrdOucEnv &xEnv,
                                 const char *cfn)
{

// Try to load this wrapper library
//
   TRACE(DEBUG, "Loading wrapper filesystem library " <<path);
   osFS = XrdXrootdloadFileSystem(&eDest, osFS, path, cfn, &xEnv);
   if (!osFS)
      {eDest.Emsg("Config", "Unable to load file system wrapper from", path);
       return false;
      }
   osFS->EnvInfo(&xEnv);
   return true;
}
  
/******************************************************************************/
/*                         C o n f i g G S t r e a m                          */
/******************************************************************************/

void XrdXrootdProtocol::ConfigGStream(XrdOucEnv &myEnv)
{
   struct GSTable {const char *pin; int Mode; char Type;} gsObj[] =
                  {{"ccm", XROOTD_MON_CCM, XROOTD_MON_GSCCM},
                   {"pfc", XROOTD_MON_PFC, XROOTD_MON_GSPFC}
                  };
   int numgs = sizeof(gsObj)/sizeof(struct GSTable);
   int flint = XrdXrootdMonitor::Flushing();
   char vbuff[64];

// For each enabled monitoring provider, allocate a g-stream and put
// its address in our environment.
//
   for (int i = 0; i < numgs; i++)
       {if (XrdXrootdMonitor::ModeEnabled(gsObj[i].Mode))
           {XrdXrootdGStream *gs = new XrdXrootdGSReal(gsObj[i].pin,
                                                       gsObj[i].Type,
                                                       gsObj[i].Mode, flint);
            snprintf(vbuff, sizeof(vbuff), "%s.gStream*", gsObj[i].pin);
            myEnv.PutPtr(vbuff, (void *)gs);
           }
       }
}

  
/******************************************************************************/
/*                        C o n f i g S e c u r i t y                         */
/******************************************************************************/

int XrdXrootdProtocol::ConfigSecurity(XrdOucEnv &xEnv, const char *cfn)
{
   XrdSecGetProt_t secGetProt = 0;
   char idBuff[256];
   int n;

// Obtain our uid and username
//
   myUID = geteuid();
   if ((n = XrdOucUtils::UidName(myUID, idBuff, sizeof(idBuff))))
      {myUName = strdup(idBuff);
       myUNLen = n;
      }

// Obtain our gid and groupname
//
   myGID = getegid();
   if ((n = XrdOucUtils::GidName(myGID, idBuff, sizeof(idBuff))))
      {myGName = strdup(idBuff);
       myGNLen = n;
      }

// TLS support is independent of security per se. Record context, if any.
//
   if (tlsCtx) xEnv.PutPtr("XrdTLSContext*", (void *)tlsCtx);

// Check if we need to load anything
//
   if (!SecLib)
      {eDest.Say("Config warning: 'xrootd.seclib' not specified;"
                 " strong authentication disabled!");
       xEnv.PutPtr("XrdSecGetProtocol*", (void *)0);
       xEnv.PutPtr("XrdSecProtector*"  , (void *)0);
       return 1;
      }

// Blad some debugging info
//
   TRACE(DEBUG, "Loading security library " <<SecLib);

// Load the security server
//
   if (!(CIA = XrdSecLoadSecService(&eDest, cfn,
               (strcmp(SecLib,"default") ? SecLib : 0),
               &secGetProt, &DHS)))
      {eDest.Emsg("Config", "Unable to load security system.");
       return 0;
      }

// Set environmental pointers
//
   xEnv.PutPtr("XrdSecGetProtocol*", (void *)secGetProt);
   xEnv.PutPtr("XrdSecProtector*"  , (void *)DHS);
   return 1;
}
  
/******************************************************************************/
/*                                x a s y n c                                 */
/******************************************************************************/

/* Function: xasync

   Purpose:  To parse directive: async [limit <aiopl>] [maxsegs <msegs>]
                                       [maxtot <mtot>] [segsize <segsz>]
                                       [minsize <iosz>] [maxstalls <cnt>]
                                       [force] [syncw] [off] [nosf]

             <aiopl>  maximum number of async ops per link. Default 8.
             <msegs>  maximum number of async ops per request. Default 8.
             <mtot>   maximum number of async ops per server. Default is 20%
                      of maximum connection times aiopl divided by two.
             <segsz>  The aio segment size. This is the maximum size that data
                      will be read or written. The defaults to 64K but is
                      adjusted for each request to minimize latency.
             <iosz>   the minimum number of bytes that must be read or written
                      to allow async processing to occur (default is maxbsz/2
                      typically 1M).
             <cnt>    Maximum number of client stalls before synchronous i/o is
                      used. Async mode is tried after <cnt> requests.
             force    Uses async i/o for all requests, even when not explicitly
                      requested (this is compatible with synchronous clients).
             syncw    Use synchronous i/o for write requests.
             off      Disables async i/o
             nosf     Disables use of sendfile to send data to the client.

   Output: 0 upon success or 1 upon failure.
*/

int XrdXrootdProtocol::xasync(XrdOucStream &Config)
{
    char *val;
    int  i, ppp;
    int  V_force=-1, V_syncw = -1, V_off = -1, V_mstall = -1, V_nosf = -1;
    int  V_limit=-1, V_msegs=-1, V_mtot=-1, V_minsz=-1, V_segsz=-1;
    int  V_minsf=-1;
    long long llp;
    struct asyncopts {const char *opname; int minv; int *oploc;
                      const char *opmsg;} asopts[] =
       {
        {"force",     -1, &V_force, ""},
        {"off",       -1, &V_off,   ""},
        {"nosf",      -1, &V_nosf,  ""},
        {"syncw",     -1, &V_syncw, ""},
        {"limit",      0, &V_limit, "async limit"},
        {"segsize", 4096, &V_segsz, "async segsize"},
        {"maxsegs",    0, &V_msegs, "async maxsegs"},
        {"maxstalls",  0, &V_mstall,"async maxstalls"},
        {"maxtot",     0, &V_mtot,  "async maxtot"},
        {"minsfsz",    1, &V_minsf, "async minsfsz"},
        {"minsize", 4096, &V_minsz, "async minsize"}};
    int numopts = sizeof(asopts)/sizeof(struct asyncopts);

    if (!(val = Config.GetWord()))
       {eDest.Emsg("Config", "async option not specified"); return 1;}

    while (val)
         {for (i = 0; i < numopts; i++)
              if (!strcmp(val, asopts[i].opname))
                 {if (asopts[i].minv >=  0 && !(val = Config.GetWord()))
                     {eDest.Emsg("Config","async",(char *)asopts[i].opname,
                                 "value not specified");
                      return 1;
                     }
                       if (asopts[i].minv >  0)
                          if (XrdOuca2x::a2sz(eDest,asopts[i].opmsg, val, &llp,
                                         (long long)asopts[i].minv)) return 1;
                             else *asopts[i].oploc = (int)llp;
                  else if (asopts[i].minv == 0)
                          if (XrdOuca2x::a2i(eDest,asopts[i].opmsg,val,&ppp,1))
                                            return 1;
                             else *asopts[i].oploc = ppp;
                  else *asopts[i].oploc = 1;
                  break;
                 }
          if (i >= numopts)
             eDest.Emsg("Config", "Warning, invalid async option", val);
          val = Config.GetWord();
         }

// Make sure max values are consistent
//
   if (V_limit > 0 && V_mtot > 0 && V_limit > V_mtot)
           {eDest.Emsg("Config", "async limit may not be greater than maxtot");
            return 1;
           }

// Calculate the actual segment size
//
   if (V_segsz > 0)
      {i = BPool->Recalc(V_segsz);
       if (!i) {eDest.Emsg("Config", "async segsize is too large"); return 1;}
       if (i != V_segsz)
          {char buff[64];
           sprintf(buff, "%d readjusted to %d", V_segsz, i);
           eDest.Emsg("Config", "async segsize", buff);
           V_segsz = i;
          }
      }

// Establish async options
//
   if (V_limit > 0) as_maxperlnk = V_limit;
   if (V_msegs > 0) as_maxperreq = V_msegs;
   if (V_mtot  > 0) as_maxpersrv = V_mtot;
   if (V_minsz > 0) as_miniosz   = V_minsz;
   if (V_segsz > 0) as_segsize   = V_segsz;
   if (V_mstall> 0) as_maxstalls = V_mstall;
   if (V_force > 0) as_force     = 1;
   if (V_off   > 0) as_noaio     = 1;
   if (V_syncw > 0) as_syncw     = 1;
   if (V_nosf  > 0) as_nosf      = 1;
   if (V_minsf > 0) as_minsfsz   = V_minsf;

   return 0;
}

/******************************************************************************/
/*                                x c k s u m                                 */
/******************************************************************************/

/* Function: xcksum

   Purpose:  To parse the directive: chksum [chkcgi] [max <n>] <type> [<path>]

             max       maximum number of simultaneous jobs
             chkcgi    Always check for checksum type in cgo info.
             <type>    algorithm of checksum (e.g., md5). If more than one
                       checksum is supported then they should be listed with
                       each separated by a space.
             <path>    the path of the program performing the checksum
                       If no path is given, the checksum is local.

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xcksum(XrdOucStream &Config)
{
   static XrdOucProg *theProg = 0;
   int (*Proc)(XrdOucStream *, char **, int) = 0;
   XrdOucTList *tP, *algFirst = 0, *algLast = 0;
   char *palg, prog[2048];
   int jmax = 4, anum[2] = {0,0};

// Get the algorithm name and the program implementing it
//
   JobCKCGI = 0;
   while ((palg = Config.GetWord()) && *palg != '/')
         {if (!strcmp(palg,"chkcgi")) {JobCKCGI = 1; continue;}
          if (strcmp(palg, "max"))
             {XrdOucUtils::toLower(palg);
              XrdOucTList *xalg = new XrdOucTList(palg, anum); anum[0]++;
              if (algLast) algLast->next = xalg;
                 else      algFirst      = xalg;
              algLast = xalg;
              continue;
             }
          if (!(palg = Config.GetWord()))
             {eDest.Emsg("Config", "chksum max not specified"); return 1;}
          if (XrdOuca2x::a2i(eDest, "chksum max", palg, &jmax, 0)) return 1;
         }

// Verify we have an algoritm
//
   if (!algFirst)
      {eDest.Emsg("Config", "chksum algorithm not specified"); return 1;}
   if (JobCKT) free(JobCKT);
   JobCKT = strdup(algFirst->text);

// Handle alternate checksums
//
   while((tP = JobCKTLST)) {JobCKTLST = tP->next; delete tP;}
   JobCKTLST = algFirst;
   if (algFirst->next) JobCKCGI = 2;

// Handle program if we have one
//
   if (palg)
      {int n = strlen(palg);
       if (n+2 >= (int)sizeof(prog))
          {eDest.Emsg("Config", "cksum program too long"); return 1;}
       strcpy(prog, palg); palg = prog+n; *palg++ = ' '; n = sizeof(prog)-n-1;
       if (!Config.GetRest(palg, n))
          {eDest.Emsg("Config", "cksum parameters too long"); return 1;}
      } else *prog = 0;

// Check if we have a program. If not, then this will be a local checksum and
// the algorithm will be verified after we load the filesystem.
//
   if (*prog) JobLCL = 0;
      else {  JobLCL = 1; Proc = &CheckSum; strcpy(prog, "chksum");}

// Set up the program and job
//
   if (!theProg) theProg = new XrdOucProg(0);
   if (theProg->Setup(prog, &eDest, Proc)) return 1;
   if (JobCKS) delete JobCKS;
   if (jmax) JobCKS = new XrdXrootdJob(Sched, theProg, "chksum", jmax);
      else   JobCKS = 0;
   return 0;
}
  
/******************************************************************************/
/*                                  x d i g                                   */
/******************************************************************************/

/* Function: xdig

   Purpose:  To parse the directive: diglib * <parms>

             *         use builtin digfs library (only one supported now).
             parms     parameters for digfs.

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xdig(XrdOucStream &Config)
{
    char parms[4096], *val;

// Get the path
//
   if (!(val = Config.GetWord()))
      {eDest.Emsg("Config", "diglib not specified"); return 1;}

// Make sure it refers to an internal one
//
   if (strcmp(val, "*"))
      {eDest.Emsg("Config", "builtin diglib not specified"); return 1;}

// Grab the parameters
//
    if (!Config.GetRest(parms, sizeof(parms)))
       {eDest.Emsg("Config", "diglib parameters too long"); return 1;}
    if (digParm) free(digParm);
    digParm = strdup(parms);

// All done
//
   return 0;
}
  
/******************************************************************************/
/*                                  x e x p                                   */
/******************************************************************************/

/* Function: xexp

   Purpose:  To parse the directive: export <path> [lock|nolock] [mwfiles]

             <path>    the path to be exported.

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xexp(XrdOucStream &Config)
{
    char *val, pbuff[1024];
    int   popt = 0;

// Get the path
//
   val = Config.GetWord();
   if (!val || !val[0])
      {eDest.Emsg("Config", "export path not specified"); return 1;}
   strlcpy(pbuff, val, sizeof(pbuff));

// Get export lock option
//
   while((val = Config.GetWord()))
      {     if (!strcmp( "nolock", val)) popt |=  XROOTDXP_NOLK;
       else if (!strcmp(   "lock", val)) popt &= ~XROOTDXP_NOLK;
       else if (!strcmp("mwfiles", val)) popt |=  XROOTDXP_NOMWCHK;
       else {Config.RetToken(); break;}
      }

// Add path to configuration
//
   return xexpdo(pbuff, popt);
}

/******************************************************************************/

int XrdXrootdProtocol::xexpdo(char *path, int popt)
{
   char *opaque;
   int   xopt;

// Check if we are exporting a generic name
//
   if (*path == '*')
      {popt |= XROOTDXP_NOSLASH | XROOTDXP_NOCGI;
       if (*(path+1))
          {if (*(path+1) == '?') popt &= ~XROOTDXP_NOCGI;
              else {eDest.Emsg("Config","invalid export path -",path);return 1;}
          }
       XPList.Set(popt, path);
       return 0;
      }

// Make sure path start with a slash
//
   if (rpCheck(path, &opaque))
      {eDest.Emsg("Config", "non-absolute export path -", path); return 1;}

// Record the path
//
   if (!(xopt = Squash(path)) || xopt != (popt|XROOTDXP_OK))
      XPList.Insert(path, popt);
   return 0;
}
  
/******************************************************************************/
/*                                  x f s l                                   */
/******************************************************************************/

/* Function: xfsl

   Purpose:  To parse the directive: fslib [throttle | [-2] <fspath2>]
                                           {default  | [-2] <fspath1>}
                                          | ++ <fspath2>

             -2        Uses version2 of the plugin initializer.
                       This is ignored now because it's always done.
             ++        Pushes a wrapper onto the library stack.
             throttle  load libXrdThrottle.so as the head interface.
             <fspath2> load the named library as the head interface.
             default   load libXrdOfs.so ro libXrdPss.so as the tail
                       interface. This is the default.
             <fspath1> load the named library as the tail interface.

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xfsl(XrdOucStream &Config)
{
    char *val;

// Get the path
//
   if (!(val = Config.GetWord()))
      {eDest.Emsg("Config", "fslib not specified"); return 1;}

// First check for a psuhdown
//
   if (!strcmp("++", val))
      {if (!(val = Config.GetWord()))
          {eDest.Emsg("Config", "fslib wrapper not specified"); return 1;}
       if (strcmp("throttle", val))  FSLPath.push_back((std::string)val);
          else FSLPath.push_back("libXrdThrottle.so");
       return 0;
      }

// Clear storage pointers
//
   if (FSLib[0]) {free(FSLib[0]); FSLib[0] = 0;}
   if (FSLib[1]) {free(FSLib[1]); FSLib[1] = 0;}

// Check if this is "thottle"
//
   if (!strcmp("throttle", val))
      {FSLib[1] = strdup("libXrdThrottle.so");
       if (!(val = Config.GetWord()))
          {eDest.Emsg("Config","fslib throttle target library not specified");
           return 1;
          }
       return xfsL(Config, val, 0);
      }

// Check for default or default library, the common case
//
   if (xfsL(Config, val, 1))    return 1;
   if (!FSLib[1])               return 0;

// If we dont have another token, then demote the previous library
//
   if (!(val = Config.GetWord()))
      {FSLib[0] = FSLib[1]; FSLib[1] = 0;
       return 0;
      }

// Check for default or default library, the common case
//
   return xfsL(Config, val, 0);
}

/******************************************************************************/

int XrdXrootdProtocol::xfsL(XrdOucStream &Config, char *val, int lix)
{
    char *Slash;

// Check if this is a version token
//
   if (!strcmp(val, "-2"))
      {if (!(val = Config.GetWord()))
          {eDest.Emsg("Config", "fslib not specified"); return 1;}
      }

// We will play fast and furious with the syntax as "default" should not be
// prefixed with a version number but will let that pass.
//
   if (!strcmp("default", val)) return 0;

// If this is the "standard" name tell the user that we are ignoring this lib.
// Otherwise, record the path and return.
//
   if (!(Slash = rindex(val, '/'))) Slash = val;
      else Slash++;
   if (!strcmp(Slash, "libXrdOfs.so"))
      eDest.Say("Config warning: 'fslib libXrdOfs.so' is actually built-in.");
      else FSLib[lix] = strdup(val);
   return 0;
}

/******************************************************************************/
/*                                  x f s o                                   */
/******************************************************************************/
  
/* Function: xfso

   Purpose:  To parse the directive: fsoverload [options]

   options:  [[no]bypass] [redirect <host>:<port>[%<prvhost>:<port>]]
             [stall <sec>]

             bypass    If path is a forwarding path, redirect client to the
                       location specified in the path to bypass this server.
                       The default is nobypass.
             redirect  Redirect the request to the specified destination.
             stall     Stall the client <sec> seconds. The default is 33.
*/

int XrdXrootdProtocol::xfso(XrdOucStream &Config)
{
    static const int rHLen = 264;
    char rHost[2][rHLen], *hP[2] = {0,0}, *val;
    int  rPort[2], bypass = -1, stall = -1;

// Process all of the options
//
   while((val = Config.GetWord()) && *val)
        {     if (!strcmp(val, "bypass"))   bypass = 1;
         else if (!strcmp(val, "nobypass")) bypass = 0;
         else if (!strcmp(val, "redirect"))
                 {val = Config.GetWord();
                  if (!xred_php(val, hP, rPort)) return 1;
                  for (int i = 0; i < 2; i++)
                      {if (!hP[i]) rHost[i][0] = 0;
                          else {strlcpy(rHost[i], hP[i], rHLen);
                                hP[i] = rHost[i];
                               }
                      }
                 }
         else if (!strcmp(val, "stall"))
                 {if (!(val = Config.GetWord()) || !(*val))
                     {eDest.Emsg("Config", "stall value not specified");
                      return 1;
                     }
                  if (XrdOuca2x::a2tm(eDest,"stall",val,&stall,0,32767))
                     return 1;
                 }
         else {eDest.Emsg("config","invalid fsoverload option",val); return 1;}
        }

// Set all specified values
//
   if (bypass >= 0) OD_Bypass = (bypass ? true : false);
   if (stall  >= 0) OD_Stall  = stall;
   if (hP[0])
      {if (Route[RD_ovld].Host[0]) free(Route[RD_ovld].Host[0]);
       if (Route[RD_ovld].Host[1]) free(Route[RD_ovld].Host[1]);
       Route[RD_ovld].Host[0] = strdup(hP[0]);
       Route[RD_ovld].Port[0] = rPort[0];
       Route[RD_ovld].RDSz[0] = strlen(hP[0]);
       if (hP[1])
          {Route[RD_ovld].Host[1] = strdup(hP[1]);
           Route[RD_ovld].Port[1] = rPort[1];
           Route[RD_ovld].RDSz[1] = strlen(hP[1]);
          } else {
           Route[RD_ovld].Host[1] = Route[RD_ovld].Host[0];
           Route[RD_ovld].Port[1] = Route[RD_ovld].Port[0];
           Route[RD_ovld].RDSz[1] = Route[RD_ovld].RDSz[0];
          }
       OD_Redir = true;
      } else OD_Redir = false;

   return 0;
}

/******************************************************************************/
/*                                  x g p f                                   */
/******************************************************************************/

/* Function: xgpf

   Purpose:  To parse the directive: gpflib <path> <parms>

             <path>    library path to use or default to use the builtin one.
             parms     optional parameters.

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xgpf(XrdOucStream &Config)
{
    char parms[4096], *val;

// Remove any previous parameters
//
   if (gpfLib)  {free(gpfLib);  gpfLib  = 0;}
   if (gpfParm) {free(gpfParm); gpfParm = 0;}

// Get the path
//
   if (!(val = Config.GetWord()))
      {eDest.Emsg("Config", "gpflib not specified"); return 1;}

// If this refers to out default, then keep the library pointer nil
//
   if (strcmp(val, "default")) gpfLib = strdup(val);

// Grab the parameters
//
    if (!Config.GetRest(parms, sizeof(parms)))
       {eDest.Emsg("Config", "gpflib parameters too long"); return 1;}
    gpfParm = strdup(parms);

// All done
//
   return 0;
}
  
/******************************************************************************/
/*                                  x l o g                                   */
/******************************************************************************/

/* Function: xlog

   Purpose:  To parse the directive: log <events>

             <events> the blank separated list of events to log.

   Output: 0 upon success or 1 upon failure.
*/

int XrdXrootdProtocol::xlog(XrdOucStream &Config)
{
    char *val;
    static struct logopts {const char *opname; int opval;} lgopts[] =
       {
        {"all",     -1},
        {"disc",    SYS_LOG_02},
        {"login",   SYS_LOG_01}
       };
    int i, neg, lgval = -1, numopts = sizeof(lgopts)/sizeof(struct logopts);

    if (!(val = Config.GetWord()))
       {eDest.Emsg("config", "log option not specified"); return 1;}
    while (val)
          {if ((neg = (val[0] == '-' && val[1]))) val++;
           for (i = 0; i < numopts; i++)
               {if (!strcmp(val, lgopts[i].opname))
                   {if (neg) lgval &= ~lgopts[i].opval;
                       else  lgval |=  lgopts[i].opval;
                    break;
                   }
               }
           if (i >= numopts) eDest.Emsg("config","invalid log option",val);
           val = Config.GetWord();
          }
    eDest.setMsgMask(lgval);
    return 0;
}

/******************************************************************************/
/*                                  x m o n                                   */
/******************************************************************************/

/* Function: xmon

   Purpose:  Parse directive: monitor [all] [auth]  [flush [io] <sec>]
                                      [fstat <sec> [lfn] [ops] [ssq] [xfr <n>]
                                      [ident <sec>] [mbuff <sz>] [rbuff <sz>]
                                      [rnums <cnt>] [window <sec>]
                                      dest [Events] <host:port>

   Events: [ccm] [files] [fstat] [info] [io] [iov] [pfc] [redir] [user]

         all                enables monitoring for all connections.
         auth               add authentication information to "user".
         flush  [io] <sec>  time (seconds, M, H) between auto flushes. When
                            io is given applies only to i/o events.
         fstat  <sec>       produces an "f" stream for open & close events
                            <sec> specifies the flush interval (also see xfr)
                            lfn    - adds lfn to the open event
                            ops    - adds the ops record when the file is closed
                            ssq    - computes the sum of squares for the ops rec
                            xfr <n>- inserts i/o stats for open files every
                                     <sec>*<n>. Minimum is 1.
         ident  <sec>       time (seconds, M, H) between identification records.
         mbuff  <sz>        size of message buffer for event trace monitoring.
         rbuff  <sz>        size of message buffer for redirection monitoring.
         rnums  <cnt>       bumber of redirections monitoring streams.
         window <sec>       time (seconds, M, H) between timing marks.
         dest               specified routing information. Up to two dests
                            may be specified.
         ccm                monitor cache context management
         files              only monitors file open/close events.
         fstats             vectors the "f" stream to the destination
         info               monitors client appid and info requests.
         io                 monitors I/O requests, and files open/close events.
         iov                like I/O but also unwinds vector reads.
         pfc                monitor proxy file cache
         redir              monitors request redirections
         user               monitors user login and disconnect events.
         <host:port>        where monitor records are to be sentvia UDP.

   Output: 0 upon success or !0 upon failure. Ignored by master.
*/
int XrdXrootdProtocol::xmon(XrdOucStream &Config)
{   static const char *mrMsg[] = {"monitor mbuff value not specified",
                                  "monitor rbuff value not specified",
                                  "monitor mbuff", "monitor rbuff"
                                 };
    char  *val = 0, *cp, *monDest[2] = {0, 0};
    long long tempval;
    int i, monFlash = 0, monFlush=0, monMBval=0, monRBval=0, monWWval=0;
    int    monIdent = 3600, xmode=0, monMode[2] = {0, 0}, mrType, *flushDest;
    int    monRnums = 0, monFSint = 0, monFSopt = 0, monFSion = 0;
    int    haveWord = 0;

    while(haveWord || (val = Config.GetWord()))
         {haveWord = 0;
               if (!strcmp("all",  val)) xmode = XROOTD_MON_ALL;
          else if (!strcmp("auth",  val))
                  monMode[0] = monMode[1] = XROOTD_MON_AUTH;
          else if (!strcmp("flush", val))
                {if ((val = Config.GetWord()) && !strcmp("io", val))
                    {    flushDest = &monFlash; val = Config.GetWord();}
                    else flushDest = &monFlush;
                 if (!val)
                    {eDest.Emsg("Config", "monitor flush value not specified");
                     return 1;
                    }
                 if (XrdOuca2x::a2tm(eDest,"monitor flush",val,
                                           flushDest,1)) return 1;
                }
          else if (!strcmp("fstat",val))
                  {if (!(val = Config.GetWord()))
                      {eDest.Emsg("Config", "monitor fstat value not specified");
                       return 1;
                      }
                   if (XrdOuca2x::a2tm(eDest,"monitor fstat",val,
                                             &monFSint,0)) return 1;
                   while((val = Config.GetWord()))
                        if (!strcmp("lfn", val)) monFSopt |=  XROOTD_MON_FSLFN;
                   else if (!strcmp("ops", val)) monFSopt |=  XROOTD_MON_FSOPS;
                   else if (!strcmp("ssq", val)) monFSopt |=  XROOTD_MON_FSSSQ;
                   else if (!strcmp("xfr", val))
                           {if (!(val = Config.GetWord()))
                               {eDest.Emsg("Config", "monitor fstat xfr count not specified");
                                return 1;
                               }
                            if (XrdOuca2x::a2i(eDest,"monitor fstat io count",
                                               val, &monFSion,1)) return 1;
                            monFSopt |=  XROOTD_MON_FSXFR;
                           }
                   else {haveWord = 1; break;}
                  }
          else if (!strcmp("mbuff",val) || !strcmp("rbuff",val))
                  {mrType = (*val == 'r');
                   if (!(val = Config.GetWord()))
                      {eDest.Emsg("Config",  mrMsg[mrType]); return 1;}
                   if (XrdOuca2x::a2sz(eDest,mrMsg[mrType+2], val,
                                             &tempval, 1024, 65536)) return 1;
                   if (mrType) monRBval = static_cast<int>(tempval);
                      else     monMBval = static_cast<int>(tempval);
                  }
          else if (!strcmp("ident", val))
                {if (!(val = Config.GetWord()))
                    {eDest.Emsg("Config", "monitor ident value not specified");
                     return 1;
                    }
                 if (XrdOuca2x::a2tm(eDest,"monitor ident",val,
                                           &monIdent,0)) return 1;
                }
          else if (!strcmp("rnums", val))
                {if (!(val = Config.GetWord()))
                    {eDest.Emsg("Config", "monitor rnums value not specified");
                     return 1;
                    }
                 if (XrdOuca2x::a2i(eDest,"monitor rnums",val, &monRnums,1,
                                    XrdXrootdMonitor::rdrMax)) return 1;
                }
          else if (!strcmp("window", val))
                {if (!(val = Config.GetWord()))
                    {eDest.Emsg("Config", "monitor window value not specified");
                     return 1;
                    }
                 if (XrdOuca2x::a2tm(eDest,"monitor window",val,
                                           &monWWval,1)) return 1;
                }
          else break;
         }

    if (!val) {eDest.Emsg("Config", "monitor dest not specified"); return 1;}

    for (i = 0; i < 2; i++)
        {if (strcmp("dest", val)) break;
         while((val = Config.GetWord()))
                   if (!strcmp("ccm",  val)) monMode[i] |=  XROOTD_MON_CCM;
              else if (!strcmp("files",val)) monMode[i] |=  XROOTD_MON_FILE;
              else if (!strcmp("fstat",val)) monMode[i] |=  XROOTD_MON_FSTA;
              else if (!strcmp("info", val)) monMode[i] |=  XROOTD_MON_INFO;
              else if (!strcmp("io",   val)) monMode[i] |=  XROOTD_MON_IO;
              else if (!strcmp("iov",  val)) monMode[i] |= (XROOTD_MON_IO
                                                           |XROOTD_MON_IOV);
              else if (!strcmp("pfc",  val)) monMode[i] |=  XROOTD_MON_PFC;
              else if (!strcmp("redir",val)) monMode[i] |=  XROOTD_MON_REDR;
              else if (!strcmp("user", val)) monMode[i] |=  XROOTD_MON_USER;
              else break;
          if (!val) {eDest.Emsg("Config","monitor dest value not specified");
                     return 1;
                    }
          if (!(cp = index(val, (int)':')) || !atoi(cp+1))
             {eDest.Emsg("Config","monitor dest port missing or invalid in",val);
              return 1;
             }
          monDest[i] = strdup(val);
         if (!(val = Config.GetWord())) break;
        }

    if (val)
       {if (!strcmp("dest", val))
           eDest.Emsg("Config", "Warning, a maximum of two dest values allowed.");
           else eDest.Emsg("Config", "Warning, invalid monitor option", val);
       }

// Make sure dests differ
//
   if (monDest[0] && monDest[1] && !strcmp(monDest[0], monDest[1]))
      {eDest.Emsg("Config", "Warning, monitor dests are identical.");
       monMode[0] |= monMode[1]; monMode[1] = 0;
       free(monDest[1]); monDest[1] = 0;
      }

// Add files option if I/O is enabled
//
   if (monMode[0] & XROOTD_MON_IO) monMode[0] |= XROOTD_MON_FILE;
   if (monMode[1] & XROOTD_MON_IO) monMode[1] |= XROOTD_MON_FILE;

// If ssq was specified, make sure we support IEEE754 floating point
//
#if !defined(__solaris__) || !defined(_IEEE_754)
   if (monFSopt & XROOTD_MON_FSSSQ && !(std::numeric_limits<double>::is_iec559))
      {monFSopt &= !XROOTD_MON_FSSSQ;
       eDest.Emsg("Config","Warning, 'fstat ssq' ignored; platform does not "
                           "use IEEE754 floating point.");
      }
#endif

// Set the monitor defaults
//
   XrdXrootdMonitor::Defaults(monMBval, monRBval, monWWval,
                              monFlush, monFlash, monIdent, monRnums,
                              monFSint, monFSopt, monFSion);

   if (monDest[0]) monMode[0] |= (monMode[0] ? xmode : XROOTD_MON_FILE|xmode);
   if (monDest[1]) monMode[1] |= (monMode[1] ? xmode : XROOTD_MON_FILE|xmode);
   XrdXrootdMonitor::Defaults(monDest[0],monMode[0],monDest[1],monMode[1]);

   return 0;
}
  
/******************************************************************************/
/*                                 x p i d f                                  */
/******************************************************************************/

/* Function: xpidf

   Purpose:  To parse the directive: pidpath <path>

             <path>    the path where the pid file is to be created.

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xpidf(XrdOucStream &Config)
{
    char *val;

// Get the path
//
   val = Config.GetWord();
   if (!val || !val[0])
      {eDest.Emsg("Config", "pidpath not specified"); return 1;}

// Record the path
//
   if (pidPath) free(pidPath);
   pidPath = strdup(val);
   return 0;
}

/******************************************************************************/
/*                                 x p r e p                                  */
/******************************************************************************/

/* Function: xprep

   Purpose:  To parse the directive: prep [keep <sec>] [scrub <sec>]
                                          [logdir <path>]
         keep   <sec>  time (seconds, M, H) to keep logdir entries.
         scrub  <sec>  time (seconds, M, H) between logdir scrubs.
         logdir <path> the absolute path to the prepare log directory.

   Output: 0 upon success or !0 upon failure. Ignored by master.
*/
int XrdXrootdProtocol::xprep(XrdOucStream &Config)
{   int   rc, keep = 0, scrub=0;
    char  *ldir=0,*val,buff[1024];

    if (!(val = Config.GetWord()))
       {eDest.Emsg("Config", "prep options not specified"); return 1;}

        do { if (!strcmp("keep", val))
                {if (!(val = Config.GetWord()))
                    {eDest.Emsg("Config", "prep keep value not specified");
                     return 1;
                    }
                 if (XrdOuca2x::a2tm(eDest,"prep keep int",val,&keep,1)) return 1;
                }
        else if (!strcmp("scrub", val))
                {if (!(val = Config.GetWord()))
                    {eDest.Emsg("Config", "prep scrub value not specified");
                     return 1;
                    }
                 if (XrdOuca2x::a2tm(eDest,"prep scrub",val,&scrub,0)) return 1;
                }
        else if (!strcmp("logdir", val))
                {if (!(ldir = Config.GetWord()))
                   {eDest.Emsg("Config", "prep logdir value not specified");
                    return 1;
                   }
                }
        else eDest.Emsg("Config", "Warning, invalid prep option", val);
       } while((val = Config.GetWord()));

// Set the values
//
   if (scrub || keep) XrdXrootdPrepare::setParms(scrub, keep);
   if (ldir) 
       if ((rc = XrdOucUtils::genPath(buff, sizeof(buff), ldir, myInst)) < 0
       ||  (rc = XrdOucUtils::makePath(buff, XrdOucUtils::pathMode)) < 0
       ||  (rc = XrdXrootdPrepare::setParms(buff)) < 0)
          {eDest.Emsg("Config", rc, "process logdir", ldir);
           return 1;
          }
   return 0;
}

/******************************************************************************/
/*                                  x r e d                                   */
/******************************************************************************/
  
/* Function: xred

   Purpose:  To parse the directive: redirect <host>:<port>[%<prvhost>:<port>]
                                              {<funcs>|[?]<path>}

             <funcs>   are one or more of the following functions that will
                       be immediately redirected to <host>:<port>. Each function
                       may be prefixed by a minus sign to disable redirection.

                       chmod dirlist locate mkdir mv prepare rm rmdir stat

             <paths>   redirects the client when an attempt is made to open
                       one of absolute <paths>. Up to 4 different redirect
                       combinations may be specified. When prefixed by "?"
                       then the redirect applies to any operation on the path
                       that results in an ENOENT error.

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xred(XrdOucStream &Config)
{
    static struct rediropts {const char *opname; RD_func opval;} rdopts[] =
       {
        {"chmod",    RD_chmod},
        {"chksum",   RD_chksum},
        {"dirlist",  RD_dirlist},
        {"locate",   RD_locate},
        {"mkdir",    RD_mkdir},
        {"mv",       RD_mv},
        {"prepare",  RD_prepare},
        {"prepstage",RD_prepstg},
        {"rm",       RD_rm},
        {"rmdir",    RD_rmdir},
        {"stat",     RD_stat},
        {"trunc",    RD_trunc}
       };
    static const int rHLen = 264;
    char rHost[2][rHLen], *hP[2], *val;
    int i, k, neg, numopts = sizeof(rdopts)/sizeof(struct rediropts);
    int rPort[2], isQ = 0;

// Get the host and port
//
   val = Config.GetWord();
   if (!xred_php(val, hP, rPort)) return 1;

// Copy out he values as the target variable will be lost
//
   for (i = 0; i < 2; i++)
       {if (!hP[i]) rHost[i][0] = 0;
           else {strlcpy(rHost[i], hP[i], rHLen);
                 hP[i] = rHost[i];
                }
       }

// Set all redirect target functions
//
    if (!(val = Config.GetWord()))
       {eDest.Emsg("config", "redirect option not specified"); return 1;}

    if (*val == '/' || (isQ = ((*val == '?') || !strcmp(val,"enoent"))))
       {if (isQ)
           {RQLxist = 1;
            if (!(val = Config.GetWord()))
               {eDest.Emsg("Config", "redirect path not specified.");
                return 1;
               }
            if (*val != '/')
               {eDest.Emsg("Config", "non-absolute redirect path -", val);
                return 1;
               }
           }
        for (k = static_cast<int>(RD_open1); k < RD_Num; k++)
            if (xred_xok(k, hP, rPort)) break;
        if (k >= RD_Num)
           {eDest.Emsg("Config", "too many diffrent path redirects"); return 1;}
        xred_set(RD_func(k), hP, rPort);
        do {if (isQ) RQList.Insert(val, k, 0);
               else  RPList.Insert(val, k, 0);
            if ((val = Config.GetWord()) && *val != '/')
               {eDest.Emsg("Config", "non-absolute redirect path -", val);
                return 1;
               }
           } while(val);
        return 0;
       }

    while (val)
          {if (!strcmp(val, "all"))
              {for (i = 0; i < numopts; i++)
                   xred_set(rdopts[i].opval, hP, rPort);
              }
              else {if ((neg = (val[0] == '-' && val[1]))) val++;
                    for (i = 0; i < numopts; i++)
                       {if (!strcmp(val, rdopts[i].opname))
                           {if (neg) xred_set(rdopts[i].opval, 0, 0);
                               else  xred_set(rdopts[i].opval, hP, rPort);
                            break;
                           }
                       }
                   if (i >= numopts)
                      eDest.Emsg("config", "invalid redirect option", val);
                  }
          val = Config.GetWord();
         }
   return 0;
}

bool XrdXrootdProtocol::xred_php(char *val, char *hP[2], int rPort[2])
{
   char *pp;

// Make sure we have a value
//
   if (!val || !(*val))
      {eDest.Emsg("config", "redirect option not specified"); return false;}

// Check if we have two hosts here
//
   hP[0] = val;
   if (!(pp = index(val, '%'))) hP[1] = 0;
      else {hP[1] = pp+1; *pp = 0;}

// Verify corectness here
//
   if (!(*val) || (hP[1] && !*hP[1]))
      {eDest.Emsg("Config", "malformed redirect host specification");
       return false;
      }

// Process the hosts
//
   for (int i = 0; i < 2; i++)
       {if (!(val = hP[i])) break;
        if (!val || !val[0] || val[0] == ':')
           {eDest.Emsg("Config", "redirect host not specified"); return false;}
        if (!(pp = rindex(val, ':')))
           {eDest.Emsg("Config", "redirect port not specified"); return false;}
        if (!(rPort[i] = atoi(pp+1)))
           {eDest.Emsg("Config", "redirect port is invalid");    return false;}
        *pp = '\0';
       }

// All done
//
   return true;
}

void XrdXrootdProtocol::xred_set(RD_func func, char *rHost[2], int rPort[2])
{

// Reset static redirection
//
   if (Route[func].Host[0]) free(Route[func].Host[0]);
   if (Route[func].Host[0] != Route[func].Host[1]) free(Route[func].Host[1]);

   if (rHost)
      {Route[func].Host[0] = strdup(rHost[0]);
       Route[func].Port[0] = rPort[0];
      } else {
       Route[func].Host[0] = Route[func].Host[1] = 0;
       Route[func].Port[0] = Route[func].Port[1] = 0;
       return;
      }

   if (!rHost[1])
      {Route[func].Host[1] = Route[func].Host[0];
       Route[func].Port[1] = Route[func].Port[0];
      } else {
       Route[func].Host[1] = strdup(rHost[1]);
       Route[func].Port[1] = rPort[1];
      }
}

bool XrdXrootdProtocol::xred_xok(int func, char *rHost[2], int rPort[2])
{
   if (!Route[func].Host[0]) return true;

   if (strcmp(Route[func].Host[0], rHost[0])
   ||  Route[func].Port[0] != rPort[0]) return false;

   if (!rHost[1]) return Route[func].Host[0] == Route[func].Host[1];

   if (strcmp(Route[func].Host[1], rHost[1])
   ||  Route[func].Port[1] != rPort[1]) return false;

   return true;
}

/******************************************************************************/
/*                                 x s e c l                                  */
/******************************************************************************/

/* Function: xsecl

   Purpose:  To parse the directive: seclib {default | <path>}

             <path>    the path of the security library to be used.
                       "default" uses the default security library.

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xsecl(XrdOucStream &Config)
{
    char *val;

// Get the path
//
   val = Config.GetWord();
   if (!val || !val[0])
      {eDest.Emsg("Config", "XRootd seclib not specified"); return 1;}

// Record the path
//
   if (SecLib) free(SecLib);
   SecLib = strdup(val);
   return 0;
}

/******************************************************************************/
/*                                  x t l s                                   */
/******************************************************************************/
  
/* Function: xtls

   Purpose:  To parse the directive: [capable] <reqs>

             capable   Enforce TLS requirements only for TLS capable clients.
                       Otherwise, TLS is enforced for all clients.
             <reqs>    are one or more of the following tls requirements. Each
                       may be prefixed by a minus sign to disable it. Note
                       this directive is cummalitive.

                       all     Requires all of the below.
                       data    All bound sockets must use TLS. When specified,
                               session is implied unless login is specified.
                       gpfile  getile and putfile requests must use TLS
                       login   Logins and all subsequent requests must use TLS
                       none    Turns all requirements off (default).
                       off     Synonym for none.
                       session All requests after login must use TLS
                       tpc     Third party copy requests must use TLS

  Output: 0 upon success or !0 upon failure.
*/

int XrdXrootdProtocol::xtls(XrdOucStream &Config)
{
    static const int Req_TLSAll = Req_TLSData|Req_TLSLogin|Req_TLSTPC;
    static struct enforceopts {const char *opname; int opval; int enval;}
    enfopts[] =
       {
        {"all",      kXR_tlsAny,   Req_TLSAll},
        {"data",     kXR_tlsData,  Req_TLSData},
        {"gpfile",   kXR_tlsGPF,   Req_TLSGPFile},
        {"login",    kXR_tlsLogin, Req_TLSLogin},
        {"session",  kXR_tlsSess,  Req_TLSSess},
        {"tpc",      kXR_tlsTPC,   Req_TLSTPC}
       };
    char *val;
    int i, numopts = sizeof(enfopts)/sizeof(struct enforceopts);
    bool neg, forall = true;

    if (!(val = Config.GetWord()))
       {eDest.Emsg("config", "tls parameter not specified"); return 1;}

    if (!strcmp("capable", val))
       {forall = false;
        if (!(val = Config.GetWord()))
           {eDest.Emsg("config", "tls requirement not specified"); return 1;}
       }

    while (val)
         {if (!strcmp(val, "off") || !strcmp(val, "none"))
             {myRole &= ~kXR_tlsAny;
              if (forall) tlsCap = tlsNot = 0;
                 else tlsCap = 0;
             } else {
              if ((neg = (val[0] == '-' && val[1]))) val++;
              for (i = 0; i < numopts; i++)
                  {if (!strcmp(val, enfopts[i].opname))
                      {if (neg) myRole &= ~enfopts[i].opval;
                          else  myRole |=  enfopts[i].opval;
                       if (neg) tlsCap &= ~enfopts[i].enval;
                          else  tlsCap |=  enfopts[i].enval;
                       if (forall)
                          {if (neg) tlsNot &= ~enfopts[i].enval;
                              else  tlsNot |=  enfopts[i].enval;
                          }
                       break;
                      }
                  }
              if (i >= numopts)
                 {eDest.Emsg("config", "Invalid tls requirement -", val);
                  return 1;
                 }
             }
          val = Config.GetWord();
         }

// If login specified, turn off session as it doesn't make sense together.
//
   if (myRole & kXR_tlsLogin) myRole &= ~kXR_tlsSess;
   if (tlsCap & Req_TLSLogin) tlsCap &= ~Req_TLSSess;
   if (tlsNot & Req_TLSLogin) tlsNot &= ~Req_TLSSess;

// If data needs TLS but the session does not, then force session TLS
//
   if ((myRole & kXR_tlsData) && !(myRole & (kXR_tlsLogin | kXR_tlsSess)))
      myRole |= kXR_tlsSess;
   if ((tlsCap & kXR_tlsData) && !(tlsCap & (Req_TLSLogin | Req_TLSSess)))
      tlsCap |= Req_TLSSess;
   if ((tlsNot & kXR_tlsData) && !(tlsNot & (Req_TLSLogin | Req_TLSSess)))
      tlsNot |= Req_TLSSess;

// If there are any TLS requirements then TLS must have been configured.
//
    if (myRole & kXR_tlsAny && !tlsCtx)
       {eDest.Emsg("config", "Unable to honor TLS requirement; "
                             "TLS not configured!");
        return 1;
       }
    return 0;
}
  
/******************************************************************************/
/*                                x t r a c e                                 */
/******************************************************************************/

/* Function: xtrace

   Purpose:  To parse the directive: trace <events>

             <events> the blank separated list of events to trace. Trace
                      directives are cummalative.

   Output: 0 upon success or 1 upon failure.
*/

int XrdXrootdProtocol::xtrace(XrdOucStream &Config)
{
    char *val;
    static struct traceopts {const char *opname; int opval;} tropts[] =
       {
        {"all",      TRACE_ALL},
        {"emsg",     TRACE_EMSG},
        {"debug",    TRACE_DEBUG},
        {"fs",       TRACE_FS},
        {"login",    TRACE_LOGIN},
        {"mem",      TRACE_MEM},
        {"stall",    TRACE_STALL},
        {"redirect", TRACE_REDIR},
        {"request",  TRACE_REQ},
        {"response", TRACE_RSP}
       };
    int i, neg, trval = 0, numopts = sizeof(tropts)/sizeof(struct traceopts);

    if (!(val = Config.GetWord()))
       {eDest.Emsg("config", "trace option not specified"); return 1;}
    while (val)
         {if (!strcmp(val, "off")) trval = 0;
             else {if ((neg = (val[0] == '-' && val[1]))) val++;
                   for (i = 0; i < numopts; i++)
                       {if (!strcmp(val, tropts[i].opname))
                           {if (neg) trval &= ~tropts[i].opval;
                               else  trval |=  tropts[i].opval;
                            break;
                           }
                       }
                   if (i >= numopts)
                      eDest.Emsg("config", "invalid trace option", val);
                  }
          val = Config.GetWord();
         }
    XrdXrootdTrace->What = trval;
    return 0;
}

/******************************************************************************/
/*                                x l i m i t                                 */
/******************************************************************************/

/* Function: xlimit

   Purpose:  To parse the directive: limit [prepare <count>] [noerror]

             prepare <count> The maximum number of prepares that are allowed
                             during the course of a single connection

             noerror         When possible, do not issue an error when a limit
                             is hit.

   Output: 0 upon success or 1 upon failure.
*/
int XrdXrootdProtocol::xlimit(XrdOucStream &Config)
{
   int plimit = -1;
   const char *word;

// Look for various limits set
//
   while ( (word = Config.GetWord()) ) {
      if (!strcmp(word, "prepare")) {
          if (!(word = Config.GetWord()))
          {
             eDest.Emsg("Config", "'limit prepare' value not specified");
             return 1;
          }
          if (XrdOuca2x::a2i(eDest, "limit prepare", word, &plimit, 0)) { return 1; }
      } else if (!strcmp(word, "noerror")) {
          LimitError = false;
      }
   }
   if (plimit >= 0) {PrepareLimit = plimit;}
   return 0;
}
